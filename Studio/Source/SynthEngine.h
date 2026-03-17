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
#include <vector>
#include "ProjectModel.h"

namespace DDSPAutoPatch
{
    inline float blendFloat(float base, float generated, float amount)
    {
        return base + (generated - base) * juce::jlimit(0.0f, 1.0f, amount);
    }

    inline SynthParams generate(const SynthParams& base, int midiPitch, float velocity,
                                double noteLengthSeconds = 0.25)
    {
        if (!base.ddspAuto.enabled || base.ddspAuto.amount <= 0.001f)
            return base;

        const float amount = juce::jlimit(0.0f, 1.0f, base.ddspAuto.amount);
        const float brightnessBias = juce::jlimit(0.0f, 1.0f, base.ddspAuto.brightness);
        const float motionBias = juce::jlimit(0.0f, 1.0f, base.ddspAuto.motion);
        const float vel = juce::jlimit(0.0f, 1.0f, velocity);
        const float pitchNorm = juce::jlimit(0.0f, 1.0f, (float)(midiPitch - 24) / 84.0f);
        const float lenNorm = juce::jlimit(0.0f, 1.0f, (float)(noteLengthSeconds / 1.6));

        SynthParams generated = base;
        const float targetBrightness = juce::jlimit(0.0f, 1.0f,
                                                    brightnessBias * 0.65f + pitchNorm * 0.25f + vel * 0.10f);

        generated.waveform =
            targetBrightness < 0.24f ? 0 :
            targetBrightness < 0.50f ? 3 :
            targetBrightness < 0.72f ? 2 : 1;

        generated.attack = juce::jlimit(1.0f, 2000.0f,
            8.0f + (1.0f - vel) * 40.0f + (1.0f - pitchNorm) * 25.0f + lenNorm * 110.0f);
        generated.decay = juce::jlimit(10.0f, 2200.0f,
            80.0f + (1.0f - pitchNorm) * 240.0f + lenNorm * 600.0f + brightnessBias * 80.0f);
        generated.sustain = juce::jlimit(0.0f, 1.0f,
            0.18f + lenNorm * 0.55f + (1.0f - targetBrightness) * 0.18f);
        generated.release = juce::jlimit(20.0f, 4000.0f,
            120.0f + lenNorm * 1400.0f + (1.0f - vel) * 180.0f + motionBias * 220.0f);
        generated.cutoff = juce::jlimit(80.0f, 20000.0f,
            250.0f + targetBrightness * 12000.0f + vel * 3500.0f + pitchNorm * 2500.0f);
        generated.resonance = juce::jlimit(0.05f, 0.92f,
            0.12f + (1.0f - targetBrightness) * 0.22f + motionBias * 0.20f);
        generated.lfoRate = juce::jlimit(0.1f, 20.0f,
            0.6f + motionBias * 6.0f + pitchNorm * 1.2f);
        generated.lfoDepth = juce::jlimit(0.0f, 1.0f,
            motionBias * 0.34f + lenNorm * 0.10f);
        generated.lfoTarget = motionBias >= 0.45f ? 1 : 0;

        SynthParams result = base;
        result.attack    = blendFloat(base.attack,    generated.attack,    amount);
        result.decay     = blendFloat(base.decay,     generated.decay,     amount);
        result.sustain   = blendFloat(base.sustain,   generated.sustain,   amount);
        result.release   = blendFloat(base.release,   generated.release,   amount);
        result.cutoff    = blendFloat(base.cutoff,    generated.cutoff,    amount);
        result.resonance = blendFloat(base.resonance, generated.resonance, amount);
        result.lfoRate   = blendFloat(base.lfoRate,   generated.lfoRate,   amount);
        result.lfoDepth  = blendFloat(base.lfoDepth,  generated.lfoDepth,  amount);
        if (amount >= 0.5f)
            result.waveform = generated.waveform;
        if (amount >= 0.35f)
            result.lfoTarget = generated.lfoTarget;
        return result;
    }
}

namespace SynthVoicing
{
    inline float stableHash01(int seed)
    {
        const auto x = (juce::uint32)seed * 747796405u + 2891336453u;
        return (float)((x >> 8) & 0x00ffffffu) / (float)0x01000000u;
    }

    inline float softClip(float x)
    {
        return x / (1.0f + 0.6f * std::abs(x));
    }

    inline float detuneRatioFromCents(float cents)
    {
        return std::pow(2.0f, cents / 1200.0f);
    }

    inline float curveUp(float t, float shape)
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        return 1.0f - std::pow(1.0f - t, juce::jlimit(1.15f, 4.0f, shape));
    }

    inline float curveDown(float t, float shape)
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        return std::pow(1.0f - t, juce::jlimit(1.15f, 4.5f, shape));
    }

    inline float hzToNormalized(float hz, float minHz = 50.0f, float maxHz = 20000.0f)
    {
        const float safeHz = juce::jlimit(minHz, maxHz, hz);
        return juce::jlimit(0.0f, 1.0f,
                            std::log(safeHz / minHz) / std::log(maxHz / minHz));
    }

    inline float normalizedToHz(float norm, float minHz = 50.0f, float maxHz = 20000.0f)
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, norm);
        return minHz * std::pow(maxHz / minHz, clamped);
    }

    inline float smoothValue(float current, float target, float coeff)
    {
        return target + coeff * (current - target);
    }
}

// ---------------------------------------------------------------------------
// ADSR slider ranges — single source of truth for both DSP and normalisation.
// Raw UI values are in milliseconds.
// ---------------------------------------------------------------------------
struct ADSRRange
{
    static constexpr float kAttackMin  =  1.0f, kAttackMax  = 2000.0f;
    static constexpr float kDecayMin   = 10.0f, kDecayMax   = 2200.0f;
    static constexpr float kReleaseMin = 20.0f, kReleaseMax = 4000.0f;

    static float attackNorm (float v) noexcept
        { return juce::jlimit(0.f, 1.f, (v - kAttackMin)  / (kAttackMax  - kAttackMin )); }
    static float decayNorm  (float v) noexcept
        { return juce::jlimit(0.f, 1.f, (v - kDecayMin)   / (kDecayMax   - kDecayMin  )); }
    static float releaseNorm(float v) noexcept
        { return juce::jlimit(0.f, 1.f, (v - kReleaseMin) / (kReleaseMax - kReleaseMin)); }
};

// ---------------------------------------------------------------------------
// Perceptually uniform (logarithmic) envelope time mapping.
// norm [0,1] → ms, exponentially spaced between minMs and maxMs.
// ---------------------------------------------------------------------------
inline float envTimeMs(float norm, float minMs, float maxMs)
{
    return minMs * std::pow(maxMs / minMs, juce::jlimit(0.0f, 1.0f, norm));
}

// ---------------------------------------------------------------------------
// One-pole exponential envelope coefficient for a given time in ms.
// env(n) = target + (start - target) * coeff^n  →  reaches target asymptotically.
// coeff ≈ 1: slow;  coeff ≈ 0: instant.
// ---------------------------------------------------------------------------
inline float envCoeff(float timeMs, float sampleRate)
{
    if (timeMs < 0.01f) return 0.0f;
    return std::exp(-1000.0f / (timeMs * sampleRate));
}

// ---------------------------------------------------------------------------
// One polyphonic voice
// ---------------------------------------------------------------------------
// =============================================================================
// Filter architecture — IFilter interface + LadderFilter implementation
// =============================================================================

enum class FilterMode { Ladder, SVF, Diode };

// ---------------------------------------------------------------------------
// Abstract filter interface — all filter types must conform to this
// ---------------------------------------------------------------------------
class IFilter
{
public:
    virtual ~IFilter() = default;

    virtual void prepare(double sampleRate) = 0;
    virtual void reset()                    = 0;
    virtual float process(float input)      = 0;
    virtual void setCutoff(float cutoffHz)  = 0;
    virtual void setResonance(float res)    = 0;

    // Optional topology selector — only meaningful for SVF.
    // 0=LP 1=HP 2=BP 3=Notch  (matches SynthParams::filterType)
    virtual void setMode(int /*mode*/) {}
};

// ---------------------------------------------------------------------------
// Moog-style ZDF Ladder filter (4-stage TPT with non-linear feedback)
//
// Based on Zavalishin "The Art of VA Filter Design" (2018).
// Each stage is a first-order TPT 1-pole LP.  Non-linear tanh saturation
// appears in the feedback path and at the input, giving the characteristic
// warm, self-oscillating Moog sound.
//
// setCutoff and setResonance are safe to call every sample (audio-rate
// modulation). g_ = tan(pi*fc/sr)/(1+tan(pi*fc/sr)) is the TPT gain.
// ---------------------------------------------------------------------------
class LadderFilter final : public IFilter
{
public:
    void prepare(double sr) override
    {
        sampleRate_ = sr;
        // Recompute g_ with new sample rate
        const float fc = juce::jlimit(20.0f, (float)(sampleRate_ * 0.499), lastCutoff_);
        const float g  = std::tan(juce::MathConstants<float>::pi * fc / (float)sampleRate_);
        g_ = g / (1.0f + g);
    }

    void reset() override
    {
        s_[0] = s_[1] = s_[2] = s_[3] = 0.0f;
    }

    void setCutoff(float hz) override
    {
        // Clamp and precompute TPT gain: G = tan(pi*fc/sr) / (1 + tan(pi*fc/sr))
        lastCutoff_ = hz;
        const float fc = juce::jlimit(20.0f, (float)(sampleRate_ * 0.499), hz);
        const float g  = std::tan(juce::MathConstants<float>::pi * fc / (float)sampleRate_);
        g_ = g / (1.0f + g);
    }

    void setResonance(float res) override
    {
        // Map normalised [0..1.2] → feedback gain [0..4.8].
        // Self-oscillation onset is at k ≈ 4.0.
        res_ = juce::jlimit(0.0f, 1.2f, res) * 4.0f;
    }

    float process(float x) override
    {
        // Non-linear input stage: subtract resonance * tanh(last stage)
        const float fb = std::tanh(s_[3]);
        float y        = std::tanh(x - res_ * fb);

        // 4 first-order TPT 1-pole stages in cascade
        for (int k = 0; k < 4; ++k)
        {
            const float v = (y - s_[k]) * g_;
            y      = v + s_[k];
            s_[k]  = y + v;          // s_new = s_old + 2*v
        }

        // Flush denormals (critical for CPU on sustained notes)
        for (auto& s : s_) s += 1.0e-20f;

        return y;
    }

private:
    double sampleRate_ = 44100.0;
    float  g_          = 0.0f;      // TPT gain G = tan(pi*fc/sr) / (1+tan(pi*fc/sr))
    float  res_        = 0.0f;      // resonance feedback gain [0..4.8]
    float  lastCutoff_ = 1000.0f;   // cached for prepare() re-initialisation
    float  s_[4]       = {};        // 4 integrator states
};

// ---------------------------------------------------------------------------
// SVF output topology selector
// ---------------------------------------------------------------------------
enum class SVFMode { LowPass = 0, HighPass = 1, BandPass = 2, Notch = 3 };

// ---------------------------------------------------------------------------
// Zero-Delay Feedback State Variable Filter (ZDF SVF)
//
// Based on Zavalishin "The Art of VA Filter Design" (2018), Ch. 4.
// Computes LP, HP, BP and Notch outputs simultaneously from two integrator
// states (ic1eq, ic2eq).  All four outputs are available every sample;
// the active one is selected by SVFMode.
//
// Characteristics:
//  - Linear phase, clean transient response (unlike the Ladder)
//  - Resonance via damping coefficient k = 1/Q
//  - Q = 0.5 + resonance * 10  →  k [2.0 … 0.095]
//  - Unconditionally stable under ZDF formulation
//  - No saturation — intentionally clean; drive the input externally
// ---------------------------------------------------------------------------
class SVFFilter final : public IFilter
{
public:
    // --- IFilter interface --------------------------------------------------

    void setMode(int mode) override
    {
        mode_ = static_cast<SVFMode>(juce::jlimit(0, 3, mode));
    }

    void prepare(double sr) override
    {
        sampleRate_ = sr;
        // Recompute g with new sample rate using last known cutoff
        const float fc = juce::jlimit(20.0f, (float)(sampleRate_ * 0.499), lastCutoff_);
        g_ = std::tan(juce::MathConstants<float>::pi * fc / (float)sampleRate_);
    }

    void reset() override
    {
        ic1eq_ = ic2eq_ = 0.0f;
    }

    void setCutoff(float hz) override
    {
        // g = tan(pi * fc / sr)  — prewarped angular frequency
        lastCutoff_ = hz;
        const float fc = juce::jlimit(20.0f, (float)(sampleRate_ * 0.499), hz);
        g_ = std::tan(juce::MathConstants<float>::pi * fc / (float)sampleRate_);
    }

    void setResonance(float res) override
    {
        // Map normalised resonance [0..1] to Q [0.5 .. 10.5] → k = 1/Q
        // k = 2 (fully damped) .. k ≈ 0.095 (near self-oscillation)
        const float Q = 0.5f + juce::jlimit(0.0f, 1.0f, res) * 10.0f;
        k_ = 1.0f / Q;
    }

    float process(float x) override
    {
        // ZDF SVF core — Zavalishin "The Art of VA Filter Design" (2018) §4.4
        //
        //   v1 = (g*(x - ic2eq) + ic1eq) / (1 + g*(g+k))
        //   v2 = ic2eq + g*v1
        //   ic1eq = 2*v1 - ic1eq
        //   ic2eq = 2*v2 - ic2eq
        //
        // LP=v2, BP=v1, HP=x-k*v1-v2, Notch=LP+HP

        const float denom = 1.0f + g_ * (g_ + k_);

        // Guard against degenerate denominator (should never happen with ZDF
        // but prevents NaN if sample rate or cutoff are temporarily invalid)
        if (denom < 1.0e-10f) return x;

        const float v1 = (g_ * (x - ic2eq_) + ic1eq_) / denom;
        const float v2 = ic2eq_ + g_ * v1;

        // Update integrator states (trapezoidal rule)
        ic1eq_ = 2.0f * v1 - ic1eq_;
        ic2eq_ = 2.0f * v2 - ic2eq_;

        // Flush denormals — sustained silence would otherwise accumulate FP work
        ic1eq_ += 1.0e-20f;
        ic2eq_ += 1.0e-20f;

        // Route to selected output
        switch (mode_)
        {
            case SVFMode::LowPass:  return v2;
            case SVFMode::HighPass: return x - k_ * v1 - v2;
            case SVFMode::BandPass: return v1;
            case SVFMode::Notch:    return v2 + (x - k_ * v1 - v2);  // LP + HP
        }
        return v2; // default LP
    }

private:
    double  sampleRate_ = 44100.0;
    float   g_          = 0.0f;              // tan(pi*fc/sr)
    float   k_          = 1.0f;              // damping = 1/Q
    float   lastCutoff_ = 1000.0f;           // cached for prepare() recompute
    float   ic1eq_      = 0.0f;              // integrator state 1
    float   ic2eq_      = 0.0f;              // integrator state 2
    SVFMode mode_       = SVFMode::LowPass;
};

// ---------------------------------------------------------------------------
// Filter factory — returns a new instance for the given mode.
// Falls back to Ladder if mode is not yet implemented.
// ---------------------------------------------------------------------------
inline std::unique_ptr<IFilter> createFilter(FilterMode mode)
{
    switch (mode)
    {
        case FilterMode::Ladder:  return std::make_unique<LadderFilter>();
        case FilterMode::SVF:     return std::make_unique<SVFFilter>();
        case FilterMode::Diode:   return std::make_unique<LadderFilter>(); // placeholder
    }
    return std::make_unique<LadderFilter>();
}

// =============================================================================

class SynthVoice
{
public:
    // Trigger a note. noteLenSamples: auto-release after this many samples.
    void noteOn(int midiPitch, float velocity, double sr, const SynthParams& p, int noteLenSamples,
                int voiceSlot = 0, float outputGain = 1.0f, float outputPan = 0.0f, int mixerTrack = 0)
    {
        beginResidualFade();
        noteOnFadeRemaining_ = kNoteOnFadeSamples;
        pitch_      = midiPitch;
        velocity_   = velocity;
        params_     = p;
        voiceSlot_  = voiceSlot;
        frequency_  = 440.0 * std::pow(2.0, (midiPitch - 69) / 12.0);
        const float startJitter = SynthVoicing::stableHash01(voiceSlot * 131 + 11);
        const float subJitter   = SynthVoicing::stableHash01(voiceSlot * 223 + 97);
        const float driftJitter = SynthVoicing::stableHash01(voiceSlot * 283 + 149);
        subPhase_  = subJitter;
        if (!p.lfoFreeRun) lfoPhase_ = 0.0;
        prevLfoPhase_ = lfoPhase_;
        lfoSHValue_   = 0.0f;
        voicePan_ = juce::jlimit(-0.22f, 0.22f,
                                 ((float)voiceSlot - 3.5f) * 0.055f + (startJitter - 0.5f) * 0.05f);
        // Static ±0.8-cent base drift (unique per voice slot, independent of driftDepth)
        const float driftCents = (driftJitter - 0.5f) * 1.6f;
        driftRatio_ = SynthVoicing::detuneRatioFromCents(driftCents);

        // Slow drift oscillator — triangle LFO at 0.15–0.30 Hz, phase randomised
        slowDriftPhase_ = SynthVoicing::stableHash01(voiceSlot * 317 + 71);
        slowDriftRate_  = 0.15f + SynthVoicing::stableHash01(voiceSlot * 401 + 29) * 0.15f;

        // Oscillator / unison setup
        oscWaveform_ = (p.waveform == 6) ? 1 : p.waveform;   // supersaw uses saw internally
        pulseWidth_  = juce::jlimit(0.05f, 0.95f, p.pulseWidth);
        noiseSeed_   = (uint32_t)(voiceSlot * 2654435761u + 1u);

        if (p.waveform == 6)   // Supersaw — 7 fixed-detune voices (Roland JP style)
        {
            activeUnison_ = 7;
            static constexpr float kSSCents[] = { -14.4f, -8.8f, -3.5f, 0.0f, 3.5f, 8.8f, 14.4f };
            static constexpr float kSSPan[]   = { -0.85f, -0.55f, -0.22f, 0.0f, 0.22f, 0.55f, 0.85f };
            for (int u = 0; u < 7; ++u)
            {
                oscStack_[u].phase = SynthVoicing::stableHash01(voiceSlot * 131 + u * 37 + 11);
                oscStack_[u].detuneRatio = SynthVoicing::detuneRatioFromCents(kSSCents[u]);
                const float pa = (kSSPan[u] + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                oscStack_[u].panL = std::cos(pa);
                oscStack_[u].panR = std::sin(pa);
                oscStack_[u].driftOffsetCents = (SynthVoicing::stableHash01(voiceSlot * 491 + u * 53 + 7) - 0.5f) * 2.0f;
            }
        }
        else if (juce::jlimit(1, kMaxUnison, p.unisonVoices) == 1)
        {
            activeUnison_ = 1;
            oscStack_[0].phase = startJitter;
            oscStack_[0].detuneRatio = 1.0f;
            oscStack_[0].panL = 1.0f;
            oscStack_[0].panR = 1.0f;
            oscStack_[0].driftOffsetCents = (SynthVoicing::stableHash01(voiceSlot * 491 + 7) - 0.5f) * 2.0f;
        }
        else
        {
            activeUnison_ = juce::jlimit(1, kMaxUnison, p.unisonVoices);
            const float halfDetune = p.unisonDetune * 0.5f;
            const float halfN      = (float)(activeUnison_ - 1) * 0.5f;
            for (int u = 0; u < activeUnison_; ++u)
            {
                oscStack_[u].phase = SynthVoicing::stableHash01(voiceSlot * 131 + u * 37 + 11);
                const float spread = (halfN > 0.0f) ? ((float)u - halfN) / halfN : 0.0f;  // -1..+1
                oscStack_[u].detuneRatio = SynthVoicing::detuneRatioFromCents(spread * halfDetune);
                const float panPos = spread * p.unisonSpread;
                const float pa     = (panPos + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                oscStack_[u].panL  = std::cos(pa);
                oscStack_[u].panR  = std::sin(pa);
                oscStack_[u].driftOffsetCents = (SynthVoicing::stableHash01(voiceSlot * 491 + u * 53 + 7) - 0.5f) * 2.0f;
            }
        }

        transientPunch_ = 1.0f + velocity * 0.20f
                        + ((p.waveform == 1 || p.waveform == 2) ? 0.08f : 0.0f);

        const float sr_f = static_cast<float>(sr);
        attackCoeff_  = envCoeff(envTimeMs(ADSRRange::attackNorm (p.attack),  0.5f,  5000.0f), sr_f);
        decayCoeff_   = envCoeff(envTimeMs(ADSRRange::decayNorm  (p.decay),   1.0f,  8000.0f), sr_f);
        releaseCoeff_ = envCoeff(envTimeMs(ADSRRange::releaseNorm(p.release), 5.0f, 15000.0f), sr_f);
        sustainLvl_         = p.sustain;
        sustainSmoothed_    = p.sustain;    // no lag at note start
        sustainSmoothCoeff_ = 1.0f - std::exp(-1000.0f / (8.0f * sr_f));  // 8ms smoothing
        lfoFadeLevel_       = (p.lfoFadeIn < 1.0f) ? 1.0f : 0.0f;
        lfoFadeInc_         = (p.lfoFadeIn < 1.0f) ? 0.0f : 1.0f / (p.lfoFadeIn * 0.001f * sr_f);
        cutoffSmoothed_ = params_.cutoff;
        resonanceSmoothed_ = params_.resonance;
        pitchModSmoothed_ = 1.0f;
        lfoSmoothed_ = 0.0f;
        outputGain_ = juce::jlimit(0.0f, 1.25f, outputGain);
        outputPan_ = juce::jlimit(-1.0f, 1.0f, outputPan);
        mixerTrack_ = juce::jlimit(0, 7, mixerTrack);

        envState_ = Env::Attack;
        envLevel_ = 0.0f;

        noteLenRemaining_ = noteLenSamples;

        // Create or reset filter instances
        // (allocation on note-on, never in the audio loop)
        const FilterMode wantedMode = (params_.filterMode == 1) ? FilterMode::SVF
                                                                 : FilterMode::Ladder;
        // Recreate if mode changed (e.g. user switched Ladder ↔ SVF between notes)
        const bool wrongType = (filterL_ == nullptr)
            || (wantedMode == FilterMode::SVF   && dynamic_cast<SVFFilter*>   (filterL_.get()) == nullptr)
            || (wantedMode == FilterMode::Ladder && dynamic_cast<LadderFilter*>(filterL_.get()) == nullptr);

        if (wrongType)
        {
            filterL_ = createFilter(wantedMode);
            filterR_ = createFilter(wantedMode);
        }
        filterL_->prepare(sr);
        filterR_->prepare(sr);
        // Topology (LP/HP/BP/Notch) — meaningful for SVF, no-op for Ladder
        filterL_->setMode(params_.filterType);
        filterR_->setMode(params_.filterType);
        filterL_->setCutoff(params_.cutoff);
        filterL_->setResonance(params_.resonance);
        filterR_->setCutoff(params_.cutoff);
        filterR_->setResonance(params_.resonance);
        filterL_->reset();
        filterR_->reset();

        // Clear biquad state (used for HP/BP paths)
        z1L_ = z2L_ = z1R_ = z2R_ = 0.0f;
    }

    void noteOff()
    {
        if (envState_ != Env::Idle)
            envState_ = Env::Release;   // release decays naturally from current envLevel_
    }

    void kill()
    {
        beginResidualFade();
        envState_ = Env::Idle;
        envLevel_ = 0.0f;
    }

    bool isActive()  const { return envState_ != Env::Idle; }
    bool isRenderable() const { return envState_ != Env::Idle || residualFadeRemaining_ > 0; }
    int  getPitch()  const { return pitch_; }
    int  getMixerTrack() const { return mixerTrack_; }
    float getStealPriority() const
    {
        const float residualLevel = residualFadeRemaining_ > 0
            ? (float)residualFadeRemaining_ / (float)kResidualFadeSamples * 0.05f
            : 0.0f;
        const float envWeight = (envState_ == Env::Release) ? 0.35f : 1.0f;
        return envLevel_ * envWeight + residualLevel;
    }

    void renderAdd(float* L, float* R, int numSamples, double sr)
    {
        if (envState_ == Env::Idle && residualFadeRemaining_ <= 0) return;

        // Biquad coefficients — only used when Ladder mode + HP/BP (filterType 1,2).
        // SVF handles all topologies internally; Ladder is LP-only.
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        const bool useBiquad = (params_.filterMode == 0 && params_.filterType != 0);
        if (useBiquad)
            computeFilter(params_.cutoff, params_.resonance, params_.filterType, sr, b0, b1, b2, a1, a2);

        const double lfoInc = params_.lfoRate / sr;

        for (int i = 0; i < numSamples; ++i)
        {
            float outL = 0.0f;
            float outR = 0.0f;
            float synthOutL = 0.0f;
            float synthOutR = 0.0f;

            if (residualFadeRemaining_ > 0)
            {
                const float fade = (float)residualFadeRemaining_ / (float)kResidualFadeSamples;
                outL += residualTailL_ * fade;
                outR += residualTailR_ * fade;
                if (--residualFadeRemaining_ == 0)
                    residualTailL_ = residualTailR_ = 0.0f;
            }

            // Auto-release after note length
            if (noteLenRemaining_ > 0)
            {
                if (--noteLenRemaining_ == 0 &&
                    envState_ != Env::Release && envState_ != Env::Idle)
                    noteOff();
            }

            // ADSR envelope — one-pole exponential segments
            switch (envState_)
            {
                case Env::Attack:
                    // Target slightly above 1.0 so the curve reliably reaches 1.0
                    envLevel_ += (1.0f + kAttackOvershoot - envLevel_) * (1.0f - attackCoeff_);
                    if (envLevel_ >= 1.0f) { envLevel_ = 1.0f; envState_ = Env::Decay; }
                    break;
                case Env::Decay:
                    sustainSmoothed_ += (sustainLvl_ - sustainSmoothed_) * sustainSmoothCoeff_;
                    envLevel_ += (sustainSmoothed_ - envLevel_) * (1.0f - decayCoeff_);
                    if (std::abs(envLevel_ - sustainSmoothed_) < 0.001f)
                        { envLevel_ = sustainSmoothed_; envState_ = Env::Sustain; }
                    break;
                case Env::Sustain:
                    // Smooth sustain-level changes to avoid zipper noise
                    sustainSmoothed_ += (sustainLvl_ - sustainSmoothed_) * sustainSmoothCoeff_;
                    envLevel_ = sustainSmoothed_;
                    break;
                case Env::Release:
                    envLevel_ *= releaseCoeff_;
                    if (envLevel_ <= 0.00005f) { envLevel_ = 0.0f; envState_ = Env::Idle; }
                    break;
                case Env::Idle:
                    break;
            }

            if (envState_ != Env::Idle)
            {
                // --- LFO ---
                float lfoRaw = 0.0f;
                if (params_.lfoDepth > 0.0f)
                {
                    const float lp = (float)lfoPhase_;
                    switch (params_.lfoWaveform)
                    {
                        case 0: lfoRaw = std::sin(lp * juce::MathConstants<float>::twoPi); break;
                        case 1: lfoRaw = (lp < 0.5f) ? (4.0f * lp - 1.0f) : (3.0f - 4.0f * lp); break;
                        case 2: lfoRaw = 2.0f * lp - 1.0f; break;
                        case 3: lfoRaw = (lp < 0.5f) ? 1.0f : -1.0f; break;
                        case 4:  // Sample & Hold — new random value on phase wrap
                            if (lfoPhase_ < prevLfoPhase_) lfoSHValue_ = nextNoise();
                            lfoRaw = lfoSHValue_;
                            break;
                        default: break;
                    }
                    prevLfoPhase_ = lfoPhase_;
                    lfoPhase_ += lfoInc;
                    if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
                    if (lfoFadeLevel_ < 1.0f)
                        lfoFadeLevel_ = juce::jmin(1.0f, lfoFadeLevel_ + lfoFadeInc_);
                    lfoRaw *= lfoFadeLevel_;
                }
                lfoSmoothed_ = SynthVoicing::smoothValue(lfoSmoothed_, lfoRaw, 0.992f);

                // --- Frequency + pitch LFO ---
                double freq = frequency_ * driftRatio_;
                if (params_.lfoDepth > 0.0f && params_.lfoTarget == 1)
                {
                    const float pitchModTarget = std::pow(2.0f, (lfoSmoothed_ * params_.lfoDepth * 1.5f) / 12.0f);
                    pitchModSmoothed_ = SynthVoicing::smoothValue(pitchModSmoothed_, pitchModTarget, 0.994f);
                    freq *= pitchModSmoothed_;
                }
                else
                {
                    pitchModSmoothed_ = SynthVoicing::smoothValue(pitchModSmoothed_, 1.0f, 0.990f);
                    freq *= pitchModSmoothed_;
                }

                // --- Pulse width LFO (target 3) ---
                float currentPW = pulseWidth_;
                if (params_.lfoDepth > 0.0f && params_.lfoTarget == 3 && oscWaveform_ == 4)
                    currentPW = juce::jlimit(0.05f, 0.95f, pulseWidth_ + lfoSmoothed_ * params_.lfoDepth * 0.40f);

                // --- Filter cutoff + resonance modulation ---
                const float cutoffKeyTrack = 1.0f + juce::jlimit(-0.08f, 0.24f, ((float)pitch_ - 60.0f) * 0.0085f);
                const float envOpen = std::pow(juce::jlimit(0.0f, 1.0f, envLevel_), 0.42f);
                const float cutoffNorm = SynthVoicing::hzToNormalized(params_.cutoff);
                const float filterEnvAmt = juce::jlimit(-1.0f, 1.0f, params_.filterEnvAmount);
                // Positive depth: envelope opens filter above base cutoff
                // Negative depth: envelope closes filter below base cutoff
                const float envContrib = envOpen * filterEnvAmt * 0.55f
                                       + velocity_ * juce::jlimit(0.0f, 1.0f, filterEnvAmt) * 0.10f;
                float modCutoffNorm = std::pow(cutoffNorm, 0.92f) + envContrib;
                if (params_.lfoDepth > 0.0f && params_.lfoTarget == 0)
                    modCutoffNorm += lfoSmoothed_ * params_.lfoDepth * 0.085f;
                modCutoffNorm = juce::jlimit(0.0f, 1.0f, modCutoffNorm);
                const float noteAwareFloor = juce::jlimit(55.0f, 2600.0f,
                                                          (float)frequency_ * (1.75f + envOpen * 1.35f));
                const float cutoffTarget = juce::jlimit(noteAwareFloor, 20000.0f,
                                                        SynthVoicing::normalizedToHz(modCutoffNorm) * cutoffKeyTrack);
                cutoffSmoothed_    = SynthVoicing::smoothValue(cutoffSmoothed_, cutoffTarget, 0.985f);
                const float resonanceTarget = juce::jlimit(0.03f, 0.96f,
                                                           std::pow(params_.resonance, 0.78f)
                                                         * (0.92f + (1.0f - envOpen) * 0.08f));
                resonanceSmoothed_ = SynthVoicing::smoothValue(resonanceSmoothed_, resonanceTarget, 0.989f);

                // Update IFilter (Ladder LP or SVF all-topologies)
                const bool useIFilter = (filterL_ != nullptr)
                    && (params_.filterMode == 1 || params_.filterType == 0);

                if (useIFilter)
                {
                    filterL_->setCutoff(cutoffSmoothed_);
                    filterL_->setResonance(resonanceSmoothed_);
                    filterR_->setCutoff(cutoffSmoothed_);
                    filterR_->setResonance(resonanceSmoothed_);
                }
                else if (useBiquad)
                {
                    computeFilter(cutoffSmoothed_, resonanceSmoothed_, params_.filterType, sr, b0, b1, b2, a1, a2);
                }

                // --- Amplitude LFO — tremolo (target 2) ---
                float ampMod = 1.0f;
                if (params_.lfoDepth > 0.0f && params_.lfoTarget == 2)
                    ampMod = juce::jlimit(0.0f, 1.0f,
                                         1.0f - (lfoSmoothed_ * 0.5f + 0.5f) * params_.lfoDepth);

                // --- Slow oscillator drift — triangle LFO per voice ---
                // Advance phase; value is -1..+1 triangle wave scaled by driftDepth
                slowDriftPhase_ += slowDriftRate_ / (float)sr;
                if (slowDriftPhase_ >= 1.0f) slowDriftPhase_ -= 1.0f;
                const float slowDriftRaw = (slowDriftPhase_ < 0.5f)
                    ? (4.0f * slowDriftPhase_ - 1.0f)
                    : (3.0f - 4.0f * slowDriftPhase_);   // -1..+1
                // Total drift = static base (driftRatio_) * slow modulation
                // slowAmt: 0 = no slow drift, 1 = ±5 cents variation
                const float slowAmt = params_.driftDepth * 5.0f;

                // --- Unison oscillator stack ---
                const double dtBase = freq / sr;
                const double dtSub  = juce::jlimit(0.0, 0.49, (freq * 0.5) / sr);
                subPhase_ += dtSub;
                if (subPhase_ >= 1.0) subPhase_ -= 1.0;

                float rawSampleL = 0.0f, rawSampleR = 0.0f;
                if (oscWaveform_ == 5)   // White noise — no phase oscillation
                {
                    rawSampleL = rawSampleR = nextNoise();
                }
                else
                {
                    const float uniGain = 1.0f / std::sqrt((float)juce::jmax(1, activeUnison_));
                    for (int u = 0; u < activeUnison_; ++u)
                    {
                        // Combine static detune + slow drift (unique direction per osc unit)
                        const float perOscDrift = slowDriftRaw * oscStack_[u].driftOffsetCents * slowAmt * 0.5f;
                        const double dt = dtBase * (double)oscStack_[u].detuneRatio
                                        * (double)SynthVoicing::detuneRatioFromCents(perOscDrift);
                        oscStack_[u].phase += dt;
                        if (oscStack_[u].phase >= 1.0) oscStack_[u].phase -= 1.0;
                        const float s = generateSample((float)oscStack_[u].phase, dt, oscWaveform_, currentPW);
                        rawSampleL += s * oscStack_[u].panL * uniGain;
                        rawSampleR += s * oscStack_[u].panR * uniGain;
                    }
                    const float subOsc = generateSubSample((float)subPhase_, oscWaveform_);
                    const float subMix = getSubMix(oscWaveform_);
                    rawSampleL += subOsc * subMix;
                    rawSampleR += subOsc * subMix;
                }

                // --- Perceptual scaling + drive ---
                const float pitchDelta    = juce::jmax(0.0f, juce::jmin(1.0f, (60.0f - (float)pitch_) / 36.0f));
                const float loudnessComp  = std::pow(2.0f, pitchDelta * 0.65f);
                const float transientDrive = 1.0f + (1.0f - envLevel_) * (0.18f + velocity_ * 0.12f)
                                           + (envOpen * 0.08f);
                const float harmonicDrive  = 1.12f + resonanceSmoothed_ * 0.85f
                                           + ((oscWaveform_ == 1 || oscWaveform_ == 2 || oscWaveform_ == 4) ? 0.18f : 0.0f);
                const float driveGain     = 1.0f + params_.filterDrive * 3.0f;
                const float envVelGain    = envLevel_ * velocity_ * 0.28f
                                          * transientPunch_ * outputGain_ * loudnessComp * ampMod;

                const float sampleL = SynthVoicing::softClip(rawSampleL * harmonicDrive * transientDrive * driveGain) * envVelGain;
                const float sampleR = SynthVoicing::softClip(rawSampleR * harmonicDrive * transientDrive * driveGain) * envVelGain;

                // --- Filter routing ---
                // IFilter path: Moog Ladder (LP) or SVF (LP/HP/BP/Notch)
                // Biquad path:  Ladder mode + HP/BP only (legacy, will be removed when SVF is used)
                float filteredL, filteredR;
                if (useIFilter)
                {
                    filteredL = filterL_->process(sampleL);
                    filteredR = filterR_->process(sampleR);
                }
                else
                {
                    // RBJ biquad — Ladder mode HP/BP only
                    const float resonanceBoost = 1.0f + resonanceSmoothed_ * 0.30f;
                    filteredL = SynthVoicing::softClip((b0 * sampleL + z1L_) * resonanceBoost);
                    z1L_ = b1 * sampleL - a1 * filteredL + z2L_;
                    z2L_ = b2 * sampleL - a2 * filteredL;
                    filteredR = SynthVoicing::softClip((b0 * sampleR + z1R_) * resonanceBoost);
                    z1R_ = b1 * sampleR - a1 * filteredR + z2R_;
                    z2R_ = b2 * sampleR - a2 * filteredR;
                }

                // Air high-frequency presence blend
                const float air = juce::jlimit(0.0f, 0.22f,
                                               ((cutoffSmoothed_ - 1800.0f) / 12000.0f) * 0.18f
                                              + (oscWaveform_ == 3 ? 0.03f : 0.0f));
                const float outL = SynthVoicing::softClip(filteredL + (rawSampleL - filteredL) * air);
                const float outR = SynthVoicing::softClip(filteredR + (rawSampleR - filteredR) * air);

                // Voice slot + mixer pan applied as balance (preserves unison stereo width)
                const float finalPan = juce::jlimit(-1.0f, 1.0f, voicePan_ + outputPan_ * 0.85f);
                const float balL = (finalPan < 0.0f) ? 1.0f : 1.0f - finalPan;
                const float balR = (finalPan > 0.0f) ? 1.0f : 1.0f + finalPan;
                synthOutL = outL * balL;
                synthOutR = outR * balR;
            }

            if (noteOnFadeRemaining_ > 0)
            {
                const float fadeIn = 1.0f - ((float)noteOnFadeRemaining_ / (float)kNoteOnFadeSamples);
                synthOutL *= fadeIn;
                synthOutR *= fadeIn;
                --noteOnFadeRemaining_;
            }

            outL += synthOutL;
            outR += synthOutR;
            L[i] += outL;
            R[i] += outR;
            lastOutputL_ = outL;
            lastOutputR_ = outR;
        }
    }

private:
    enum class Env { Idle, Attack, Decay, Sustain, Release };

    int    pitch_             = 60;
    float  velocity_          = 0.8f;
    double frequency_         = 440.0;
    double subPhase_          = 0.0;
    double lfoPhase_          = 0.0;
    double prevLfoPhase_      = 0.0;    // S&H edge detection

    Env    envState_           = Env::Idle;
    float  envLevel_           = 0.0f;
    float  attackCoeff_        = 0.0f;   // one-pole coefficient: close to 1 = slow, 0 = instant
    float  decayCoeff_         = 0.0f;
    float  releaseCoeff_       = 0.0f;
    float  sustainLvl_         = 0.7f;   // target sustain (set by UI)
    float  sustainSmoothed_    = 0.7f;   // smoothed sustain (anti-zipper)
    float  sustainSmoothCoeff_ = 0.002f;
    static constexpr float kAttackOvershoot = 0.0015f;
    SynthParams params_       = {};

    // Unison oscillator stack
    struct OscUnit { double phase = 0.0; float detuneRatio = 1.0f; float panL = 1.0f; float panR = 1.0f; float driftOffsetCents = 0.0f; };
    static constexpr int kMaxUnison = 8;
    OscUnit  oscStack_[kMaxUnison];
    int      activeUnison_    = 1;
    int      oscWaveform_     = 1;    // actual waveform index (1=saw for supersaw)
    float    pulseWidth_      = 0.5f; // modulated per sample by LFO target 3

    // Noise generator (LCG, per-voice seed)
    mutable uint32_t noiseSeed_ = 0x12345678u;
    float nextNoise() const noexcept
    {
        noiseSeed_ = noiseSeed_ * 1664525u + 1013904223u;
        return (float)(int32_t)noiseSeed_ * (1.0f / 2147483648.0f);
    }

    float  driftRatio_        = 1.0f;
    float  slowDriftPhase_    = 0.0f;   // 0..1 phase of slow drift triangle LFO
    float  slowDriftRate_     = 0.20f;  // Hz (randomised per voice: 0.15–0.30)
    float  transientPunch_    = 1.0f;
    float  voicePan_          = 0.0f;
    float  outputGain_        = 1.0f;
    float  outputPan_         = 0.0f;
    int    mixerTrack_        = 0;
    float  cutoffSmoothed_    = 4000.0f;
    float  resonanceSmoothed_ = 0.3f;
    float  pitchModSmoothed_  = 1.0f;
    float  lfoSmoothed_       = 0.0f;
    float  lfoSHValue_        = 0.0f;  // S&H held value
    float  lfoFadeLevel_      = 1.0f;  // 0..1 ramp for fade-in
    float  lfoFadeInc_        = 0.0f;  // per-sample increment
    int    voiceSlot_         = 0;

    int    noteLenRemaining_  = 0;

    // Ladder filter instances — one per stereo channel (LP path)
    std::unique_ptr<IFilter> filterL_;
    std::unique_ptr<IFilter> filterR_;

    // Biquad filter state — used for HP/BP paths (filterType 1, 2)
    float  z1L_ = 0.0f, z2L_ = 0.0f;
    float  z1R_ = 0.0f, z2R_ = 0.0f;
    float  lastOutputL_ = 0.0f, lastOutputR_ = 0.0f;
    float  residualTailL_ = 0.0f, residualTailR_ = 0.0f;
    int    residualFadeRemaining_ = 0;
    int    noteOnFadeRemaining_ = 0;
    static constexpr int kResidualFadeSamples = 64;
    static constexpr int kNoteOnFadeSamples = 96;

    void beginResidualFade()
    {
        if (std::abs(lastOutputL_) > 1.0e-5f || std::abs(lastOutputR_) > 1.0e-5f)
        {
            residualTailL_ = lastOutputL_;
            residualTailR_ = lastOutputR_;
            residualFadeRemaining_ = kResidualFadeSamples;
        }
        else
        {
            residualTailL_ = residualTailR_ = 0.0f;
            residualFadeRemaining_ = 0;
        }
    }

    // Polynomial Band-Limited Step function — corrects the discontinuity
    // at phase wrap-around (and at 0.5 for square). t = phase, dt = phase increment.
    static float polyBLEP(double t, double dt)
    {
        if (dt <= 0.0) return 0.0f;
        if (t < dt)           // just after the discontinuity
        {
            const double x = t / dt;
            return (float)(2.0 * x - x * x - 1.0);
        }
        if (t > 1.0 - dt)    // just before the next discontinuity
        {
            const double x = (t - 1.0) / dt;
            return (float)(x * x + 2.0 * x + 1.0);
        }
        return 0.0f;
    }

    // pw = pulse width [0.05..0.95], only used for waveform 4
    float generateSample(float p, double dt, int waveform, float pw = 0.5f) const
    {
        switch (waveform)
        {
            case 0: return std::sin(p * juce::MathConstants<float>::twoPi);
            case 1:  // Sawtooth + PolyBLEP
            {
                float saw = 2.0f * p - 1.0f;
                saw -= polyBLEP((double)p, dt);
                return saw;
            }
            case 2:  // Square + PolyBLEP
            {
                float sq = (p < 0.5f) ? 1.0f : -1.0f;
                sq += polyBLEP((double)p, dt);
                sq -= polyBLEP(std::fmod((double)p + 0.5, 1.0), dt);
                return sq;
            }
            case 3: return 1.0f - 4.0f * std::abs(p - 0.5f);  // Triangle — band-limited
            case 4:  // Pulse with variable pulse width + PolyBLEP
            {
                float sq = (p < pw) ? 1.0f : -1.0f;
                sq += polyBLEP((double)p, dt);
                sq -= polyBLEP(std::fmod((double)p + (1.0 - (double)pw), 1.0), dt);
                // DC offset compensation: subtract mean value (2*pw-1)
                return sq - (2.0f * pw - 1.0f);
            }
            default: return 0.0f;
        }
    }

    static float getSubMix(int waveform) noexcept
    {
        switch (waveform)
        {
            case 1: return 0.22f;   // saw
            case 2: return 0.28f;   // square
            case 3: return 0.14f;   // triangle
            case 4: return 0.20f;   // pulse
            default: return 0.0f;   // sine, noise, supersaw — no sub
        }
    }

    float generateSubSample(float p, int waveform) const
    {
        switch (waveform)
        {
            case 2:
            case 3:
                return 1.0f - 4.0f * std::abs(p - 0.5f);
            case 1:
                return std::sin(p * juce::MathConstants<float>::twoPi);
            default:
                return 0.0f;
        }
    }

    // Robert Bristow-Johnson biquad coefficients — LP / HP / BP
    void computeFilter(float cutoff, float resonance, int filterType, double sr,
                       float& b0, float& b1, float& b2, float& a1, float& a2) const
    {
        const float f     = juce::jlimit(20.0f, (float)(sr * 0.499), cutoff);
        const float Q     = 0.5f + resonance * 3.5f;          // Q: 0.5 – 4.0
        const float w0    = juce::MathConstants<float>::twoPi * f / (float)sr;
        const float cosW0 = std::cos(w0);
        const float sinW0 = std::sin(w0);
        const float alpha = sinW0 / (2.0f * Q);
        const float a0inv = 1.0f / (1.0f + alpha);

        // Denominator coefficients are identical for all types
        a1 = (-2.0f * cosW0) * a0inv;
        a2 = (1.0f - alpha)  * a0inv;

        switch (filterType)
        {
            case 1:  // High-pass (RBJ)
                b0 = ((1.0f + cosW0) * 0.5f) * a0inv;
                b1 = -(1.0f + cosW0)          * a0inv;
                b2 = b0;
                break;
            case 2:  // Band-pass (constant 0 dB peak gain, RBJ)
                b0 = (sinW0 * 0.5f) * a0inv;
                b1 = 0.0f;
                b2 = -b0;
                break;
            default: // Low-pass (existing)
                b0 = ((1.0f - cosW0) * 0.5f) * a0inv;
                b1 = (1.0f - cosW0)           * a0inv;
                b2 = b0;
                break;
        }
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
    void noteOn(int pitch, float velocity, double sr, const SynthParams& p, int noteLenSamples,
                float outputGain = 1.0f, float outputPan = 0.0f, int mixerTrack = 0)
    {
        if (!p.enabled) return;

        int voiceIdx = -1;
        for (int i = 0; i < kNumVoices; ++i)
            if (!voices_[i].isRenderable()) { voiceIdx = i; break; }

        if (voiceIdx < 0)  // voice stealing — round-robin oldest
        {
            float lowestPriority = std::numeric_limits<float>::max();
            for (int i = 0; i < kNumVoices; ++i)
            {
                const float priority = voices_[i].getStealPriority();
                if (priority < lowestPriority)
                {
                    lowestPriority = priority;
                    voiceIdx = i;
                }
            }
            if (voiceIdx < 0)
            {
                voiceIdx = stealIdx_;
                stealIdx_ = (stealIdx_ + 1) % kNumVoices;
            }
        }

        voices_[voiceIdx].noteOn(pitch, velocity, sr, p, noteLenSamples, voiceIdx, outputGain, outputPan, mixerTrack);
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
            v.kill();
    }

    bool isAnyActive() const
    {
        for (auto& v : voices_)
            if (v.isActive()) return true;
        return false;
    }

    // Renders and adds into the provided L/R float pointers.
    void renderNextBlock(float* L, float* R, int numSamples, double sr)
    {
        int activeVoiceCount = 0;
        for (auto& v : voices_)
            if (v.isRenderable())
            {
                ++activeVoiceCount;
                v.renderAdd(L, R, numSamples, sr);
            }

        if (activeVoiceCount > 1)
        {
            const float gainComp = 1.0f / std::sqrt(1.0f + 0.38f * (float)(activeVoiceCount - 1));
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] = SynthVoicing::softClip(L[i] * gainComp);
                R[i] = SynthVoicing::softClip(R[i] * gainComp);
            }
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float deltaL = L[i] - lastBlockOutL_;
            const float deltaR = R[i] - lastBlockOutR_;
            const float limitedL = lastBlockOutL_ + juce::jlimit(-kMaxOutputStep, kMaxOutputStep, deltaL);
            const float limitedR = lastBlockOutR_ + juce::jlimit(-kMaxOutputStep, kMaxOutputStep, deltaR);
            lastBlockOutL_ = limitedL;
            lastBlockOutR_ = limitedR;
            L[i] = limitedL;
            R[i] = limitedR;
        }
    }

    void renderNextBlockRouted(std::array<juce::AudioBuffer<float>, 8>& trackBuffers, int numSamples, double sr)
    {
        std::array<int, 8> activeVoiceCount {};

        for (auto& v : voices_)
            if (v.isRenderable())
            {
                const int track = v.getMixerTrack();
                ++activeVoiceCount[(size_t) track];
                auto& buffer = trackBuffers[(size_t) track];
                v.renderAdd(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples, sr);
            }

        for (int track = 0; track < 8; ++track)
        {
            auto& buffer = trackBuffers[(size_t) track];
            if (activeVoiceCount[(size_t) track] > 1)
            {
                const float gainComp = 1.0f / std::sqrt(1.0f + 0.38f * (float) (activeVoiceCount[(size_t) track] - 1));
                buffer.applyGain(gainComp);
            }

            for (int i = 0; i < numSamples; ++i)
            {
                const float inL = buffer.getSample(0, i);
                const float inR = buffer.getSample(1, i);
                const float deltaL = inL - lastTrackOutL_[(size_t) track];
                const float deltaR = inR - lastTrackOutR_[(size_t) track];
                const float limitedL = lastTrackOutL_[(size_t) track] + juce::jlimit(-kMaxOutputStep, kMaxOutputStep, deltaL);
                const float limitedR = lastTrackOutR_[(size_t) track] + juce::jlimit(-kMaxOutputStep, kMaxOutputStep, deltaR);
                lastTrackOutL_[(size_t) track] = limitedL;
                lastTrackOutR_[(size_t) track] = limitedR;
                buffer.setSample(0, i, limitedL);
                buffer.setSample(1, i, limitedR);
            }
        }
    }

    void reset()
    {
        for (auto& v : voices_) v = SynthVoice{};
        stealIdx_ = 0;
        lastBlockOutL_ = 0.0f;
        lastBlockOutR_ = 0.0f;
        lastTrackOutL_.fill(0.0f);
        lastTrackOutR_.fill(0.0f);
    }

    void prepare(double /*sr*/, int /*maxBlock*/) {}

private:
    std::array<SynthVoice, kNumVoices> voices_;
    int stealIdx_ = 0;
    float lastBlockOutL_ = 0.0f;
    float lastBlockOutR_ = 0.0f;
    std::array<float, 8> lastTrackOutL_ {};
    std::array<float, 8> lastTrackOutR_ {};
    static constexpr float kMaxOutputStep = 0.06f;
};

namespace SynthPreview
{
    inline std::vector<float> renderWaveform(const SynthParams& params, int numSamples, double sampleRate = 44100.0)
    {
        SynthVoice voice;
        const int previewPitch = 60;
        const float aNorm = juce::jlimit(0.0f, 1.0f, (params.attack  -  1.0f) / 1999.0f);
        const float dNorm = juce::jlimit(0.0f, 1.0f, (params.decay   - 10.0f) / 2190.0f);
        const float rNorm = juce::jlimit(0.0f, 1.0f, (params.release - 20.0f) / 3980.0f);
        const double attackSec  = envTimeMs(aNorm, 0.5f,  5000.0f) * 0.001;
        const double decaySec   = envTimeMs(dNorm, 1.0f,  8000.0f) * 0.001;
        const double releaseSec = envTimeMs(rNorm, 5.0f, 15000.0f) * 0.001;
        const double holdSec = juce::jlimit(0.12, 0.45, attackSec * 0.35 + decaySec * 0.30 + 0.16);
        const double totalDurationSec = juce::jlimit(0.45, 2.4, holdSec + releaseSec + 0.20);
        const int renderSamples = juce::jmax(numSamples, (int)std::ceil(totalDurationSec * sampleRate));
        const int holdSamples = juce::jlimit(1, juce::jmax(1, renderSamples - 1), (int)std::ceil(holdSec * sampleRate));

        std::vector<float> renderLeft((size_t)renderSamples, 0.0f);
        std::vector<float> renderRight((size_t)renderSamples, 0.0f);
        const auto previewParams = DDSPAutoPatch::generate(params, previewPitch, 1.0f, holdSec + releaseSec);
        voice.noteOn(previewPitch, 1.0f, sampleRate, previewParams, renderSamples);
        voice.renderAdd(renderLeft.data(), renderRight.data(), holdSamples, sampleRate);
        voice.noteOff();
        voice.renderAdd(renderLeft.data() + holdSamples, renderRight.data() + holdSamples, renderSamples - holdSamples, sampleRate);

        std::vector<float> left((size_t)numSamples, 0.0f);
        const double samplesPerPixel = (double) renderSamples / (double) juce::jmax(1, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const int start = juce::jlimit(0, renderSamples - 1, (int)std::floor((double)i * samplesPerPixel));
            const int end = juce::jlimit(start + 1, renderSamples, (int)std::ceil((double)(i + 1) * samplesPerPixel));
            float peak = 0.0f;
            for (int s = start; s < end; ++s)
                peak = juce::jmax(peak, std::abs(renderLeft[(size_t)s]));
            left[(size_t)i] = peak;
        }

        float peak = 0.0f;
        for (const auto sample : left)
            peak = juce::jmax(peak, std::abs(sample));

        if (peak > 0.0001f)
        {
            const float gain = 1.0f / peak;
            for (auto& sample : left)
                sample *= gain;
        }

        return left;
    }

    // ------------------------------------------------------------------
    // MinMax waveform data — shows oscillator texture + ADSR envelope shape
    // ------------------------------------------------------------------
    struct WaveformData
    {
        std::vector<float> minVals;
        std::vector<float> maxVals;
        float attackFrac  = 0.0f;   // [0..1] x-fraction where attack ends
        float decayFrac   = 0.0f;   // [0..1] x-fraction where decay ends
        float releaseFrac = 0.0f;   // [0..1] x-fraction where release begins
    };

    inline WaveformData renderWaveformData(const SynthParams& params, int numPixels, double sampleRate = 44100.0)
    {
        SynthVoice voice;
        const int    previewPitch = 60;
        const float  aNorm = juce::jlimit(0.0f, 1.0f, (params.attack  -  1.0f) / 1999.0f);
        const float  dNorm = juce::jlimit(0.0f, 1.0f, (params.decay   - 10.0f) / 2190.0f);
        const float  rNorm = juce::jlimit(0.0f, 1.0f, (params.release - 20.0f) / 3980.0f);
        const double attackSec  = envTimeMs(aNorm, 0.5f,  5000.0f) * 0.001;
        const double decaySec   = envTimeMs(dNorm, 1.0f,  8000.0f) * 0.001;
        const double releaseSec = envTimeMs(rNorm, 5.0f, 15000.0f) * 0.001;
        const double holdSec      = juce::jlimit(0.12, 0.45, attackSec * 0.35 + decaySec * 0.30 + 0.16);
        const double totalSec     = juce::jlimit(0.45, 2.4, holdSec + releaseSec + 0.20);
        const int    renderSamples = juce::jmax(numPixels, (int)std::ceil(totalSec * sampleRate));
        const int    holdSamples   = juce::jlimit(1, juce::jmax(1, renderSamples - 1),
                                                  (int)std::ceil(holdSec * sampleRate));

        // ADSR phase boundaries (in samples within the hold portion)
        const int attackSamples = (int)std::ceil(attackSec * sampleRate);
        const int decaySamples  = (int)std::ceil(decaySec  * sampleRate);
        const int attackEnd     = juce::jmin(attackSamples, holdSamples);
        const int decayEnd      = juce::jmin(attackSamples + decaySamples, holdSamples);

        std::vector<float> renderLeft ((size_t)renderSamples, 0.0f);
        std::vector<float> renderRight((size_t)renderSamples, 0.0f);

        const auto previewParams = DDSPAutoPatch::generate(params, previewPitch, 1.0f, holdSec + releaseSec);
        voice.noteOn(previewPitch, 1.0f, sampleRate, previewParams, renderSamples);
        voice.renderAdd(renderLeft.data(), renderRight.data(), holdSamples, sampleRate);
        voice.noteOff();
        voice.renderAdd(renderLeft.data() + holdSamples, renderRight.data() + holdSamples,
                        renderSamples - holdSamples, sampleRate);

        
        WaveformData data;
        data.minVals.resize((size_t)numPixels, 0.0f);
        data.maxVals.resize((size_t)numPixels, 0.0f);

        const double samplesPerPixel = (double)renderSamples / (double)juce::jmax(1, numPixels);
        for (int i = 0; i < numPixels; ++i)
        {
            const int start = juce::jlimit(0, renderSamples - 1, (int)std::floor((double)i * samplesPerPixel));
            const int end   = juce::jlimit(start + 1, renderSamples, (int)std::ceil((double)(i + 1) * samplesPerPixel));
            float lo = 0.0f, hi = 0.0f;
            for (int s = start; s < end; ++s)
            {
                const float v = renderLeft[(size_t)s];
                lo = juce::jmin(lo, v);
                hi = juce::jmax(hi, v);
            }
            data.minVals[(size_t)i] = lo;
            data.maxVals[(size_t)i] = hi;
        }

        // Normalize
        float peak = 0.0f;
        for (int i = 0; i < numPixels; ++i)
        {
            peak = juce::jmax(peak, std::abs(data.minVals[(size_t)i]));
            peak = juce::jmax(peak, std::abs(data.maxVals[(size_t)i]));
        }
        if (peak > 0.0001f)
        {
            const float gain = 1.0f / peak;
            for (int i = 0; i < numPixels; ++i)
            {
                data.minVals[(size_t)i] *= gain;
                data.maxVals[(size_t)i] *= gain;
            }
        }

        // Phase boundary fractions [0..1]
        data.attackFrac  = (float)attackEnd   / (float)renderSamples;
        data.decayFrac   = (float)decayEnd    / (float)renderSamples;
        data.releaseFrac = (float)holdSamples / (float)renderSamples;

        return data;
    }
}
