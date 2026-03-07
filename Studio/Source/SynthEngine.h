/*
  ==============================================================================

    SynthEngine.h  — M13 Built-in Synthesizer
    8-voice polyphonic subtractive synth: oscillator + biquad LP filter + ADSR + LFO

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "ProjectModel.h"

// ---------------------------------------------------------------------------
// One polyphonic voice
// ---------------------------------------------------------------------------
class SynthVoice
{
public:
    // Trigger a note. noteLenSamples: auto-release after this many samples.
    void noteOn(int midiPitch, float velocity, double sr, const SynthParams& p, int noteLenSamples)
    {
        pitch_      = midiPitch;
        velocity_   = velocity;
        frequency_  = 440.0 * std::pow(2.0, (midiPitch - 69) / 12.0);
        phase_      = 0.0;
        lfoPhase_   = 0.0;

        const float attackSamples  = juce::jmax(1.0f, p.attack  * 0.001f * (float)sr);
        const float decaySamples   = juce::jmax(1.0f, p.decay   * 0.001f * (float)sr);
        const float releaseSamples = juce::jmax(1.0f, p.release * 0.001f * (float)sr);

        attackInc_  = 1.0f / attackSamples;
        decayInc_   = (1.0f - p.sustain) / decaySamples;
        sustainLvl_ = p.sustain;
        releaseInc_ = juce::jmax(0.000001f, p.sustain / releaseSamples);

        envState_ = Env::Attack;
        envLevel_ = 0.0f;

        noteLenRemaining_ = noteLenSamples;

        // Clear filter state
        z1L_ = z2L_ = z1R_ = z2R_ = 0.0f;
    }

    void noteOff()
    {
        if (envState_ != Env::Idle)
            envState_ = Env::Release;
    }

    bool isActive()  const { return envState_ != Env::Idle; }
    int  getPitch()  const { return pitch_; }

    void renderAdd(float* L, float* R, int numSamples, double sr, const SynthParams& p)
    {
        if (envState_ == Env::Idle) return;

        float b0, b1, b2, a1, a2;
        computeFilter(p.cutoff, p.resonance, sr, b0, b1, b2, a1, a2);

        const double lfoInc = p.lfoRate / sr;

        for (int i = 0; i < numSamples; ++i)
        {
            // Auto-release after note length
            if (noteLenRemaining_ > 0)
            {
                if (--noteLenRemaining_ == 0 &&
                    envState_ != Env::Release && envState_ != Env::Idle)
                    noteOff();
            }

            // ADSR envelope
            switch (envState_)
            {
                case Env::Attack:
                    envLevel_ += attackInc_;
                    if (envLevel_ >= 1.0f) { envLevel_ = 1.0f; envState_ = Env::Decay; }
                    break;
                case Env::Decay:
                    envLevel_ -= decayInc_;
                    if (envLevel_ <= sustainLvl_) { envLevel_ = sustainLvl_; envState_ = Env::Sustain; }
                    break;
                case Env::Sustain:
                    break;
                case Env::Release:
                    envLevel_ -= releaseInc_;
                    if (envLevel_ <= 0.0f) { envLevel_ = 0.0f; envState_ = Env::Idle; return; }
                    break;
                case Env::Idle:
                    return;
            }

            // LFO
            float lfoVal = 0.0f;
            if (p.lfoDepth > 0.0f)
            {
                lfoVal = std::sin((float)(lfoPhase_ * juce::MathConstants<double>::twoPi));
                lfoPhase_ += lfoInc;
                if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
            }

            // Frequency with optional pitch LFO
            double freq = frequency_;
            if (p.lfoDepth > 0.0f && p.lfoTarget == 1)
                freq *= std::pow(2.0, (double)(lfoVal * p.lfoDepth * 2.0f) / 12.0);

            // Cutoff with optional LFO — recompute filter only when modulating
            if (p.lfoDepth > 0.0f && p.lfoTarget == 0)
            {
                const float modCutoff = juce::jlimit(50.0f, 20000.0f,
                                                     p.cutoff * (1.0f + lfoVal * p.lfoDepth));
                computeFilter(modCutoff, p.resonance, sr, b0, b1, b2, a1, a2);
            }

            // Oscillator
            phase_ += freq / sr;
            if (phase_ >= 1.0) phase_ -= 1.0;

            const float osc    = generateSample((float)phase_, p.waveform);
            const float sample = osc * envLevel_ * velocity_ * 0.25f;  // 0.25 headroom for polyphony

            // Biquad LP filter (direct-form I)
            const float outL = b0 * sample + z1L_;
            z1L_ = b1 * sample - a1 * outL + z2L_;
            z2L_ = b2 * sample - a2 * outL;

            L[i] += outL;
            R[i] += outL;
        }
    }

private:
    enum class Env { Idle, Attack, Decay, Sustain, Release };

    int    pitch_             = 60;
    float  velocity_          = 0.8f;
    double frequency_         = 440.0;
    double phase_             = 0.0;
    double lfoPhase_          = 0.0;

    Env    envState_          = Env::Idle;
    float  envLevel_          = 0.0f;
    float  attackInc_         = 0.0f;
    float  decayInc_          = 0.0f;
    float  releaseInc_        = 0.0f;
    float  sustainLvl_        = 0.7f;

    int    noteLenRemaining_  = 0;

    // Biquad filter state (mono; same signal routed to L+R)
    float  z1L_ = 0.0f, z2L_ = 0.0f;
    float  z1R_ = 0.0f, z2R_ = 0.0f;

    float generateSample(float p, int waveform) const
    {
        switch (waveform)
        {
            case 0: return std::sin(p * juce::MathConstants<float>::twoPi);          // sine
            case 1: return 2.0f * p - 1.0f;                                          // sawtooth
            case 2: return p < 0.5f ? 1.0f : -1.0f;                                 // square
            case 3: return 1.0f - 4.0f * std::abs(p - 0.5f);                        // triangle
            default: return 0.0f;
        }
    }

    // Robert Bristow-Johnson biquad LP coefficients
    void computeFilter(float cutoff, float resonance, double sr,
                       float& b0, float& b1, float& b2, float& a1, float& a2) const
    {
        const float f     = juce::jlimit(20.0f, (float)(sr * 0.499), cutoff);
        const float Q     = 0.5f + resonance * 3.5f;          // Q: 0.5 – 4.0
        const float w0    = juce::MathConstants<float>::twoPi * f / (float)sr;
        const float cosW0 = std::cos(w0);
        const float sinW0 = std::sin(w0);
        const float alpha = sinW0 / (2.0f * Q);
        const float a0inv = 1.0f / (1.0f + alpha);

        b0 = ((1.0f - cosW0) * 0.5f) * a0inv;
        b1 = (1.0f - cosW0)          * a0inv;
        b2 = b0;
        a1 = (-2.0f * cosW0)         * a0inv;
        a2 = (1.0f - alpha)          * a0inv;
    }
};

// ---------------------------------------------------------------------------
// 8-voice polyphonic synthesizer
// ---------------------------------------------------------------------------
class PolySynth
{
public:
    static constexpr int kNumVoices = 8;

    // Trigger a note with an automatic note-length (in samples).
    void noteOn(int pitch, float velocity, double sr, const SynthParams& p, int noteLenSamples)
    {
        if (!p.enabled) return;

        int voiceIdx = -1;
        for (int i = 0; i < kNumVoices; ++i)
            if (!voices_[i].isActive()) { voiceIdx = i; break; }

        if (voiceIdx < 0)  // voice stealing — round-robin oldest
        {
            voiceIdx = stealIdx_;
            stealIdx_ = (stealIdx_ + 1) % kNumVoices;
        }

        voices_[voiceIdx].noteOn(pitch, velocity, sr, p, noteLenSamples);
    }

    void noteOff(int pitch)
    {
        for (auto& v : voices_)
            if (v.isActive() && v.getPitch() == pitch)
                v.noteOff();
    }

    void allNotesOff()
    {
        for (auto& v : voices_)
            v.noteOff();
    }

    bool isAnyActive() const
    {
        for (auto& v : voices_)
            if (v.isActive()) return true;
        return false;
    }

    // Renders and adds into the provided L/R float pointers.
    void renderNextBlock(float* L, float* R, int numSamples, double sr, const SynthParams& p)
    {
        for (auto& v : voices_)
            if (v.isActive())
                v.renderAdd(L, R, numSamples, sr, p);
    }

    void reset()
    {
        for (auto& v : voices_) v = SynthVoice{};
        stealIdx_ = 0;
    }

    void prepare(double /*sr*/, int /*maxBlock*/) {}

private:
    std::array<SynthVoice, kNumVoices> voices_;
    int stealIdx_ = 0;
};
