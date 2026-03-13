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
enum class ChannelType { Drum, Melodic };
inline constexpr int kMaxPatternSteps = 128;

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

// Instrument preset database — factory presets for SynthEditorPanel ComboBox
namespace SynthPresets
{
    struct Preset { juce::String name; SynthParams params; };

    // Helper: build a SynthParams inline
    inline SynthParams make (bool en, int wv,
                             float atk, float dec, float sus, float rel,
                             float co,  float res,
                             float lfoR, float lfoD, int lfoT = 0)
    {
        SynthParams p;
        p.enabled   = en;   p.waveform  = wv;
        p.attack    = atk;  p.decay     = dec;
        p.sustain   = sus;  p.release   = rel;
        p.cutoff    = co;   p.resonance = res;
        p.lfoRate   = lfoR; p.lfoDepth  = lfoD;
        p.lfoTarget = lfoT;
        return p;
    }

    // Waveform codes: 0=Sine 1=Saw 2=Square 3=Triangle
    inline std::vector<Preset> getAll()
    {
        return {
            //  name              en    wv  atk    dec    sus    rel    cut    res   lfoR  lfoD  lfoT
            { "Piano",          make(true, 3,   5, 400, 0.20f,  450, 3000, 0.15f, 0.00f, 0.00f) },
            { "Electric Guitar",make(true, 1,   2, 250, 0.45f,  200, 4800, 0.25f, 4.00f, 0.03f) },
            { "Cello / Pad",    make(true, 1, 350, 300, 0.85f, 1500, 1600, 0.35f, 0.50f, 0.10f) },
            { "Fat Bass",       make(true, 1,   3, 150, 0.80f,  250,  700, 0.70f, 0.10f, 0.02f) },
            { "Retro Game",     make(true, 2,   1,  50, 0.00f,   40, 8000, 0.10f, 6.00f, 0.05f) },
            { "Lead Synth",     make(true, 1,  10, 120, 0.70f,  350, 5500, 0.60f, 4.50f, 0.12f) },
            { "Warm Pad",       make(true, 0, 500, 400, 0.90f, 1800, 1000, 0.15f, 0.30f, 0.08f) },
            { "Pluck",          make(true, 3,   1, 200, 0.00f,  300, 4500, 0.45f, 0.00f, 0.00f) },
            { "Trumpet",        make(true, 1,  35, 150, 0.85f,  250, 3800, 0.40f, 5.00f, 0.08f, 1) },
        };
    }
} // namespace SynthPresets

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
    static constexpr int kMaxSteps    = kMaxPatternSteps;

    bool steps[kMaxChannels][kMaxSteps] {};

    // M3 — per-channel note lists (for Melodic channels)
    std::vector<NoteEvent> notes[kMaxChannels];

    // per-channel sample paths (each pattern has its own set)
    juce::String samplePaths[kMaxChannels];

    // per-pattern channel mixer values (independent per pattern)
    float channelVolume[kMaxChannels];
    float channelPan   [kMaxChannels];
    float channelPitch [kMaxChannels];

    // per-pattern channel identity & instrument settings
    ChannelType  channelTypes  [kMaxChannels] = {};     // all Drum by default
    juce::String channelNames  [kMaxChannels];
    SynthParams  synthParams   [kMaxChannels] = {};

    // per-pattern mixer routing: channel → mixer track (0-7)
    int channelMixerRouting[kMaxChannels] = {};

    Pattern()
    {
        std::fill_n(channelVolume, kMaxChannels, 0.8f);
        std::fill_n(channelPan,    kMaxChannels, 0.0f);
        std::fill_n(channelPitch,  kMaxChannels, 0.0f);
        for (int i = 0; i < kMaxChannels; ++i)
        {
            channelNames[i]        = "Channel " + juce::String(i + 1);
            channelMixerRouting[i] = i % 8;
        }
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

    // channel rack state (global: only count is global; names/types live in Pattern)
    int channelCount = 3;

    std::vector<Pattern>      patterns;
    std::vector<PlaylistClip>  playlistClips;
    std::vector<PlaylistTrack> playlistTracks;   // M11 — dynamic track rows

    // M5 — mixer
    std::vector<MixerTrack>   mixerTracks;                  // 8 tracks initialised in MainComponent
    float                     masterVolume         = 1.0f;
    float                     masterPan            = 0.0f;

    // M14 — per-mixer-track FX params
    std::array<FXParams, 8> fxParams = {};

    // Launchpad — 8×8 pad assignments
    std::array<LaunchpadPad, 64> launchpadPads = {};

    // M9 — automation lanes
    std::vector<AutomationLane> automationLanes;

    // M8 — VST/AU instrument plugins (one slot per channel)
    std::array<PluginSlot, 16> channelInstrumentPlugins = {};
};
