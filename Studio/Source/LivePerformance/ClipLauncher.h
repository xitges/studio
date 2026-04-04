/*
  ==============================================================================

    ClipLauncher.h
    Created: 2026-04-04
    Author:  홍준영

    Minimal channel-based clip launch system for Live Performance Mode.

    Design:
      - Each of the 16 DAW channels owns one PerformanceClip slot.
      - Pressing a pad → queues that channel's clip (Idle → Queued).
      - On the next quantize boundary (1 bar = 4 beats) the queued clips
        become Playing and the audio engine is updated.
      - All state mutations happen on the MESSAGE thread only.
      - The audio thread is unaffected by this class.

    Thread model:
      Message thread: triggerClip, processQuantizedLaunch, setClip*
      Audio thread  : (none — ClipLauncher owns no audio-thread state)

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <functional>
#include <array>
#include <cstdint>

// ---------------------------------------------------------------------------
// ClipState
// ---------------------------------------------------------------------------

enum class ClipState
{
    Idle,     ///< Not active, not queued
    Queued,   ///< Waiting for the next quantize boundary to become Playing
    Playing,  ///< Currently driving the audio engine for this channel
    Stopping, ///< Will become Idle at the next quantize boundary
};

// ---------------------------------------------------------------------------
// PerformanceClip
// ---------------------------------------------------------------------------

struct PerformanceClip
{
    int        channel      = -1;  ///< Target DAW channel (0-15); -1 = unassigned
    int        patternId    = -1;  ///< Which Pattern to play
    int        variationIdx = 0;   ///< Which variation (0-3 = A/B/C/D)
    ClipState  state        = ClipState::Idle;
};

// ---------------------------------------------------------------------------
// ClipLauncher
// ---------------------------------------------------------------------------

class ClipLauncher
{
public:
    static constexpr int kMaxPads     = 64;
    static constexpr int kMaxChannels = 16;

    // -------------------------------------------------------------------------
    // Callbacks (wire from MainComponent)
    // -------------------------------------------------------------------------

    /** Called when a clip becomes Playing.
        channel, patternId, variationIdx — what to activate in AudioEngine. */
    std::function<void(int channel, int patternId, int variationIdx)> onClipLaunched;

    /** Called when a clip becomes Idle (was stopped or replaced). */
    std::function<void(int channel)> onClipStopped;

    /** Called when a clip enters the Queued or Stopping state (UI visual hint). */
    std::function<void(int channel)> onClipQueued;

    // -------------------------------------------------------------------------
    // Configuration API (message thread, before or during playback)
    // -------------------------------------------------------------------------

    /** Assign a clip to a pad index.
        padIndex: 0-63
        channel : 0-15
        Call before triggerClip. */
    void setClip(int padIndex, int channel, int patternId, int variationIdx)
    {
        if (padIndex < 0 || padIndex >= kMaxPads) return;
        auto& clip        = padClips_[(size_t)padIndex];
        clip.channel      = juce::jlimit(0, kMaxChannels - 1, channel);
        clip.patternId    = patternId;
        clip.variationIdx = juce::jlimit(0, 3, variationIdx);
        clip.state        = ClipState::Idle;
    }

    /** Shorthand: pad N → channel N % 16, variation 0. */
    void setClipDefault(int padIndex, int patternId)
    {
        setClip(padIndex, padIndex % kMaxChannels, patternId, 0);
    }

    /** Set quantize grid in beats.  Default = 4.0 (1 bar at 4/4).
        Must be > 0. */
    void setQuantizeBeats(double beats) { quantizeBeats_ = juce::jmax(0.25, beats); }
    double getQuantizeBeats() const     { return quantizeBeats_; }

    // -------------------------------------------------------------------------
    // Trigger API (message thread)
    // -------------------------------------------------------------------------

    /** Called when a pad event arrives (from draining ClipTriggerEvent queue).
        velocity > 0 = press, velocity == 0 = release (ignored for now). */
    void triggerClip(int padIndex, uint8_t velocity)
    {
        if (padIndex < 0 || padIndex >= kMaxPads) return;
        auto& clip = padClips_[(size_t)padIndex];
        if (clip.channel < 0 || clip.patternId < 0) return;  // unassigned pad

        if (velocity == 0) return;  // note-off: ignore (gate mode not needed yet)

        const int ch = clip.channel;

        switch (clip.state)
        {
            case ClipState::Playing:
                // Second press → queue a stop
                clip.state = ClipState::Stopping;
                if (onClipQueued) onClipQueued(ch);
                break;

            case ClipState::Queued:
                // Press again while queued → cancel queue
                clip.state = ClipState::Idle;
                break;

            case ClipState::Stopping:
                // Press while stopping → re-queue to play again
                clip.state = ClipState::Queued;
                if (onClipQueued) onClipQueued(ch);
                break;

            case ClipState::Idle:
            default:
                // Cancel any other queued clip on the same channel first
                for (int p = 0; p < kMaxPads; ++p)
                    if (p != padIndex
                        && padClips_[(size_t)p].channel == ch
                        && padClips_[(size_t)p].state == ClipState::Queued)
                        padClips_[(size_t)p].state = ClipState::Idle;

                clip.state = ClipState::Queued;
                if (onClipQueued) onClipQueued(ch);
                break;
        }
    }

    // -------------------------------------------------------------------------
    // Quantize boundary processing (message thread, called from timerCallback)
    // -------------------------------------------------------------------------

    /** Call every timer tick with the current beat position from AudioEngine.
        Detects bar-boundary crossings; launches all Queued clips atomically. */
    void processQuantizedLaunch(double currentBeat)
    {
        if (currentBeat < 0.0) return;  // transport stopped

        const int currentBar = (int)(currentBeat / quantizeBeats_);
        if (currentBar != lastBar_)
        {
            lastBar_ = currentBar;
            launchAllQueued();
        }
    }

    // -------------------------------------------------------------------------
    // Query (message thread / UI)
    // -------------------------------------------------------------------------

    ClipState getClipState(int padIndex) const
    {
        if (padIndex < 0 || padIndex >= kMaxPads) return ClipState::Idle;
        return padClips_[(size_t)padIndex].state;
    }

    /** Returns the padIndex of the Playing clip on a channel, or -1. */
    int getPlayingPadForChannel(int channel) const
    {
        for (int p = 0; p < kMaxPads; ++p)
            if (padClips_[(size_t)p].channel == channel
                && padClips_[(size_t)p].state == ClipState::Playing)
                return p;
        return -1;
    }

    /** Force-stop all clips across all channels (call on transport stop). */
    void stopAll()
    {
        for (int p = 0; p < kMaxPads; ++p)
        {
            const int ch = padClips_[(size_t)p].channel;
            padClips_[(size_t)p].state = ClipState::Idle;
            if (ch >= 0 && onClipStopped) onClipStopped(ch);
        }
    }

    /** Reset bar counter so the next call to processQuantizedLaunch
        immediately detects a boundary (use on transport start). */
    void resetBarCounter() { lastBar_ = -1; }

private:
    // -------------------------------------------------------------------------

    void launchAllQueued()
    {
        for (int p = 0; p < kMaxPads; ++p)
        {
            auto& clip = padClips_[(size_t)p];

            if (clip.state == ClipState::Queued)
            {
                // Stop the currently Playing clip on the same channel
                for (int other = 0; other < kMaxPads; ++other)
                {
                    if (other != p
                        && padClips_[(size_t)other].channel == clip.channel
                        && padClips_[(size_t)other].state   == ClipState::Playing)
                    {
                        padClips_[(size_t)other].state = ClipState::Idle;
                        if (onClipStopped) onClipStopped(clip.channel);
                    }
                }

                clip.state = ClipState::Playing;
                if (onClipLaunched)
                    onClipLaunched(clip.channel, clip.patternId, clip.variationIdx);
            }
            else if (clip.state == ClipState::Stopping)
            {
                clip.state = ClipState::Idle;
                if (onClipStopped) onClipStopped(clip.channel);
            }
        }
    }

    // -------------------------------------------------------------------------
    std::array<PerformanceClip, kMaxPads> padClips_ {};
    double quantizeBeats_ = 4.0;  // 1 bar in 4/4 time
    int    lastBar_       = -1;   // -1 forces boundary detection on first call
};
