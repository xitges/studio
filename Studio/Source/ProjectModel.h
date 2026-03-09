/*
  ==============================================================================

    ProjectModel.h
    Created: 6 Mar 2026
    Author:  홍준영

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

enum class PlayMode    { Pattern, Song };
enum class ChannelType { Drum, Melodic };   // M3

// M3 — one note in a melodic channel's piano roll
struct NoteEvent
{
    int   pitch       = 60;    // MIDI note number (C4 = 60)
    float startBeat   = 0.0f;  // beat position within the pattern (0 = start)
    float lengthBeats = 0.25f; // duration in beats (0.25 = 16th note)
    float velocity    = 0.8f;  // 0.0 – 1.0
};

// M13 — synthesizer parameters per channel
struct SynthParams
{
    bool  enabled   = false;
    int   waveform  = 1;        // 0=Sine 1=Saw 2=Square 3=Triangle
    float attack    = 5.0f;     // ms
    float decay     = 80.0f;    // ms
    float sustain   = 0.6f;     // 0.0 – 1.0
    float release   = 200.0f;   // ms
    float cutoff    = 4000.0f;  // Hz
    float resonance = 0.3f;     // 0.0 – 1.0
    float lfoRate   = 2.0f;     // Hz
    float lfoDepth  = 0.0f;     // 0.0 – 1.0
    int   lfoTarget = 0;        // 0 = cutoff, 1 = pitch
};

// Launchpad — one pad cell in the 8×8 performance grid
struct LaunchpadPad
{
    juce::String filePath;   // empty = not assigned
    float        volume = 0.8f;
    float        pitch  = 0.0f;   // semitones offset
};

// M8 — VST/AU instrument plugin slot per channel
struct PluginSlot
{
    juce::String pluginId;           // PluginDescription::createIdentifierString()
    juce::String pluginStateBase64;  // getStateInformation() encoded as base64
    bool         enabled = false;
};

// M14 — FX chain parameters per mixer track
struct FXParams
{
    bool  compEnabled   = false;
    float compThreshDB  = -12.0f;
    float compRatio     = 4.0f;
    float compAttackMs  = 10.0f;
    float compReleaseMs = 100.0f;

    bool  delayEnabled  = false;
    float delayBeats    = 0.5f;     // beat multiplier (0.5 = 8th note)
    float delayFeedback = 0.3f;     // 0.0 – 1.0
    float delayMix      = 0.25f;    // wet mix 0.0 – 1.0

    bool  reverbEnabled = false;
    float reverbRoom    = 0.5f;     // 0.0 – 1.0
    float reverbDamp    = 0.5f;     // 0.0 – 1.0
    float reverbWet     = 0.25f;    // 0.0 – 1.0
    float reverbWidth   = 1.0f;     // 0.0 – 1.0
};

// M5 — one strip in the mixer
struct MixerTrack
{
    juce::String name   = "Track";
    float        volume = 1.0f;
    float        pan    = 0.0f;
    bool         muted  = false;
    bool         soloed = false;
};

// M11 — one row in the playlist timeline
struct PlaylistTrack
{
    juce::String name   = "Track";
    juce::Colour colour = juce::Colour(0xff3498db);
};

struct Pattern
{
    int          id         = 0;
    juce::String name       = "Pattern 1";
    int          lengthBars = 1;
    int          stepCount  = 16;

    static constexpr int kMaxChannels = 16;
    static constexpr int kMaxSteps    = 64;

    bool steps[kMaxChannels][kMaxSteps] {};

    // M3 — per-channel note lists (for Melodic channels)
    std::vector<NoteEvent> notes[kMaxChannels];

    // per-channel sample paths (each pattern has its own set)
    juce::String samplePaths[kMaxChannels];

    // per-pattern channel mixer values (independent per pattern)
    float channelVolume[kMaxChannels];
    float channelPan   [kMaxChannels];
    float channelPitch [kMaxChannels];

    Pattern()
    {
        std::fill_n(channelVolume, kMaxChannels, 0.8f);
        std::fill_n(channelPan,    kMaxChannels, 0.0f);
        std::fill_n(channelPitch,  kMaxChannels, 0.0f);
    }
};

// M9 — one breakpoint in an automation lane
struct AutomationPoint
{
    double beat  = 0.0;   // song beat position
    float  value = 0.0f;  // normalised 0..1
};

// M9 — one automation lane (one parameter, one list of breakpoints)
struct AutomationLane
{
    // paramId: "masterVolume", "bpm", "ch0vol", "ch1vol", … "ch15vol"
    juce::String            paramId;
    float                   minVal = 0.0f;
    float                   maxVal = 1.0f;
    std::vector<AutomationPoint> points;

    // Evaluate the lane at a given song beat using linear interpolation
    float evaluate(double beat) const
    {
        if (points.empty()) return minVal;
        if (beat <= points.front().beat) return minVal + points.front().value * (maxVal - minVal);
        if (beat >= points.back().beat)  return minVal + points.back().value  * (maxVal - minVal);

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const auto& a = points[i];
            const auto& b = points[i + 1];
            if (beat >= a.beat && beat <= b.beat)
            {
                const double t = (beat - a.beat) / (b.beat - a.beat);
                return minVal + (a.value + (float)t * (b.value - a.value)) * (maxVal - minVal);
            }
        }
        return minVal;
    }
};

struct PlaylistClip
{
    int          id         = 0;
    int          patternId  = 0;
    int          trackIndex = 0;
    float        startBar   = 0.0f;
    float        lengthBars = 1.0f;

    juce::String name;
};

struct Project
{
    double bpm = 140.0;

    // channel rack state (shared across patterns — count and names)
    int          channelCount = 3;
    juce::String channelNames[16];

    std::vector<Pattern>      patterns;
    std::vector<PlaylistClip>  playlistClips;
    std::vector<PlaylistTrack> playlistTracks;   // M11 — dynamic track rows

    // M3 — channel types (global, not per-pattern)
    std::array<ChannelType, 16> channelTypes        = {};   // all Drum by default

    // M5 — mixer
    std::array<int, 16>       channelMixerRouting  = {};   // channel → mixer track (0-7)
    std::vector<MixerTrack>   mixerTracks;                  // 8 tracks initialised in MainComponent
    float                     masterVolume         = 1.0f;
    float                     masterPan            = 0.0f;

    // M13 — per-channel synth params
    std::array<SynthParams, 16> synthParams = {};

    // M14 — per-mixer-track FX params
    std::array<FXParams, 8> fxParams = {};

    // Launchpad — 8×8 pad assignments
    std::array<LaunchpadPad, 64> launchpadPads = {};

    // M9 — automation lanes
    std::vector<AutomationLane> automationLanes;

    // M8 — VST/AU instrument plugins (one slot per channel)
    std::array<PluginSlot, 16> channelInstrumentPlugins = {};
};
