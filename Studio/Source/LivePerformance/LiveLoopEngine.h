#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <cmath>
#include <functional>

/**
 * LiveLoopEngine
 *
 * Independent per-channel MIDI loop recorder + player.
 * Completely decoupled from the step-sequencer / Pattern system.
 *
 * Flow per channel:
 *   Idle → [arm()] → Armed → [first NoteOn] → WaitingForBar
 *        → [bar boundary] → Recording → [loopLength elapsed] → Looping
 *        → (overdub: new notes added while looping)
 *        → [stop()] → Idle
 *
 * Thread safety:
 *   Message thread:  arm(), stop(), setLoopLength(), resetAll(), getState()
 *   Audio thread:    processMidiEvent(), processBlock()
 *   Cross-thread:    atomic arm/stop requests, atomic state reads
 */
class LiveLoopEngine
{
public:
    static constexpr int kMaxChannels = 16;
    static constexpr int kMaxNotes    = 2048;   // per channel

    enum class State : uint8_t
    {
        Idle,
        Armed,          // waiting for first NoteOn
        WaitingForBar,  // first NoteOn seen, waiting for next bar boundary
        Recording,      // capturing notes
        Looping         // playback + overdub
    };

    // ── Message-thread API ────────────────────────────────────────────────────

    void arm(int ch) noexcept
    {
        if (ch < 0 || ch >= kMaxChannels) return;
        stopReq_[ch].store(false, std::memory_order_release);
        armReq_ [ch].store(true,  std::memory_order_release);
    }

    void stop(int ch) noexcept
    {
        if (ch < 0 || ch >= kMaxChannels) return;
        stopReq_[ch].store(true, std::memory_order_release);
    }

    void resetAll() noexcept
    {
        for (int ch = 0; ch < kMaxChannels; ++ch)
            stopReq_[ch].store(true, std::memory_order_release);
    }

    void setLoopLengthBeats(double beats) noexcept
    {
        pendingLoopLength_.store(juce::jmax(1.0, beats), std::memory_order_release);
    }

    /** Set input quantization step in beats (0 = free, 0.25 = 1/16, 0.5 = 1/8, 1.0 = 1/4). */
    void setQuantize(double stepBeats) noexcept
    {
        quantizeStep_.store(stepBeats, std::memory_order_release);
    }

    double getQuantize() const noexcept
    {
        return quantizeStep_.load(std::memory_order_relaxed);
    }

    State getState(int ch) const noexcept
    {
        if (ch < 0 || ch >= kMaxChannels) return State::Idle;
        return static_cast<State>(state_[ch].load(std::memory_order_relaxed));
    }

    double getLoopLengthBeats() const noexcept
    {
        return pendingLoopLength_.load(std::memory_order_relaxed);
    }

    /** UI-thread safe (minor read race accepted for display only). */
    struct NoteDisplayItem { int pitch; float velocity; double startBeat, endBeat; };

    int getNotesForDisplay(int ch, NoteDisplayItem* out, int maxItems) const noexcept
    {
        if (ch < 0 || ch >= kMaxChannels || !out || maxItems <= 0) return 0;
        const auto& cd = ch_[ch];
        const int n = std::min(cd.noteCount, maxItems);
        for (int i = 0; i < n; ++i)
            out[i] = { cd.notes[i].pitch, cd.notes[i].velocity,
                       cd.notes[i].startBeat, cd.notes[i].endBeat };
        return n;
    }

    double getLoopPhase(int ch) const noexcept
    {
        if (ch < 0 || ch >= kMaxChannels) return 0.0;
        return ch_[ch].loopPhase;
    }

    double getChannelLoopLength(int ch) const noexcept
    {
        if (ch < 0 || ch >= kMaxChannels) return 16.0;
        return ch_[ch].loopLength;
    }

    // ── Audio-thread callbacks (set once before first processBlock) ───────────
    // AudioEngine fills these to route playback notes into the synth/plugin chain.
    std::function<void(int ch, int pitch, float velocity)> onNoteOn;
    std::function<void(int ch, int pitch)>                 onNoteOff;

    // ── Audio-thread API ──────────────────────────────────────────────────────

    /** Call once per processBlock. Advances clocks and fires playback notes. */
    void processBlock(int numSamples, double bpm, double sampleRate) noexcept;

    /** Call for each incoming Note-On/Note-Off (already routed to ch). */
    void processMidiEvent(const juce::MidiMessage& msg, int ch) noexcept;

    /** Reset global beat counter (call on transport stop/start). */
    void resetGlobalBeat() noexcept { globalBeat_ = 0.0; }

private:
    // ── Per-note data ─────────────────────────────────────────────────────────
    struct RecordedNote
    {
        int    pitch     = 0;
        float  velocity  = 0.0f;
        double startBeat = 0.0;  // relative to loop start (0 .. loopLength)
        double endBeat   = 0.0;  // relative to loop start
        bool   valid     = false;
    };

    struct HeldNote
    {
        bool   active    = false;
        double startBeat = 0.0;
        float  velocity  = 0.0f;
    };

    // ── Per-channel state (audio-thread owned) ────────────────────────────────
    struct Channel
    {
        double loopLength      = 16.0;
        double loopPhase       = 0.0;   // position within loop (0 .. loopLength)
        double recordStartBeat = 0.0;   // global beat when recording began

        RecordedNote notes[kMaxNotes];
        int          noteCount = 0;

        HeldNote held[128];

        void reset() noexcept
        {
            loopPhase  = 0.0;
            noteCount  = 0;
            for (auto& h : held)  h = {};
        }
    };

    Channel ch_[kMaxChannels];

    // ── Cross-thread atomics ──────────────────────────────────────────────────
    std::atomic<uint8_t> state_  [kMaxChannels] {};
    std::atomic<bool>    armReq_ [kMaxChannels] {};
    std::atomic<bool>    stopReq_[kMaxChannels] {};
    std::atomic<double>  pendingLoopLength_ { 16.0 };
    std::atomic<double>  quantizeStep_      {  0.0 };  // 0 = free, >0 = snap to grid

    double globalBeat_ = 0.0;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void firePlayback(int ch, double prevPhase, double nextPhase) noexcept;
    void finalizeHeldNotes(int ch) noexcept;
};

// ─────────────────────────────────────────────────────────────────────────────
// Inline implementation (header-only for simplicity)
// ─────────────────────────────────────────────────────────────────────────────

inline void LiveLoopEngine::processBlock(int numSamples, double bpm, double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || bpm <= 0.0) return;

    const double beatsThisBlock = numSamples * bpm / (60.0 * sampleRate);
    globalBeat_ += beatsThisBlock;

    for (int c = 0; c < kMaxChannels; ++c)
    {
        // ── Handle message-thread requests ────────────────────────────────
        if (stopReq_[c].exchange(false, std::memory_order_acq_rel))
        {
            // Send note-offs for any held notes
            finalizeHeldNotes(c);
            // All active playback notes off
            if (onNoteOff)
                for (int p = 0; p < 128; ++p)
                    if (ch_[c].held[p].active) onNoteOff(c, p);
            ch_[c].reset();
            state_[c].store((uint8_t)State::Idle, std::memory_order_release);
            armReq_[c].store(false, std::memory_order_release);
        }
        if (armReq_[c].exchange(false, std::memory_order_acq_rel))
        {
            ch_[c].reset();
            state_[c].store((uint8_t)State::Armed, std::memory_order_release);
        }

        const auto st = static_cast<State>(state_[c].load(std::memory_order_relaxed));

        // ── WaitingForBar: check if bar boundary passed ───────────────────
        if (st == State::WaitingForBar)
        {
            if (globalBeat_ >= ch_[c].recordStartBeat)
            {
                ch_[c].loopPhase = 0.0;
                state_[c].store((uint8_t)State::Recording, std::memory_order_release);
            }
        }

        // ── Recording: check if loop length elapsed ───────────────────────
        else if (st == State::Recording)
        {
            const double elapsed = globalBeat_ - ch_[c].recordStartBeat;
            if (elapsed >= ch_[c].loopLength)
            {
                finalizeHeldNotes(c);
                ch_[c].loopPhase = 0.0;  // always restart from beat 0
                state_[c].store((uint8_t)State::Looping, std::memory_order_release);
            }
        }

        // ── Looping: advance phase and fire playback notes ────────────────
        else if (st == State::Looping)
        {
            auto& cd = ch_[c];
            const double prevPhase = cd.loopPhase;
            cd.loopPhase += beatsThisBlock;

            if (cd.loopPhase >= cd.loopLength)
            {
                // Fire tail of current cycle
                firePlayback(c, prevPhase, cd.loopLength);
                cd.loopPhase = std::fmod(cd.loopPhase, cd.loopLength);
                // Fire head of next cycle
                firePlayback(c, 0.0, cd.loopPhase);
            }
            else
            {
                firePlayback(c, prevPhase, cd.loopPhase);
            }
        }
    }
}

inline void LiveLoopEngine::processMidiEvent(const juce::MidiMessage& msg, int ch) noexcept
{
    if (ch < 0 || ch >= kMaxChannels) return;
    if (!msg.isNoteOn() && !msg.isNoteOff()) return;

    const double globalBeat = globalBeat_;  // use internal monotonic counter
    const int   pitch = juce::jlimit(0, 127, msg.getNoteNumber());
    const float vel   = msg.getFloatVelocity();
    auto& cd  = ch_[ch];
    const auto st = static_cast<State>(state_[ch].load(std::memory_order_relaxed));

    // ── Armed: first NoteOn → schedule start at next bar ─────────────────
    if (st == State::Armed && msg.isNoteOn())
    {
        cd.loopLength      = pendingLoopLength_.load(std::memory_order_relaxed);
        cd.recordStartBeat = (std::floor(globalBeat / 4.0) + 1.0) * 4.0;
        cd.noteCount       = 0;
        cd.loopPhase       = 0.0;
        for (auto& h : cd.held) h = {};
        state_[ch].store((uint8_t)State::WaitingForBar, std::memory_order_release);
        return;
    }

    // ── WaitingForBar: if bar already passed, transition immediately ──────
    if (st == State::WaitingForBar && globalBeat >= cd.recordStartBeat)
    {
        cd.loopPhase = 0.0;
        state_[ch].store((uint8_t)State::Recording, std::memory_order_release);
    }

    // Re-read state after potential transition
    const auto stNow = static_cast<State>(state_[ch].load(std::memory_order_relaxed));
    if (stNow != State::Recording && stNow != State::Looping) return;

    // ── Compute beat position relative to loop start ──────────────────────
    double relBeat;
    if (stNow == State::Recording)
        relBeat = std::fmax(0.0, globalBeat - cd.recordStartBeat);
    else
        relBeat = cd.loopPhase;  // current playhead position in loop

    relBeat = std::fmod(relBeat, cd.loopLength);
    if (relBeat < 0.0) relBeat += cd.loopLength;

    // ── Input quantization (snap NoteOn attack to grid) ───────────────────
    if (msg.isNoteOn())
    {
        const double qStep = quantizeStep_.load(std::memory_order_relaxed);
        if (qStep > 0.0)
        {
            relBeat = std::round(relBeat / qStep) * qStep;
            relBeat = std::fmod(relBeat, cd.loopLength);
            if (relBeat < 0.0) relBeat += cd.loopLength;
        }
    }

    if (msg.isNoteOn())
    {
        cd.held[pitch] = { true, relBeat, vel };
    }
    else if (msg.isNoteOff())
    {
        auto& hn = cd.held[pitch];
        if (!hn.active) return;

        const double qStep = quantizeStep_.load(std::memory_order_relaxed);

        // Snap NoteOff position to grid as well (cleaner end point)
        double relBeatSnapped = relBeat;
        if (qStep > 0.0)
        {
            relBeatSnapped = std::round(relBeat / qStep) * qStep;
            relBeatSnapped = std::fmod(relBeatSnapped, cd.loopLength);
            if (relBeatSnapped < 0.0) relBeatSnapped += cd.loopLength;
        }

        double len = relBeatSnapped - hn.startBeat;
        // Only add loopLength for true wrap-around (negative); len==0 means same-step
        // snap and should be handled by the minimum-step enforcement below, NOT wrap.
        if (len < 0.0) len += cd.loopLength;

        // Enforce minimum 1 step (or 1/16 beat if free)
        if (qStep > 0.0)
        {
            len = std::round(len / qStep) * qStep;
            if (len < qStep) len = qStep;
        }
        else
        {
            len = std::fmax(0.0625, len);  // free mode: minimum 1/16 beat
        }

        if (cd.noteCount < kMaxNotes)
        {
            auto& note    = cd.notes[cd.noteCount++];
            note.pitch     = pitch;
            note.velocity  = hn.velocity;
            note.startBeat = hn.startBeat;
            note.endBeat   = std::fmod(hn.startBeat + len, cd.loopLength);
            note.valid     = true;
        }
        hn.active = false;
    }
}

inline void LiveLoopEngine::firePlayback(int ch, double prevPhase, double nextPhase) noexcept
{
    if (prevPhase >= nextPhase) return;
    auto& cd = ch_[ch];

    for (int n = 0; n < cd.noteCount; ++n)
    {
        const auto& note = cd.notes[n];
        if (!note.valid) continue;

        if (note.startBeat >= prevPhase && note.startBeat < nextPhase)
            if (onNoteOn) onNoteOn(ch, note.pitch, note.velocity);

        if (note.endBeat >= prevPhase && note.endBeat < nextPhase)
            if (onNoteOff) onNoteOff(ch, note.pitch);
    }
}

inline void LiveLoopEngine::finalizeHeldNotes(int ch) noexcept
{
    auto& cd = ch_[ch];
    for (int p = 0; p < 128; ++p)
    {
        auto& hn = cd.held[p];
        if (!hn.active) continue;

        double endBeat = (cd.noteCount > 0)
            ? std::fmod(hn.startBeat + cd.loopLength * 0.5, cd.loopLength)
            : cd.loopLength;  // fallback: end of loop

        if (cd.noteCount < kMaxNotes)
        {
            auto& note    = cd.notes[cd.noteCount++];
            note.pitch     = p;
            note.velocity  = hn.velocity;
            note.startBeat = hn.startBeat;
            note.endBeat   = endBeat;
            note.valid     = true;
        }
        hn.active = false;
        if (onNoteOff) onNoteOff(ch, p);
    }
}
