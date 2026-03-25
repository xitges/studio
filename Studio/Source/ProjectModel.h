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
enum class PadPlayMode { OneShot, Loop, Gate };
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
            makeChordProgressionPreset("Pop", "Sensitive Pop", "vi-IV-I-V", ScaleType::Major,
                                       { { 5, 3 }, { 3, 3 }, { 0, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Pop", "Lifted Chorus", "I-IV-vi-V", ScaleType::Major,
                                       { { 0, 3 }, { 3, 3 }, { 5, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Pop", "Big Hook", "IV-I-V-vi", ScaleType::Major,
                                       { { 3, 3 }, { 0, 3 }, { 4, 3 }, { 5, 3 } }),
            makeChordProgressionPreset("Pop", "Late Night Pop", "I-iii-vi-IV", ScaleType::Major,
                                       { { 0, 3 }, { 2, 3 }, { 5, 3 }, { 3, 3 } }),
            makeChordProgressionPreset("R&B", "Silk Bounce", "I-vi-ii-V", ScaleType::Major,
                                       { { 0, 4 }, { 5, 4 }, { 1, 4 }, { 4, 4 } }),
            makeChordProgressionPreset("R&B", "Afterglow", "iii-vi-ii-V", ScaleType::Major,
                                       { { 2, 4 }, { 5, 4 }, { 1, 4 }, { 4, 4 } }),
            makeChordProgressionPreset("R&B", "Weekend Glide", "IV-V-iii-vi", ScaleType::Major,
                                       { { 3, 4 }, { 4, 4 }, { 2, 4 }, { 5, 4 } }),
            makeChordProgressionPreset("Funk", "Funk Push", "I-IV-ii-V", ScaleType::Major,
                                       { { 0, 4 }, { 3, 4 }, { 1, 4 }, { 4, 4 } }),
            makeChordProgressionPreset("House", "Piano Rush", "I-V-ii-IV", ScaleType::Major,
                                       { { 0, 3 }, { 4, 3 }, { 1, 3 }, { 3, 3 } }),
            makeChordProgressionPreset("Jazz", "Jazz Cadence", "ii-V-I", ScaleType::Major,
                                       { { 1, 4 }, { 4, 4 }, { 0, 4 } }),
            makeChordProgressionPreset("Jazz", "Circle Resolve", "vi-ii-V-I", ScaleType::Major,
                                       { { 5, 4 }, { 1, 4 }, { 4, 4 }, { 0, 4 } }),
            makeChordProgressionPreset("Jazz", "Neo Soul Lift", "ii-V-I-vi", ScaleType::Major,
                                       { { 1, 4 }, { 4, 4 }, { 0, 4 }, { 5, 4 } }),
            makeChordProgressionPreset("Jazz", "Sunset Extensions", "IV-vii-iii-vi", ScaleType::Major,
                                       { { 3, 4 }, { 6, 4 }, { 2, 4 }, { 5, 4 } }),
            makeChordProgressionPreset("Anthem", "Bright Walk", "I-ii-IV-V", ScaleType::Major,
                                       { { 0, 3 }, { 1, 3 }, { 3, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Anthem", "Festival Lift", "I-V-vi-V", ScaleType::Major,
                                       { { 0, 3 }, { 4, 3 }, { 5, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Long Form", "Climbing Story", "I-iii-IV-I-ii-V-I", ScaleType::Major,
                                       { { 0, 3 }, { 2, 3 }, { 3, 3 }, { 0, 3 }, { 1, 3 }, { 4, 3 }, { 0, 3 } }),
            makeChordProgressionPreset("Long Form", "City Pop Cruise", "IV-V-iii-vi-ii-V-I", ScaleType::Major,
                                       { { 3, 4 }, { 4, 4 }, { 2, 4 }, { 5, 4 }, { 1, 4 }, { 4, 4 }, { 0, 4 } })
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
            makeChordProgressionPreset("Emotional", "Sad Hook", "VI-III-VII-i", ScaleType::Minor,
                                       { { 5, 3 }, { 2, 3 }, { 6, 3 }, { 0, 3 } }),
            makeChordProgressionPreset("Emotional", "Minor Turn", "i-iv-v-i", ScaleType::Minor,
                                       { { 0, 3 }, { 3, 3 }, { 4, 3 }, { 0, 3 } }),
            makeChordProgressionPreset("Emotional", "Minor Path", "i-VII-VI-v", ScaleType::Minor,
                                       { { 0, 3 }, { 6, 3 }, { 5, 3 }, { 4, 3 } }),
            makeChordProgressionPreset("Emotional", "Minor Lift", "iv-VI-III-VII", ScaleType::Minor,
                                       { { 3, 3 }, { 5, 3 }, { 2, 3 }, { 6, 3 } }),
            makeChordProgressionPreset("R&B", "Velvet Minor", "i-v-iv-VI", ScaleType::Minor,
                                       { { 0, 4 }, { 4, 4 }, { 3, 4 }, { 5, 4 } }),
            makeChordProgressionPreset("R&B", "Minor Neo Soul", "iv-VII-III-VI", ScaleType::Minor,
                                       { { 3, 4 }, { 6, 4 }, { 2, 4 }, { 5, 4 } }),
            makeChordProgressionPreset("R&B", "After Hours", "i-III-iv-VI", ScaleType::Minor,
                                       { { 0, 4 }, { 2, 4 }, { 3, 4 }, { 5, 4 } }),
            makeChordProgressionPreset("House", "Night Drive", "i-VI-III-VII", ScaleType::Minor,
                                       { { 0, 3 }, { 5, 3 }, { 2, 3 }, { 6, 3 } }),
            makeChordProgressionPreset("Jazz", "Dark Cadence", "ii-v-i", ScaleType::Minor,
                                       { { 1, 4 }, { 4, 4 }, { 0, 4 } }),
            makeChordProgressionPreset("Jazz", "Smoked Resolve", "iv-v-i-VI", ScaleType::Minor,
                                       { { 3, 4 }, { 4, 4 }, { 0, 4 }, { 5, 4 } }),
            makeChordProgressionPreset("Cadence", "Minor Resolve", "VI-VII-i-i", ScaleType::Minor,
                                       { { 5, 3 }, { 6, 3 }, { 0, 3 }, { 0, 3 } }),
            makeChordProgressionPreset("Long Form", "Shadow Story", "i-VI-III-VII-i", ScaleType::Minor,
                                       { { 0, 3 }, { 5, 3 }, { 2, 3 }, { 6, 3 }, { 0, 3 } })
        };

        return scale == ScaleType::Minor ? minorPresets : majorPresets;
    }
}

// Per-step expressive parameters — applied when a step fires.
// All values are multiplicative or additive on top of channel defaults.
// Default (isDefault() == true) → no change from current behaviour.
struct StepParams
{
    float velocity    = 1.0f;   // 0..2  — multiplier on trigger velocity (1.0 = default 0.8)
    float gate        = 1.0f;   // 0..2  — multiplier on note gate length  (1.0 = full 1/16 step)
    float probability = 1.0f;   // 0..1  — chance this step fires           (1.0 = always)
    int   pitchOffset = 0;      // -12..+12  semitone offset added to channel pitch

    // Phase 2 — timbral modulation
    float cutoffMod      = 0.0f;  // -3..+3 octaves applied on top of channel cutoff (0 = no change)
    float startOffsetFrac = 0.0f; // 0..1  — normalized source-buffer start position (0 = channel default)

    // Groove — per-step timing offset
    float timingOffset   = 0.0f;  // -0.5..+0.5 — fraction of one step duration (0 = on grid)

    bool isDefault() const noexcept
    {
        return velocity == 1.0f && gate == 1.0f
            && probability == 1.0f && pitchOffset == 0
            && cutoffMod == 0.0f && startOffsetFrac == 0.0f
            && timingOffset == 0.0f;
    }
    void reset() { *this = StepParams{}; }
};

// M3 — one note in a melodic channel's piano roll
struct NoteEvent
{
    int   pitch       = 60;    // MIDI note number (C4 = 60)
    float startBeat   = 0.0f;  // beat position within the pattern (0 = start)
    float lengthBeats = 0.25f; // duration in beats (0.25 = 16th note)
    float velocity    = 0.8f;  // 0.0 – 1.0
};

// ---------------------------------------------------------------------------
// Sampler instrument source — used when channelSourceType == Sampler
// The sample is pitched chromatically around rootNote.
// ADSR / Filter / LFO from SynthParams still apply.
// ---------------------------------------------------------------------------
struct SamplerParams
{
    juce::String samplePath;          // absolute path to the loaded sample file

    int   rootNote      = 60;         // MIDI note at which the sample plays at original pitch
    float fineTuneCents = 0.0f;       // ±100 cents fine-tune on top of rootNote mapping
    float gain          = 1.0f;       // 0..2 sample pre-gain (before envelope)

    // Loop — sustained playback for pad / string / brass sounds
    bool  loopEnabled   = false;
    int   loopStartSamples = 0;       // loop region start  (0 = file start)
    int   loopEndSamples   = 0;       // loop region end    (0 = file end)

    // One-shot flag: if true, note-off has no effect; voice plays to end of sample.
    // Useful for drum instruments loaded in Melodic mode for chromatic kit playback.
    bool  oneShot       = false;

    // Start offset — skip N samples at the front (trim transient / skip silence)
    int   startOffsetSamples = 0;
};

// Which oscillator/source a Melodic channel uses for voice rendering.
// Drum channels are unaffected — they always use one-shot sample triggers.
enum class ChannelSourceType
{
    Synth   = 0,   // classic waveform oscillator (existing behaviour)
    Sampler = 1,   // sample buffer read at pitched playback rate
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

    bool  enabled      = false;
    int   waveform     = 1;        // 0=Sine 1=Saw 2=Square 3=Triangle 4=Pulse 5=Noise 6=Supersaw
    float pulseWidth   = 0.5f;    // Pulse duty cycle 0.05–0.95
    int   unisonVoices = 1;       // 1–8 stacked oscillators
    float unisonDetune = 0.0f;    // 0–100 cents total spread
    float unisonSpread = 0.5f;    // 0–1 stereo width

    // ---------------------------------------------------------------------------
    // ADSR — UI slider values in ms (mapped through double-log curve internally).
    // Practical perceived times at 44.1 kHz:
    //   attack 5 ms UI  → ~0.51 ms actual   (noteOnFade provides pop-free onset)
    //   decay  800 ms UI → ~25  ms actual   → ~146 ms audible decay tail
    //   release 900 ms UI → ~29 ms actual   → ~278 ms audible release tail
    // ---------------------------------------------------------------------------
    float attack       = 5.0f;    // ms
    float decay        = 800.0f;  // ms  (was 80 → snap; 800 = natural settle)
    float sustain      = 0.65f;   // 0.0–1.0  (was 0.6; slightly fuller body)
    float release      = 900.0f;  // ms  (was 200 → abrupt; 900 = musical tail)

    // ---------------------------------------------------------------------------
    // Filter — default tuned so the Ladder LP stays audible throughout the note:
    //   cutoff 3200 Hz + filterEnvAmount 0.30 → sweeps 4.3 kHz (closed) → 9.4 kHz
    //   (sustain).  Old 4000 Hz + 0.50 swept the filter fully open at sustain,
    //   effectively bypassing it and losing all warmth on held notes.
    // ---------------------------------------------------------------------------
    float cutoff       = 3200.0f; // Hz  (was 4000; warmer base, filter stays in play)
    float resonance    = 0.18f;   // 0.0–1.0  (was 0.3; less nasal/peaky character)
    int   filterType      = 0;      // 0=LP 1=HP 2=BP 3=Notch (for SVF)
    int   filterMode      = 0;      // 0=Ladder 1=SVF
    float filterDrive     = 0.0f;   // 0–1 pre-filter saturation
    float filterEnvAmount = 0.30f;  // -1..+1  (was 0.5; gentle musical sweep, not dominant)
    float driftDepth      = 0.22f;  // 0..1  (was 0.3; still organic, less obvious wobble)
    float lfoRate      = 2.0f;   // Hz
    float lfoDepth     = 0.0f;   // 0.0 – 1.0
    int   lfoTarget    = 0;      // 0=Cutoff 1=Pitch 2=Amplitude 3=PulseWidth
    int   lfoWaveform  = 0;      // 0=Sine 1=Triangle 2=Saw 3=Square 4=S&H
    bool  lfoFreeRun   = false;  // don't reset phase on noteOn
    float lfoFadeIn    = 0.0f;   // 0–2000 ms ramp-up
    DDSPAutoSettings ddspAuto;

    // Physical modelling params (waveform 7 = Plucked, waveform 8 = Wind)
    // Plucked (Karplus-Strong)
    float ksDamping        = 0.15f;  // 0=bright, 1=dark (loss filter coeff)
    float ksDecay          = 0.9985f; // feedback gain (sustain length)
    float ksStiffness      = 0.3f;   // allpass dispersion (inharmonicity)
    float ksBrightness     = 8000.0f; // excitation LP cutoff Hz
    float ksPluckPos       = 0.10f;  // pluck position (0=bridge, 0.5=middle)
    float ksBodyFreq       = 180.0f; // body resonance center Hz
    float ksBodyAmount     = 0.25f;  // body resonance wet mix
    // Wind (reed + bore)
    float windBreathPressure = 0.5f; // steady-state breath level
    float windReedStiffness  = 0.7f; // reed nonlinearity
    float windBoreLoss       = 0.15f; // bore LP loss coefficient
    float windNoiseAmount    = 0.03f; // breath noise mix

    juce::String presetName;  // tracks last selected preset (not a synthesis param)
};

// ---------------------------------------------------------------------------
// Sampler-neutral SynthParams — transparent processing profile for Sampler
// source channels.  Preserves the loaded sample's original timbre by default;
// the user can shape it further through the editor if desired.
//
// Key differences from the synth defaults:
//   filterEnvAmount  0.5 → 0.0   no filter sweep on attack/decay (main offender)
//   cutoff        4000  → 20000  fully open low-pass, no frequency coloring
//   resonance        0.3 → 0.0   no resonant peak at cutoff frequency
//   sustain          0.6 → 1.0   full amplitude during sustain — no volume dip
//   decay            80  → 1.0   near-zero decay, reaches full sustain immediately
//   release         200  → 50    short natural fade, not a dramatic synth tail
//   driftDepth       0.3 → 0.0   no pitch-drift artifacts on sample playback
// ---------------------------------------------------------------------------
inline SynthParams samplerNeutralSynthParams()
{
    SynthParams p;
    p.enabled         = true;      // sampler channels are always active
    p.cutoff          = 20000.0f;  // fully open — no timbral coloring
    p.resonance       = 0.0f;      // no resonance peak
    p.filterEnvAmount = 0.0f;      // no filter sweep (was 0.5 — the main offender)
    p.filterDrive     = 0.0f;      // no drive saturation
    p.attack          = 5.0f;      // fast — lets natural transients through
    p.decay           = 1.0f;      // near-zero — reach full sustain immediately
    p.sustain         = 1.0f;      // full amplitude throughout the note
    p.release         = 50.0f;     // short, clean fade-out
    p.lfoDepth        = 0.0f;      // no modulation unless user enables it
    p.driftDepth      = 0.0f;      // no oscillator-drift artifacts
    return p;
}

// Returns true if the given SynthParams still carry the out-of-the-box synth
// defaults that were never intentionally edited for sampler use.
// Used for one-time migration: when a saved Sampler channel still has these
// values, apply samplerNeutralSynthParams() so it sounds correct on first open.
inline bool looksLikeUntouchedSynthDefaults(const SynthParams& p)
{
    return p.filterEnvAmount > 0.45f && p.filterEnvAmount < 0.55f
        && p.cutoff          > 3900.0f && p.cutoff        < 4100.0f
        && p.resonance       > 0.25f   && p.resonance     < 0.35f
        && p.sustain         > 0.55f   && p.sustain       < 0.65f;
}

// Instrument preset database — factory presets for SynthEditorPanel ComboBox
namespace SynthPresets
{
    struct Preset { juce::String name; SynthParams params; };

    // ---------------------------------------------------------------------------
    // Fluent builder — lets each preset set only the fields it cares about.
    // Unset fields keep their SynthParams defaults.
    // ---------------------------------------------------------------------------
    struct B
    {
        SynthParams v;
        B() { v.enabled = true; }

        // Oscillator
        B& wv   (int x)                         { v.waveform      = x;              return *this; }
        B& pw   (float x)                        { v.pulseWidth    = x;              return *this; }

        // ADSR (all in ms, except sustain 0-1)
        B& adsr (float a,float d,float s,float r){ v.attack=a; v.decay=d; v.sustain=s; v.release=r; return *this; }

        // Filter  mode: 0=Ladder 1=SVF   type: 0=LP 1=HP 2=BP 3=Notch
        B& filt (float co, float res,
                 int mode=0, int type=0,
                 float drive=0.0f, float env=0.5f)
        {
            v.cutoff=co; v.resonance=res;
            v.filterMode=mode; v.filterType=type;
            v.filterDrive=drive; v.filterEnvAmount=env;
            return *this;
        }

        // LFO  wf: 0=Sine 1=Tri 2=Saw 3=Square 4=S&H   target: 0=Cutoff 1=Pitch 2=Amp 3=PW
        B& lfo  (float rate, float depth,
                 int target=0, int wf=0, float fadeIn=0.0f)
        {
            v.lfoRate=rate; v.lfoDepth=depth;
            v.lfoTarget=target; v.lfoWaveform=wf; v.lfoFadeIn=fadeIn;
            return *this;
        }

        // Unison
        B& uni  (int voices, float detune=0.0f, float spread=0.5f)
        { v.unisonVoices=voices; v.unisonDetune=detune; v.unisonSpread=spread; return *this; }

        // Oscillator drift (0-1)
        B& drift(float d) { v.driftDepth = d; return *this; }

        operator SynthParams() const { return v; }
    };

    // ---------------------------------------------------------------------------
    // Factory presets
    //
    // Waveform:   0=Sine  1=Saw  2=Square  3=Triangle  4=Pulse  5=Noise  6=Supersaw
    // FilterMode: 0=Ladder (warm, non-linear)   1=SVF (clean, multimode)
    // FilterType: 0=LP  1=HP  2=BP  3=Notch(SVF only)
    // LFO target: 0=Cutoff  1=Pitch  2=Amplitude  3=PulseWidth
    // ---------------------------------------------------------------------------
    inline std::vector<Preset> getAll()
    {
        return {

        // ── Keys ──────────────────────────────────────────────────────────────
        { "Studio Piano",
          B().wv(3)                                      // Triangle — clean harmonic stack
             .adsr(3, 300, 0.08f, 500)
             .filt(6000, 0.10f, 1, 0, 0.0f, -0.28f)    // SVF LP, filter closes with env
             .drift(0.22f)
        },

        { "Soft EP",
          B().wv(0)                                      // Sine — fundamental-heavy EP tone
             .adsr(4, 380, 0.20f, 900)
             .filt(5500, 0.06f, 1, 0, 0.0f, -0.30f)    // SVF LP, slight filter closure
             .lfo(5.5f, 0.015f, 2, 0, 180.0f)           // subtle amplitude tremolo fade-in
             .drift(0.20f)
        },

        // ── Bass ──────────────────────────────────────────────────────────────
        { "Fat Bass",
          B().wv(2)                                      // Square — odd harmonics = fat
             .adsr(2, 140, 0.72f, 280)
             .filt(500, 0.58f, 0, 0, 0.30f, 0.65f)     // Ladder LP, drive + strong env sweep
             .uni(2, 8.0f, 0.25f)                       // slight unison for width
             .drift(0.40f)
        },

        { "Deep Sub",
          B().wv(0)                                      // Sine — pure sub fundamental
             .adsr(8, 200, 0.90f, 400)
             .filt(240, 0.05f, 0, 0, 0.08f, 0.10f)     // Ladder LP, very low cutoff
             .drift(0.15f)
        },

        // ── Lead ──────────────────────────────────────────────────────────────
        { "Bright Lead",
          B().wv(1)                                      // Saw — rich overtone stack
             .adsr(4, 120, 0.68f, 300)
             .filt(5800, 0.32f, 0, 0, 0.15f, 0.42f)    // Ladder LP, filter env adds bite
             .lfo(5.2f, 0.05f, 1, 0, 300.0f)            // pitch vibrato with 300ms fade-in
             .drift(0.30f)
        },

        { "Plucked Pulse",
          B().wv(4).pw(0.28f)                           // Pulse — narrow width = bright pluck
             .adsr(1, 120, 0.05f, 180)
             .filt(3500, 0.38f, 0, 0, 0.10f, 0.68f)    // Ladder LP, fast filter sweep = pluck
             .lfo(5.1f, 0.02f, 1)                        // micro pitch wobble
             .drift(0.35f)
        },

        // ── Brass / Wind ──────────────────────────────────────────────────────
        { "Mono Brass",
          B().wv(2)                                      // Square — hollow brass resonance
             .adsr(12, 180, 0.60f, 340)
             .filt(1900, 0.48f, 0, 0, 0.22f, 0.58f)    // Ladder LP, envelope opens filter
             .lfo(4.8f, 0.02f, 0)                        // subtle cutoff vibrato
             .drift(0.25f)
        },

        // ── Bells / Mallet ────────────────────────────────────────────────────
        { "Glass Bell",
          B().wv(0)                                      // Sine — pure inharmonic shimmer
             .adsr(1, 450, 0.00f, 1800)
             .filt(8500, 0.04f, 1, 0, 0.0f, -0.08f)    // SVF LP, wide open, slight closure
             .drift(0.15f)
        },

        // ── Pads ──────────────────────────────────────────────────────────────
        { "Bowed Pad",
          B().wv(3)                                      // Triangle — soft bowed texture
             .adsr(250, 640, 0.82f, 2600)
             .filt(2800, 0.18f, 1, 0, 0.0f, 0.22f)     // SVF LP, gentle env opening
             .lfo(4.4f, 0.03f, 1, 0, 400.0f)            // slow pitch vibrato after fade-in
             .uni(2, 4.0f, 0.50f)                       // gentle stereo widening
             .drift(0.35f)
        },

        { "Warm Pad",
          B().wv(6)                                      // Supersaw — dense detuned wall
             .adsr(350, 780, 0.85f, 3800)
             .filt(2400, 0.12f, 1, 0, 0.0f, 0.18f)     // SVF LP, slow filter bloom
             .lfo(0.30f, 0.09f, 0)                       // very slow cutoff drift
             .uni(4, 15.0f, 0.80f)                      // wide 4-voice unison
             .drift(0.50f)
        },

        { "Air Pad",
          B().wv(6)                                      // Supersaw — lush high-voice shimmer
             .adsr(580, 900, 0.88f, 4500)
             .filt(3800, 0.07f, 1, 0, 0.0f, 0.12f)     // SVF LP, very open
             .lfo(0.25f, 0.10f, 2)                       // slow amplitude swell = breathing
             .uni(6, 18.0f, 0.90f)                      // 6-voice wide unison
             .drift(0.55f)
        },

        // ── Digital / Retro ───────────────────────────────────────────────────
        { "Chiptune Pulse",
          B().wv(4).pw(0.25f)                           // Pulse narrow = classic 8-bit tone
             .adsr(1, 20, 0.90f, 60)
             .filt(18000, 0.02f, 1, 0, 0.0f, 0.0f)     // SVF LP fully open = bypass
             .drift(0.05f)                               // very stable, chip-accurate
        },

        }; // end preset list
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
    float        volume   = 0.8f;
    float        pitch    = 0.0f;   // semitones offset
    PadPlayMode  playMode = PadPlayMode::OneShot;
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

// Auto-Tune preset indices
enum class AutoTunePreset
{
    Subtle = 0,
    NaturalPop,
    ModernPop,
    HardTune,
    HyperPop,
    Robotic,
    Custom,       // user-modified from any preset
    NumPresets
};

// Auto-Tune — real-time pitch correction (hyper-pop / T-Pain style)
struct AutoTuneParams
{
    bool  enabled         = false;
    int   keyTonic        = 0;       // 0=C, 1=C#, … 11=B
    int   scaleType       = 0;       // 0=Major, 1=Minor (maps to ScaleType)

    // -- Correction engine --
    float retuneSpeed     = 0.0f;    // 0.0 = instant snap, 1.0 = very slow convergence
    float correctionAmount = 1.0f;   // 0.0 = no correction, 1.0 = full snap to target
    float humanize        = 0.0f;    // 0.0 = rigid, 1.0 = allows expressive micro-deviation
    float flexTune        = 0.0f;    // 0.0 = strict, 1.0 = tolerates near-center pitch (cents window)

    // -- Transition / Glide --
    float transitionSpeed = 0.0f;    // 0.0 = instant note change, 1.0 = slow portamento between notes

    // -- Vibrato --
    float vibratoPreserve = 0.5f;    // 0.0 = kill all vibrato, 1.0 = fully preserve natural vibrato

    // -- Formant --
    float formantAmount   = 1.0f;    // 0.0 = shift formants with pitch (robotic), 1.0 = full preserve
    // Legacy compat: formantPreserve maps to formantAmount >= 0.5
    bool  formantPreserve = true;

    // -- Mix --
    float mix             = 1.0f;    // 0.0 = dry, 1.0 = fully corrected

    // -- Input range --
    float inputLowHz      = 65.0f;   // lowest expected input pitch (C2)
    float inputHighHz     = 1200.0f; // highest expected input pitch (D6)

    // -- Per-note scale mask (chromatic: true = note allowed as target) --
    // Default: all 12 notes enabled (scale filtering still applies via keyTonic+scaleType)
    bool  noteMask[12]    = { true, true, true, true, true, true,
                              true, true, true, true, true, true };
    bool  useNoteMask     = false;   // when true, noteMask overrides scale snapping

    // -- Preset --
    int   presetIndex     = static_cast<int>(AutoTunePreset::ModernPop);
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

struct VariationData
{
    static constexpr int kMaxChannels = 16;
    static constexpr int kMaxSteps    = kMaxPatternSteps;

    bool       steps     [kMaxChannels][kMaxSteps] {};
    StepParams stepParams[kMaxChannels][kMaxSteps] {};   // per-step expression; isDefault() → no override
    std::vector<NoteEvent> notes[kMaxChannels];
};

struct Pattern
{
    int          id           = 0;
    juce::String name         = "Pattern 1";
    int          lengthBars   = 1;
    int          stepCount    = 16;
    int          channelCount = 3;      // per-pattern channel count (default 3)
    float        swingAmount  = 0.0f;   // 0.0 = straight, 0.33 = triplet, 0.5 = dotted swing

    static constexpr int kMaxChannels  = 16;
    static constexpr int kMaxSteps     = kMaxPatternSteps;
    static constexpr int kMaxVariations = 4;

    VariationData variations[kMaxVariations];

    // per-channel sample paths (each pattern has its own set)
    juce::String samplePaths[kMaxChannels];

    // per-pattern channel mixer values (independent per pattern)
    float channelVolume[kMaxChannels];
    float channelPan   [kMaxChannels];
    float channelPitch [kMaxChannels];

    // per-pattern channel identity & instrument settings
    ChannelType       channelTypes      [kMaxChannels] = {};   // all Drum by default
    juce::String      channelNames      [kMaxChannels];
    SynthParams       synthParams       [kMaxChannels] = {};

    // Sampler instrument: independent from synthParams; source type selects which to use
    ChannelSourceType channelSourceTypes[kMaxChannels] = {};   // all Synth by default
    SamplerParams     samplerParams     [kMaxChannels] = {};

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

enum class ClipType { Pattern, Audio };

// Playback algorithm for audio clips.
// Resample  — speed + pitch change together (lightweight, no external dep)
// Stretch   — pitch-only (RubberBand offline, linear quality)
// Elastique — pitch-only (RubberBand offline, high quality)
enum class AudioClipMode { Resample, Stretch, Elastique };

struct PlaylistClip
{
    int          id           = 0;
    int          patternId    = -1;
    int          trackIndex   = 0;
    float        startBar     = 0.0f;
    float        lengthBars   = 1.0f;
    int          variationIdx = 0;

    juce::String name;

    // Audio clip fields (ignored when clipType == Pattern)
    ClipType      clipType      = ClipType::Pattern;
    juce::String  audioFilePath;
    float         pitchSemitone = 0.0f;        // -24 ~ +24 st
    AudioClipMode audioClipMode = AudioClipMode::Resample;
    float         fadeInBars    = 0.0f;        // gain 0→1 ramp at clip start
    float         fadeOutBars   = 0.0f;        // gain 1→0 ramp at clip end
    float         sourceOffsetSamples         = 0.0f; // slip edit: current file read start (samples, looped)
    float         originalSourceOffsetSamples = 0.0f; // reset target (defaults to 0 = file start)

    // Pattern Clip slip edit (ignored when clipType == Audio)
    float         patternStartOffsetBars         = 0.0f; // pattern internal start offset (bars, looped)
    float         originalPatternStartOffsetBars = 0.0f; // reset target

    bool          muted = false;  // clip mute — skipped during playback when true
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

    // Auto-Tune — per-mixer-track pitch correction
    std::array<AutoTuneParams, 8> autoTuneParams = {};

    // Launchpad — 8×8 pad assignments
    std::array<LaunchpadPad, 64> launchpadPads = {};

    // M9 — automation lanes
    std::vector<AutomationLane> automationLanes;

    // M8 — VST/AU instrument plugins (one slot per channel)
    std::array<PluginSlot, 16> channelInstrumentPlugins = {};

    std::vector<SynthPresets::Preset> customSynthPresets;
};
