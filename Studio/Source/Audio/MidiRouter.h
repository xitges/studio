/*
  ==============================================================================

    MidiRouter.h
    Created: 2026-04-04
    Author:  홍준영

    Lock-free, real-time-safe MIDI routing layer for Live Performance Mode.

    Responsibilities:
      1. Classify each incoming MIDI note as Instrument or ClipTrigger.
      2. Determine the target DAW channel for Instrument notes.
      3. Capture CC messages in a lock-free queue for the message thread.
      4. Capture ClipTrigger events in a lock-free queue for the message thread.

    Thread model:
      ─ Message thread  : writes routing config (setNoteRoute, setPadRange, etc.)
      ─ Audio thread    : reads routing config (classifyNote), enqueues events
      ─ Message thread  : drains event queues (drainCcEvent, drainClipTrigger)

    Backward compatibility:
      If no pad range is configured and no per-note overrides are set, every
      incoming note is routed to the default instrument channel — identical to
      the previous single-midiTargetChannel behaviour.

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

// ---------------------------------------------------------------------------
// Routing result type
// ---------------------------------------------------------------------------

enum class MidiRoutingType : uint8_t
{
    Instrument  = 0,  ///< Send to synth / plugin on dawChannel
    ClipTrigger = 1,  ///< Intercept — do NOT send to any instrument
};

// ---------------------------------------------------------------------------
// Event structs (audio thread → message thread, through lock-free FIFOs)
// ---------------------------------------------------------------------------

/** A CC message captured from the audio thread, ready for message-thread mapping. */
struct CcEvent
{
    uint8_t ccNumber   = 0;
    uint8_t value      = 0;    ///< 0-127
    int     dawChannel = -1;   ///< -1 = global / unassigned
};

/** A pad-note event captured from the audio thread. */
struct ClipTriggerEvent
{
    uint8_t note      = 0;
    uint8_t velocity  = 0;     ///< 0 = released, 1-127 = pressed
    uint8_t padIndex  = 0xFF;  ///< 0xFF = unmapped pad
};

// ---------------------------------------------------------------------------
// MidiRouter
// ---------------------------------------------------------------------------

class MidiRouter
{
public:
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    MidiRouter()
    {
        // Initialise per-note arrays to "no override" sentinel
        for (int i = 0; i < 128; ++i)
        {
            noteRouteType_    [(size_t)i].store(kNoOverride,          std::memory_order_relaxed);
            noteTargetChannel_[(size_t)i].store((int8_t)-1,          std::memory_order_relaxed);
            padMapping_       [(size_t)i].store((uint8_t)0xFF,       std::memory_order_relaxed);
        }
    }

    // -------------------------------------------------------------------------
    // Message-thread configuration API
    // -------------------------------------------------------------------------

    /** Replaces the old midiTargetChannel.
        All notes without an explicit override route to this DAW channel. */
    void setDefaultInstrumentChannel(int dawCh) noexcept
    {
        defaultInstrumentChannel_.store(juce::jlimit(0, 15, dawCh),
                                        std::memory_order_relaxed);
    }

    int getDefaultInstrumentChannel() const noexcept
    {
        return defaultInstrumentChannel_.load(std::memory_order_relaxed);
    }

    /** Mark a MIDI note range [noteLow, noteHigh] as ClipTrigger (pad range).
        Notes in this range are NOT forwarded to any instrument.
        Call setPadRange(0, -1) or setPadRange(128, 0) to disable. */
    void setPadRange(int noteLow, int noteHigh) noexcept
    {
        if (noteLow > noteHigh)
        {
            // Disable pad range: set to impossible range
            padRangeLow_ .store(128, std::memory_order_relaxed);
            padRangeHigh_.store(0,   std::memory_order_relaxed);
        }
        else
        {
            padRangeLow_ .store((uint8_t)juce::jlimit(0, 127, noteLow),  std::memory_order_relaxed);
            padRangeHigh_.store((uint8_t)juce::jlimit(0, 127, noteHigh), std::memory_order_relaxed);
        }
    }

    /** Per-note routing override.
        type            : Instrument or ClipTrigger
        targetDawChannel: the DAW channel to use when type == Instrument.
                          Pass -1 to use the default instrument channel. */
    void setNoteRoute(int note, MidiRoutingType type, int targetDawChannel = -1) noexcept
    {
        if (note < 0 || note > 127) return;
        noteRouteType_    [(size_t)note].store((uint8_t)type, std::memory_order_relaxed);
        noteTargetChannel_[(size_t)note].store(
            (int8_t)juce::jlimit(-1, 15, targetDawChannel), std::memory_order_relaxed);
    }

    /** Map a MIDI note number to a logical pad index (0-63).
        Used when type == ClipTrigger so the clip system knows which pad fired. */
    void setPadMapping(int note, uint8_t padIndex) noexcept
    {
        if (note < 0 || note > 127) return;
        padMapping_[(size_t)note].store(padIndex, std::memory_order_relaxed);
    }

    /** Remove all per-note overrides (pad range is kept). */
    void clearNoteRoutes() noexcept
    {
        for (int i = 0; i < 128; ++i)
        {
            noteRouteType_    [(size_t)i].store(kNoOverride, std::memory_order_relaxed);
            noteTargetChannel_[(size_t)i].store((int8_t)-1, std::memory_order_relaxed);
        }
    }

    // -------------------------------------------------------------------------
    // Audio-thread classification API  (no allocation, no lock)
    // -------------------------------------------------------------------------

    struct RouteResult
    {
        MidiRoutingType type;
        int     dawChannel;  ///< valid when type == Instrument
        uint8_t padIndex;    ///< valid when type == ClipTrigger (0xFF = unmapped)
    };

    /** Classify a MIDI note number.  Called once per NoteOn/NoteOff on the
        audio thread.  Returns instantly with no memory allocation. */
    RouteResult classifyNote(int note) const noexcept
    {
        const size_t n = (size_t)juce::jlimit(0, 127, note);

        // Per-note override takes highest priority
        const uint8_t routeType = noteRouteType_[n].load(std::memory_order_relaxed);
        if (routeType != kNoOverride)
        {
            const int8_t targetCh = noteTargetChannel_[n].load(std::memory_order_relaxed);
            const int dawCh = (targetCh < 0)
                              ? defaultInstrumentChannel_.load(std::memory_order_relaxed)
                              : (int)targetCh;
            const uint8_t padIdx = padMapping_[n].load(std::memory_order_relaxed);
            return { (MidiRoutingType)routeType, dawCh, padIdx };
        }

        // Pad range: notes in [low, high] are ClipTriggers
        const uint8_t lo = padRangeLow_ .load(std::memory_order_relaxed);
        const uint8_t hi = padRangeHigh_.load(std::memory_order_relaxed);
        if (lo <= hi && (uint8_t)note >= lo && (uint8_t)note <= hi)
        {
            const uint8_t padIdx = padMapping_[n].load(std::memory_order_relaxed);
            return { MidiRoutingType::ClipTrigger, -1, padIdx };
        }

        // Default: instrument on the default channel
        return { MidiRoutingType::Instrument,
                 defaultInstrumentChannel_.load(std::memory_order_relaxed),
                 0xFF };
    }

    // -------------------------------------------------------------------------
    // Lock-free event queues  (audio → message thread)
    // -------------------------------------------------------------------------

    static constexpr int kQueueSize = 256;

    /** Enqueue a CC event.  Called on audio thread.  Returns false if queue full. */
    bool enqueueCc(const CcEvent& ev) noexcept
    {
        int s1, bs1, s2, bs2;
        ccFifo_.prepareToWrite(1, s1, bs1, s2, bs2);
        if (bs1 == 0) return false;
        ccQueue_[(size_t)s1] = ev;
        ccFifo_.finishedWrite(bs1);
        return true;
    }

    /** Enqueue a ClipTrigger event.  Called on audio thread.  Returns false if full. */
    bool enqueueClipTrigger(const ClipTriggerEvent& ev) noexcept
    {
        int s1, bs1, s2, bs2;
        clipFifo_.prepareToWrite(1, s1, bs1, s2, bs2);
        if (bs1 == 0) return false;
        clipQueue_[(size_t)s1] = ev;
        clipFifo_.finishedWrite(bs1);
        return true;
    }

    /** Drain one CC event.  Called on message thread.  Returns false if empty. */
    bool drainCcEvent(CcEvent& out) noexcept
    {
        int s1, bs1, s2, bs2;
        ccFifo_.prepareToRead(1, s1, bs1, s2, bs2);
        if (bs1 == 0) return false;
        out = ccQueue_[(size_t)s1];
        ccFifo_.finishedRead(bs1);
        return true;
    }

    /** Drain one ClipTrigger event.  Called on message thread.  Returns false if empty. */
    bool drainClipTrigger(ClipTriggerEvent& out) noexcept
    {
        int s1, bs1, s2, bs2;
        clipFifo_.prepareToRead(1, s1, bs1, s2, bs2);
        if (bs1 == 0) return false;
        out = clipQueue_[(size_t)s1];
        clipFifo_.finishedRead(bs1);
        return true;
    }

private:
    // 0xFF sentinel: "no per-note override, fall through to pad range / default"
    static constexpr uint8_t kNoOverride = 0xFF;

    // Per-note routing overrides [0..127]
    std::atomic<uint8_t> noteRouteType_    [128];
    std::atomic<int8_t>  noteTargetChannel_[128];
    std::atomic<uint8_t> padMapping_       [128];

    // Pad range: notes in [padRangeLow_, padRangeHigh_] are ClipTriggers.
    // Initialised to impossible range (128 > 0) = pad range disabled.
    std::atomic<uint8_t> padRangeLow_  { 128 };
    std::atomic<uint8_t> padRangeHigh_ { 0   };

    // Default instrument channel when no per-note override applies
    std::atomic<int> defaultInstrumentChannel_ { 0 };

    // Lock-free CC event queue (audio → message thread)
    CcEvent            ccQueue_[kQueueSize];
    juce::AbstractFifo ccFifo_ { kQueueSize };

    // Lock-free ClipTrigger event queue (audio → message thread)
    ClipTriggerEvent   clipQueue_[kQueueSize];
    juce::AbstractFifo clipFifo_ { kQueueSize };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiRouter)
};
