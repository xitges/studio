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
}

// ---------------------------------------------------------------------------
// One polyphonic voice
// ---------------------------------------------------------------------------
class SynthVoice
{
public:
    // Trigger a note. noteLenSamples: auto-release after this many samples.
    void noteOn(int midiPitch, float velocity, double sr, const SynthParams& p, int noteLenSamples,
                int voiceSlot = 0)
    {
        beginResidualFade();
        noteOnFadeRemaining_ = kNoteOnFadeSamples;
        pitch_      = midiPitch;
        velocity_   = velocity;
        params_     = p;
        voiceSlot_  = voiceSlot;
        frequency_  = 440.0 * std::pow(2.0, (midiPitch - 69) / 12.0);
        const float startJitter = SynthVoicing::stableHash01(voiceSlot * 131 + 11);
        const float secondJitter = SynthVoicing::stableHash01(voiceSlot * 197 + 53);
        const float subJitter = SynthVoicing::stableHash01(voiceSlot * 223 + 97);
        const float driftJitter = SynthVoicing::stableHash01(voiceSlot * 283 + 149);
        phase_      = startJitter;
        phase2_     = secondJitter;
        subPhase_   = subJitter;
        lfoPhase_   = 0.0;

        static constexpr std::array<float, 8> kDetuneCents { -3.5f, -1.75f, -0.75f, 0.0f,
                                                              0.85f, 1.8f, 2.8f, 4.0f };
        detuneRatio_ = SynthVoicing::detuneRatioFromCents(
            kDetuneCents[(size_t)juce::jlimit(0, (int)kDetuneCents.size() - 1, voiceSlot)]);
        voicePan_ = juce::jlimit(-0.22f, 0.22f,
                                 ((float)voiceSlot - 3.5f) * 0.055f + (startJitter - 0.5f) * 0.05f);
        const float driftCents = (driftJitter - 0.5f) * 1.6f;
        driftRatio_ = SynthVoicing::detuneRatioFromCents(driftCents);
        filterEnvDepth_ = juce::jlimit(0.04f, 0.85f,
                                       0.18f + (1.0f - p.sustain) * 0.34f + velocity * 0.16f
                                     + (p.waveform == 1 || p.waveform == 2 ? 0.07f : 0.0f));
        transientPunch_ = 1.0f + velocity * 0.20f
                        + ((p.waveform == 1 || p.waveform == 2) ? 0.08f : 0.0f);

        const float attackSamples  = juce::jmax(kMinAttackSamples, p.attack  * 0.001f * (float)sr);
        const float decaySamples   = juce::jmax(1.0f, p.decay   * 0.001f * (float)sr);
        releaseSamples_            = juce::jmax(kMinReleaseSamples, p.release * 0.001f * (float)sr);

        attackInc_  = 1.0f / attackSamples;
        decayInc_   = (1.0f - p.sustain) / decaySamples;
        sustainLvl_ = p.sustain;
        releaseInc_ = juce::jmax(0.000001f, juce::jmax(envLevel_, p.sustain) / releaseSamples_);

        envState_ = Env::Attack;
        envLevel_ = 0.0f;

        noteLenRemaining_ = noteLenSamples;

        // Clear filter state
        z1L_ = z2L_ = z1R_ = z2R_ = 0.0f;
    }

    void noteOff()
    {
        if (envState_ != Env::Idle)
        {
            releaseInc_ = juce::jmax(0.000001f, envLevel_ / juce::jmax(1.0f, releaseSamples_));
            envState_ = Env::Release;
        }
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

        float b0, b1, b2, a1, a2;
        computeFilter(params_.cutoff, params_.resonance, sr, b0, b1, b2, a1, a2);

        const double lfoInc = params_.lfoRate / sr;
        const float panAngle = (voicePan_ + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float panL = std::cos(panAngle);
        const float panR = std::sin(panAngle);

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
                    if (envLevel_ <= 0.0f) { envLevel_ = 0.0f; envState_ = Env::Idle; }
                    break;
                case Env::Idle:
                    break;
            }

            if (envState_ != Env::Idle)
            {
                // LFO
                float lfoVal = 0.0f;
                if (params_.lfoDepth > 0.0f)
                {
                    lfoVal = std::sin((float)(lfoPhase_ * juce::MathConstants<double>::twoPi));
                    lfoPhase_ += lfoInc;
                    if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
                }

                // Frequency with optional pitch LFO
                double freq = frequency_ * driftRatio_;
                if (params_.lfoDepth > 0.0f && params_.lfoTarget == 1)
                    freq *= std::pow(2.0, (double)(lfoVal * params_.lfoDepth * 2.0f) / 12.0);

                const float cutoffKeyTrack = 1.0f + juce::jlimit(-0.18f, 0.28f, ((float)pitch_ - 60.0f) * 0.0105f);
                const float envOpen = std::pow(juce::jlimit(0.0f, 1.0f, envLevel_), 0.55f);
                const float envCutoff = params_.cutoff * (1.0f + filterEnvDepth_ * envOpen);
                float modCutoff = envCutoff * cutoffKeyTrack;
                if (params_.lfoDepth > 0.0f && params_.lfoTarget == 0)
                    modCutoff *= (1.0f + lfoVal * params_.lfoDepth * 0.75f);
                modCutoff = juce::jlimit(50.0f, 20000.0f, modCutoff);
                computeFilter(modCutoff, params_.resonance, sr, b0, b1, b2, a1, a2);

                // Oscillator
                const double dt = freq / sr;
                const double dt2 = (freq * detuneRatio_) / sr;
                const double dtSub = juce::jlimit(0.0, 0.49, (freq * 0.5) / sr);
                phase_ += dt;
                if (phase_ >= 1.0) phase_ -= 1.0;
                phase2_ += dt2;
                if (phase2_ >= 1.0) phase2_ -= 1.0;
                subPhase_ += dtSub;
                if (subPhase_ >= 1.0) subPhase_ -= 1.0;

                const float osc    = generateSample((float)phase_, dt, params_.waveform);
                const float osc1   = generateSample((float)phase2_, dt2, params_.waveform);
                const float subOsc = generateSubSample((float)subPhase_, params_.waveform);
                const float subMix = (params_.waveform == 0) ? 0.0f
                                   : (params_.waveform == 2 ? 0.28f
                                      : (params_.waveform == 1 ? 0.22f : 0.14f));
                const float transientDrive = 1.0f + (1.0f - envLevel_) * (0.18f + velocity_ * 0.12f)
                                           + (envOpen * 0.08f);
                const float harmonicDrive = 1.12f + params_.resonance * 0.85f
                                          + ((params_.waveform == 1 || params_.waveform == 2) ? 0.18f : 0.0f);
                const float bodyBlend = (params_.waveform == 3) ? 0.52f : 0.58f;
                const float rawSample = osc * bodyBlend + osc1 * 0.34f + subOsc * subMix;
                const float sample = SynthVoicing::softClip(rawSample * harmonicDrive * transientDrive)
                                   * envLevel_ * velocity_ * 0.28f * transientPunch_;

                // Biquad LP filter (direct-form I)
                const float filtered = SynthVoicing::softClip((b0 * sample + z1L_)
                                                            * (1.0f + params_.resonance * 0.30f));
                z1L_ = b1 * sample - a1 * filtered + z2L_;
                z2L_ = b2 * sample - a2 * filtered;

                const float air = juce::jlimit(0.0f, 0.22f,
                                               ((params_.cutoff - 1800.0f) / 12000.0f) * 0.18f
                                             + (params_.waveform == 3 ? 0.03f : 0.0f));
                const float outputSample = SynthVoicing::softClip(filtered + (rawSample - filtered) * air);

                synthOutL = outputSample * panL;
                synthOutR = outputSample * panR;
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
    double phase_             = 0.0;
    double phase2_            = 0.0;
    double subPhase_          = 0.0;
    double lfoPhase_          = 0.0;

    Env    envState_          = Env::Idle;
    float  envLevel_          = 0.0f;
    float  attackInc_         = 0.0f;
    float  decayInc_          = 0.0f;
    float  releaseInc_        = 0.0f;
    float  releaseSamples_    = 1.0f;
    float  sustainLvl_        = 0.7f;
    SynthParams params_       = {};
    float  detuneRatio_       = 1.0f;
    float  driftRatio_        = 1.0f;
    float  filterEnvDepth_    = 0.22f;
    float  transientPunch_    = 1.0f;
    float  voicePan_          = 0.0f;
    int    voiceSlot_         = 0;

    int    noteLenRemaining_  = 0;

    // Biquad filter state (mono; same signal routed to L+R)
    float  z1L_ = 0.0f, z2L_ = 0.0f;
    float  z1R_ = 0.0f, z2R_ = 0.0f;
    float  lastOutputL_ = 0.0f, lastOutputR_ = 0.0f;
    float  residualTailL_ = 0.0f, residualTailR_ = 0.0f;
    int    residualFadeRemaining_ = 0;
    int    noteOnFadeRemaining_ = 0;
    static constexpr int kResidualFadeSamples = 64;
    static constexpr int kNoteOnFadeSamples = 96;
    static constexpr float kMinAttackSamples = 96.0f;
    static constexpr float kMinReleaseSamples = 192.0f;

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

    float generateSample(float p, double dt, int waveform) const
    {
        switch (waveform)
        {
            case 0: return std::sin(p * juce::MathConstants<float>::twoPi);          // sine — no aliasing
            case 1:                                                                    // sawtooth + PolyBLEP
            {
                float saw = 2.0f * p - 1.0f;
                saw -= polyBLEP((double)p, dt);
                return saw;
            }
            case 2:                                                                    // square + PolyBLEP
            {
                float sq = (p < 0.5f) ? 1.0f : -1.0f;
                sq += polyBLEP((double)p, dt);
                sq -= polyBLEP(std::fmod((double)p + 0.5, 1.0), dt);
                return sq;
            }
            case 3: return 1.0f - 4.0f * std::abs(p - 0.5f);                        // triangle — already band-limited
            default: return 0.0f;
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

        voices_[voiceIdx].noteOn(pitch, velocity, sr, p, noteLenSamples, voiceIdx);
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

    void reset()
    {
        for (auto& v : voices_) v = SynthVoice{};
        stealIdx_ = 0;
        lastBlockOutL_ = 0.0f;
        lastBlockOutR_ = 0.0f;
    }

    void prepare(double /*sr*/, int /*maxBlock*/) {}

private:
    std::array<SynthVoice, kNumVoices> voices_;
    int stealIdx_ = 0;
    float lastBlockOutL_ = 0.0f;
    float lastBlockOutR_ = 0.0f;
    static constexpr float kMaxOutputStep = 0.06f;
};

namespace SynthPreview
{
    inline std::vector<float> renderWaveform(const SynthParams& params, int numSamples, double sampleRate = 44100.0)
    {
        std::vector<float> left((size_t)numSamples, 0.0f);
        std::vector<float> right((size_t)numSamples, 0.0f);

        SynthVoice voice;
        const int previewPitch = 60;
        const int previewLength = juce::jmax(numSamples / 2, 1);
        const auto previewParams = DDSPAutoPatch::generate(params, previewPitch, 1.0f, 0.3);
        voice.noteOn(previewPitch, 1.0f, sampleRate, previewParams, previewLength);
        voice.renderAdd(left.data(), right.data(), numSamples, sampleRate);

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
}
