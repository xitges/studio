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
enum class ScaleType   { Major, Minor };
inline constexpr int kMaxPatternSteps = 128;

struct KeySignature
{
    int tonic = 0; // 0=C, 1=C#/Db, ... 11=B
    ScaleType scale = ScaleType::Major;
};

namespace MusicTheory
{
    inline const std::array<int, 7>& getScaleIntervals(ScaleType scale)
    {
        static const std::array<int, 7> major { 0, 2, 4, 5, 7, 9, 11 };
        static const std::array<int, 7> minor { 0, 2, 3, 5, 7, 8, 10 };
        return scale == ScaleType::Minor ? minor : major;
    }

    inline bool isPitchInScale(int midiPitch, const KeySignature& key)
    {
        const int pc = ((midiPitch % 12) + 12) % 12;
        const int rel = (pc - key.tonic + 12) % 12;
        for (const auto interval : getScaleIntervals(key.scale))
            if (interval == rel)
                return true;
        return false;
    }

    inline int snapPitchToScale(int midiPitch, const KeySignature& key)
    {
        if (isPitchInScale(midiPitch, key))
            return midiPitch;

        for (int delta = 1; delta < 12; ++delta)
        {
            const int down = juce::jmax(0, midiPitch - delta);
            if (isPitchInScale(down, key))
                return down;

            const int up = juce::jmin(127, midiPitch + delta);
            if (isPitchInScale(up, key))
                return up;
        }

        return juce::jlimit(0, 127, midiPitch);
    }

    inline int moveScaleStep(int midiPitch, const KeySignature& key, int direction)
    {
        const int step = direction >= 0 ? 1 : -1;
        int pitch = juce::jlimit(0, 127, midiPitch + step);
        while (pitch > 0 && pitch < 127)
        {
            if (isPitchInScale(pitch, key))
                return pitch;
            pitch += step;
        }
        return juce::jlimit(0, 127, pitch);
    }

    inline bool preferFlats(const KeySignature& key)
    {
        if (key.scale == ScaleType::Major)
            return key.tonic == 5 || key.tonic == 10 || key.tonic == 3 || key.tonic == 8 || key.tonic == 1;
        return key.tonic == 2 || key.tonic == 7 || key.tonic == 0 || key.tonic == 5 || key.tonic == 10;
    }

    inline juce::String noteNameForPitch(int midiPitch, const KeySignature& key)
    {
        static const std::array<const char*, 12> sharpNames
            { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const std::array<const char*, 12> flatNames
            { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

        const int pc = ((midiPitch % 12) + 12) % 12;
        return preferFlats(key) ? flatNames[(size_t)pc] : sharpNames[(size_t)pc];
    }

    inline juce::String scaleName(ScaleType scale)
    {
        return scale == ScaleType::Minor ? "Minor" : "Major";
    }

    inline juce::String tonicName(const KeySignature& key)
    {
        return noteNameForPitch(60 + key.tonic, key);
    }

    inline juce::String diatonicTriadSuffix(ScaleType scale, int degree)
    {
        static const std::array<const char*, 7> major
            { "", "m", "m", "", "", "m", "dim" };
        static const std::array<const char*, 7> minor
            { "m", "dim", "", "m", "m", "", "" };

        const auto& suffixes = scale == ScaleType::Minor ? minor : major;
        return suffixes[(size_t)juce::jlimit(0, 6, degree)];
    }

    struct ProgressionStep
    {
        int degree = 0;      // 0 = I/i, 1 = II/ii, ... 6 = VII/vii
        int chordTones = 3;  // triad by default, 4 = seventh chord
    };

    struct ChordProgressionPreset
    {
        juce::String category;
        juce::String name;
        juce::String roman;
        ScaleType    scale = ScaleType::Major;
        std::vector<ProgressionStep> steps;
    };

    inline ChordProgressionPreset makeChordProgressionPreset(const char* category,
                                                             const char* name,
                                                             const char* roman,
                                                             ScaleType scale,
                                                             std::initializer_list<ProgressionStep> steps)
    {
        ChordProgressionPreset preset;
        preset.category = category;
        preset.name = name;
        preset.roman = roman;
        preset.scale = scale;
        preset.steps.assign(steps.begin(), steps.end());
        return preset;
    }

    inline juce::String chordSymbolForDegree(const KeySignature& key, int degree)
    {
        const auto& scaleIntervals = getScaleIntervals(key.scale);
        const int safeDegree = juce::jlimit(0, 6, degree);
        const int chordPitch = 60 + key.tonic + scaleIntervals[(size_t)safeDegree];
        return noteNameForPitch(chordPitch, key) + diatonicTriadSuffix(key.scale, safeDegree);
    }

    inline juce::String chordSymbolsForProgression(const KeySignature& key, const ChordProgressionPreset& preset)
    {
        juce::StringArray names;
        for (const auto& step : preset.steps)
            names.add(chordSymbolForDegree(key, step.degree));
        return names.joinIntoString("-");
    }

    inline const std::vector<ChordProgressionPreset>& getChordProgressionPresets(ScaleType scale)
    {
        static const std::vector<ChordProgressionPreset> majorPresets
        {
            makeChordProgressionPreset("Pop", "Axis Pop", "I-V-vi-IV", ScaleType::Major,
                                       { { 0, 3 }, { 4, 3 }, { 5, 3 }, { 3, 3 } }),
            makeChordProgressionPreset("Pop", "50s Pop", "I-vi-IV-V", ScaleType::Major,
                                       { { 0, 3 }, { 5, 3 }, { 3, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Pop", "Sensitive Pop", "vi-IV-I-V", ScaleType::Major,
                                       { { 5, 3 }, { 3, 3 }, { 0, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Pop", "Lifted Chorus", "I-IV-vi-V", ScaleType::Major,
                                       { { 0, 3 }, { 3, 3 }, { 5, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Pop", "Big Hook", "IV-I-V-vi", ScaleType::Major,
                                       { { 3, 3 }, { 0, 3 }, { 4, 3 }, { 5, 3 } }),
            makeChordProgressionPreset("Pop", "Radio Climb", "I-iii-vi-IV", ScaleType::Major,
                                       { { 0, 3 }, { 2, 3 }, { 5, 3 }, { 3, 3 } }),
            makeChordProgressionPreset("Cadence", "Soul Turnaround", "I-vi-ii-V", ScaleType::Major,
                                       { { 0, 3 }, { 5, 3 }, { 1, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Cadence", "Jazz Cadence", "ii-V-I", ScaleType::Major,
                                       { { 1, 4 }, { 4, 4 }, { 0, 4 } }),
            makeChordProgressionPreset("Cadence", "Circle Resolve", "vi-ii-V-I", ScaleType::Major,
                                       { { 5, 4 }, { 1, 4 }, { 4, 4 }, { 0, 4 } }),
            makeChordProgressionPreset("Cadence", "Bright Walk", "I-ii-IV-V", ScaleType::Major,
                                       { { 0, 3 }, { 1, 3 }, { 3, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Cadence", "Folk Loop", "I-IV-I-V", ScaleType::Major,
                                       { { 0, 3 }, { 3, 3 }, { 0, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Cadence", "Classic Verse", "I-V-IV-V", ScaleType::Major,
                                       { { 0, 3 }, { 4, 3 }, { 3, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Long Form", "Canon", "I-V-vi-iii-IV-I-IV-V", ScaleType::Major,
                                       { { 0, 3 }, { 4, 3 }, { 5, 3 }, { 2, 3 }, { 3, 3 }, { 0, 3 }, { 3, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Long Form", "Climbing Story", "I-iii-IV-I-ii-V-I", ScaleType::Major,
                                       { { 0, 3 }, { 2, 3 }, { 3, 3 }, { 0, 3 }, { 1, 3 }, { 4, 3 }, { 0, 3 } }),
            makeChordProgressionPreset("Long Form", "Neo Soul", "ii-V-I-vi", ScaleType::Major,
                                       { { 1, 4 }, { 4, 4 }, { 0, 4 }, { 5, 4 } })
        };

        static const std::vector<ChordProgressionPreset> minorPresets
        {
            makeChordProgressionPreset("Dark", "Dark Pop", "i-VI-III-VII", ScaleType::Minor,
                                       { { 0, 3 }, { 5, 3 }, { 2, 3 }, { 6, 3 } }),
            makeChordProgressionPreset("Dark", "Minor Rise", "i-VII-VI-VII", ScaleType::Minor,
                                       { { 0, 3 }, { 6, 3 }, { 5, 3 }, { 6, 3 } }),
            makeChordProgressionPreset("Dark", "Cinematic", "i-iv-VI-VII", ScaleType::Minor,
                                       { { 0, 3 }, { 3, 3 }, { 5, 3 }, { 6, 3 } }),
            makeChordProgressionPreset("Dark", "Trap Loop", "i-VI-iv-VII", ScaleType::Minor,
                                       { { 0, 3 }, { 5, 3 }, { 3, 3 }, { 6, 3 } }),
            makeChordProgressionPreset("Dark", "Midnight", "i-III-VII-VI", ScaleType::Minor,
                                       { { 0, 3 }, { 2, 3 }, { 6, 3 }, { 5, 3 } }),
            makeChordProgressionPreset("Dark", "Shadow Story", "i-VI-III-VII-i", ScaleType::Minor,
                                       { { 0, 3 }, { 5, 3 }, { 2, 3 }, { 6, 3 }, { 0, 3 } }),
            makeChordProgressionPreset("Emotional", "Sad Hook", "VI-III-VII-i", ScaleType::Minor,
                                       { { 5, 3 }, { 2, 3 }, { 6, 3 }, { 0, 3 } }),
            makeChordProgressionPreset("Emotional", "Minor Turn", "i-iv-v-i", ScaleType::Minor,
                                       { { 0, 3 }, { 3, 3 }, { 4, 3 }, { 0, 3 } }),
            makeChordProgressionPreset("Emotional", "Minor Path", "i-VII-VI-v", ScaleType::Minor,
                                       { { 0, 3 }, { 6, 3 }, { 5, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Emotional", "Minor Lift", "iv-VI-III-VII", ScaleType::Minor,
                                       { { 3, 3 }, { 5, 3 }, { 2, 3 }, { 6, 3 } }),
            makeChordProgressionPreset("Cadence", "Dark Cadence", "ii-v-i", ScaleType::Minor,
                                       { { 1, 4 }, { 4, 4 }, { 0, 4 } }),
            makeChordProgressionPreset("Cadence", "Aeolian Circle", "i-iv-VII-III", ScaleType::Minor,
                                       { { 0, 3 }, { 3, 3 }, { 6, 3 }, { 2, 3 } }),
            makeChordProgressionPreset("Cadence", "Minor Resolve", "VI-VII-i-i", ScaleType::Minor,
                                       { { 5, 3 }, { 6, 3 }, { 0, 3 }, { 0, 3 } }),
            makeChordProgressionPreset("Cadence", "Minor Neo Soul", "iv-VII-III-VI", ScaleType::Minor,
                                       { { 3, 4 }, { 6, 4 }, { 2, 4 }, { 5, 4 } })
        };

        return scale == ScaleType::Minor ? minorPresets : majorPresets;
    }
}

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
    struct DDSPAutoSettings
    {
        bool  enabled    = false;
        float amount     = 0.0f;  // 0..1 blend between base patch and generated patch
        float brightness = 0.5f;  // 0..1 timbral bias
        float motion     = 0.25f; // 0..1 vibrato / movement bias
    };

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
    DDSPAutoSettings ddspAuto;
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
            { "Studio Piano",   make(true, 3,   2, 280, 0.12f,  420, 4800, 0.14f, 0.00f, 0.00f) },
            { "Soft EP",        make(true, 0,   3, 360, 0.22f,  780, 5200, 0.08f, 0.00f, 0.00f) },
            { "Plucked Pulse",  make(true, 2,   2, 150, 0.18f,  220, 3100, 0.28f, 5.10f, 0.02f, 1) },
            { "Fat Bass",       make(true, 2,   2, 120, 0.76f,  260,  430, 0.46f, 0.00f, 0.00f) },
            { "Deep Sub",       make(true, 0,   6, 180, 0.88f,  360,  260, 0.10f, 0.00f, 0.00f) },
            { "Bright Lead",    make(true, 1,   5, 100, 0.72f,  280, 5600, 0.24f, 5.00f, 0.05f, 1) },
            { "Mono Brass",     make(true, 2,  10, 170, 0.62f,  320, 1700, 0.40f, 4.80f, 0.02f, 0) },
            { "Glass Bell",     make(true, 0,   1, 420, 0.00f, 1400, 7200, 0.06f, 0.00f, 0.00f) },
            { "Bowed Pad",      make(true, 3, 220, 620, 0.84f, 2400, 2600, 0.16f, 4.40f, 0.03f, 1) },
            { "Warm Pad",       make(true, 3, 320, 760, 0.86f, 3400, 2100, 0.10f, 0.28f, 0.08f) },
            { "Air Pad",        make(true, 3, 520, 880, 0.90f, 4200, 3400, 0.08f, 0.24f, 0.10f) },
            { "Chiptune Pulse", make(true, 2,   1,  40, 0.00f,   90, 9600, 0.05f, 0.00f, 0.00f) },
        };
    }

    inline std::vector<Preset> mergeFactoryAndCustom(const std::vector<Preset>& customPresets)
    {
        auto presets = getAll();
        presets.insert(presets.end(), customPresets.begin(), customPresets.end());
        return presets;
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
    int          patternId  = -1;
    int          trackIndex = 0;
    float        startBar   = 0.0f;
    float        lengthBars = 1.0f;

    juce::String name;
};

struct Project
{
    double bpm = 140.0;
    PlayMode playMode = PlayMode::Pattern;
    int activePatternId = 1;
    KeySignature keySignature;

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

    std::vector<SynthPresets::Preset> customSynthPresets;
};
