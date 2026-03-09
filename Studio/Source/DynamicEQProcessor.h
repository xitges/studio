/*
  ==============================================================================

    DynamicEQProcessor.h  —  Professional Dynamic EQ (6-band, stereo)

    Architecture:
      - Up to 6 bands, each with its own filter type, gain, Q, and optional
        dynamic (threshold / ratio / knee / attack / release) mode.
      - Detector path: band-matched BPF (Bell) or shelf/pass filter isolates
        the relevant frequency range for envelope detection.
      - Gain computer: soft-knee compressor / expander on the detected level.
      - Main path: RBJ biquad coefficients recomputed every 4 samples from the
        smoothed gain value — no zipper noise, no trig-per-sample.
      - Atomic gain-reduction floats allow the UI to poll metering safely.

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>

// ---------------------------------------------------------------------------
// Band parameter types
// ---------------------------------------------------------------------------
enum class DynEQFilterType : int { Bell = 0, LowShelf, HighShelf, LowPass, HighPass };
enum class DynEQDirection  : int { Downward = 0, Upward };

struct DynEQBandParams
{
    float           frequency  = 1000.0f;
    float           maxGainDb  = 0.0f;    // static gain OR dynamic ceiling
    float           Q          = 1.0f;
    DynEQFilterType type       = DynEQFilterType::Bell;
    bool            enabled    = true;

    // Dynamic section
    bool            dynamicOn  = false;
    float           threshold  = -18.0f;  // dBFS
    float           ratio      = 4.0f;
    float           knee       = 3.0f;    // soft-knee width (dB)
    float           attackMs   = 5.0f;
    float           releaseMs  = 80.0f;
    DynEQDirection  direction  = DynEQDirection::Downward;
};

// ---------------------------------------------------------------------------
// Internal runtime state — one per band (audio-thread only, except atomic meter)
// ---------------------------------------------------------------------------
struct DynEQBandState
{
    // Main-path biquad coefficients (updated dynamically every 4 samples)
    float b0m = 1, b1m = 0, b2m = 0, a1m = 0, a2m = 0;
    // Main-path biquad delay lines (stereo)
    float z1L = 0, z2L = 0;
    float z1R = 0, z2R = 0;

    // Detector-path biquad coefficients (static, set on prepare/setBand)
    float b0d = 1, b1d = 0, b2d = 0, a1d = 0, a2d = 0;
    // Detector delay lines (mono, linked stereo)
    float dz1 = 0, dz2 = 0;

    // Envelope follower
    float envLinear   = 0.0f;
    float attackCoef  = 0.0f;   // exp(-1 / (attackMs * sr))
    float releaseCoef = 0.0f;

    // Gain smoother (~10ms one-pole IIR on the gain value itself)
    float currentGainDb  = 0.0f;
    float gainSmoothCoef = 0.97f;

    // Metering (written on audio thread, read on UI thread)
    std::atomic<float> gainReductionDb { 0.0f };

    // Atomic is not copyable — ensure DynEQBandState stays in-place
    DynEQBandState() = default;
    DynEQBandState(const DynEQBandState&) = delete;
    DynEQBandState& operator=(const DynEQBandState&) = delete;
};

// ---------------------------------------------------------------------------
// DynamicEQProcessor
// ---------------------------------------------------------------------------
class DynamicEQProcessor
{
public:
    static constexpr int kMaxBands = 6;

    DynamicEQProcessor() = default;

    // ---- Setup (message thread) -------------------------------------------

    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sr_      = sampleRate;
        enabled_ = false;
        for (int i = 0; i < kMaxBands; ++i)
            recomputeBand(i);
        reset();
    }

    void reset()
    {
        for (auto& s : state_)
        {
            s.z1L = s.z2L = s.z1R = s.z2R = 0.0f;
            s.dz1 = s.dz2  = 0.0f;
            s.envLinear     = 0.0f;
            s.currentGainDb = 0.0f;
            s.gainReductionDb.store(0.0f, std::memory_order_relaxed);
        }
    }

    void setEnabled(bool e) { enabled_ = e; }
    bool isEnabled()  const { return enabled_; }

    void setBand(int idx, const DynEQBandParams& p)
    {
        if (idx < 0 || idx >= kMaxBands) return;
        params_[idx] = p;
        recomputeBand(idx);
    }

    const DynEQBandParams& getBand(int idx) const
    {
        jassert(idx >= 0 && idx < kMaxBands);
        return params_[idx];
    }

    // Positive = gain was reduced (dB). Safe to poll from UI thread.
    float getGainReductionDb(int idx) const
    {
        if (idx < 0 || idx >= kMaxBands) return 0.0f;
        return state_[idx].gainReductionDb.load(std::memory_order_relaxed);
    }

    // ---- Render (audio thread) -------------------------------------------

    void processBlock(float* L, float* R, int numSamples)
    {
        if (!enabled_) return;

        for (int i = 0; i < kMaxBands; ++i)
        {
            const DynEQBandParams& p = params_[i];
            if (!p.enabled) continue;

            DynEQBandState& s = state_[i];

            if (!p.dynamicOn)
            {
                // Static EQ path — no envelope tracking needed
                for (int n = 0; n < numSamples; ++n)
                {
                    L[n] = biquad(L[n], s.z1L, s.z2L, s.b0m, s.b1m, s.b2m, s.a1m, s.a2m);
                    R[n] = biquad(R[n], s.z1R, s.z2R, s.b0m, s.b1m, s.b2m, s.a1m, s.a2m);
                }
                s.gainReductionDb.store(0.0f, std::memory_order_relaxed);
                continue;
            }

            // Dynamic path
            for (int n = 0; n < numSamples; ++n)
            {
                const float inL = L[n];
                const float inR = R[n];

                // 1. Detector (linked stereo, band-matched filter)
                const float detIn  = (inL + inR) * 0.5f;
                const float detOut = biquad(detIn, s.dz1, s.dz2,
                                            s.b0d, s.b1d, s.b2d, s.a1d, s.a2d);

                // 2. Peak envelope follower (log domain)
                const float absVal = std::abs(detOut);
                const float coef   = (absVal > s.envLinear) ? s.attackCoef : s.releaseCoef;
                s.envLinear = coef * s.envLinear + (1.0f - coef) * absVal;
                const float envDb  = 20.0f * std::log10(s.envLinear + 1e-9f);

                // 3. Gain computer (soft-knee compressor/expander)
                const float gr = gainReduction(envDb, p.threshold, p.ratio, p.knee);

                // 4. Direction: downward compress a boost, or upward expand a cut
                const float targetGain = (p.direction == DynEQDirection::Downward)
                                       ? p.maxGainDb - gr
                                       : p.maxGainDb + gr;

                // 5. Smooth gain (one-pole IIR ~10ms)
                s.currentGainDb += (1.0f - s.gainSmoothCoef) * (targetGain - s.currentGainDb);

                // 6. Meter
                s.gainReductionDb.store(p.maxGainDb - s.currentGainDb,
                                        std::memory_order_relaxed);

                // 7. Recompute main biquad coefficients (every 4 samples)
                if ((n & 3) == 0)
                    recomputeMainCoeffs(i, s.currentGainDb);

                // 8. Main filter
                L[n] = biquad(inL, s.z1L, s.z2L, s.b0m, s.b1m, s.b2m, s.a1m, s.a2m);
                R[n] = biquad(inR, s.z1R, s.z2R, s.b0m, s.b1m, s.b2m, s.a1m, s.a2m);
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicEQProcessor)

private:
    double sr_      = 44100.0;
    bool   enabled_ = false;

    DynEQBandParams params_[kMaxBands];
    DynEQBandState  state_ [kMaxBands];

    // ---- DSP primitives ---------------------------------------------------

    static inline float biquad(float x,
                                float& z1, float& z2,
                                float b0, float b1, float b2,
                                float a1, float a2) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // Soft-knee gain computer — returns gain reduction (dB, >= 0)
    static float gainReduction(float envDb, float threshold,
                                float ratio, float knee) noexcept
    {
        const float ov = envDb - threshold;
        if (ov < -(knee * 0.5f)) return 0.0f;
        if (ov >  (knee * 0.5f)) return ov * (1.0f - 1.0f / ratio);
        const float x = (ov + knee * 0.5f) / knee;
        return x * x * (1.0f - 1.0f / ratio) * knee * 0.5f;
    }

    // ---- RBJ biquad coefficient formulas ----------------------------------

    void coeffBell(float f, float gainDb, float Q,
                   float& b0, float& b1, float& b2, float& a1, float& a2)
    {
        const float A    = std::pow(10.0f, gainDb / 40.0f);
        const float w0   = juce::MathConstants<float>::twoPi * f / (float)sr_;
        const float cosW = std::cos(w0), sinW = std::sin(w0);
        const float al   = sinW / (2.0f * Q);
        const float inv  = 1.0f / (1.0f + al / A);
        b0 =  (1.0f + al * A) * inv;
        b1 = (-2.0f * cosW)   * inv;
        b2 =  (1.0f - al * A) * inv;
        a1 = b1;
        a2 =  (1.0f - al / A) * inv;
    }

    void coeffLowShelf(float f, float gainDb, float Q,
                       float& b0, float& b1, float& b2, float& a1, float& a2)
    {
        const float A    = std::pow(10.0f, gainDb / 40.0f);
        const float w0   = juce::MathConstants<float>::twoPi * f / (float)sr_;
        const float cosW = std::cos(w0), sinW = std::sin(w0);
        const float al   = sinW * 0.5f * std::sqrt((A + 1.0f/A) * (1.0f/Q - 1.0f) + 2.0f);
        const float sqA  = std::sqrt(A);
        const float inv  = 1.0f / ((A+1) + (A-1)*cosW + 2*sqA*al);
        b0 =  A * ((A+1) - (A-1)*cosW + 2*sqA*al) * inv;
        b1 = 2*A * ((A-1) - (A+1)*cosW)           * inv;
        b2 =  A * ((A+1) - (A-1)*cosW - 2*sqA*al) * inv;
        a1 = -2 * ((A-1) + (A+1)*cosW)            * inv;
        a2 =      ((A+1) + (A-1)*cosW - 2*sqA*al) * inv;
    }

    void coeffHighShelf(float f, float gainDb, float Q,
                        float& b0, float& b1, float& b2, float& a1, float& a2)
    {
        const float A    = std::pow(10.0f, gainDb / 40.0f);
        const float w0   = juce::MathConstants<float>::twoPi * f / (float)sr_;
        const float cosW = std::cos(w0), sinW = std::sin(w0);
        const float al   = sinW * 0.5f * std::sqrt((A + 1.0f/A) * (1.0f/Q - 1.0f) + 2.0f);
        const float sqA  = std::sqrt(A);
        const float inv  = 1.0f / ((A+1) - (A-1)*cosW + 2*sqA*al);
        b0 =  A * ((A+1) + (A-1)*cosW + 2*sqA*al) * inv;
        b1 = -2*A * ((A-1) + (A+1)*cosW)          * inv;
        b2 =  A * ((A+1) + (A-1)*cosW - 2*sqA*al) * inv;
        a1 =  2 * ((A-1) - (A+1)*cosW)            * inv;
        a2 =      ((A+1) - (A-1)*cosW - 2*sqA*al) * inv;
    }

    void coeffLowPass(float f, float Q,
                      float& b0, float& b1, float& b2, float& a1, float& a2)
    {
        const float w0   = juce::MathConstants<float>::twoPi * f / (float)sr_;
        const float cosW = std::cos(w0), sinW = std::sin(w0);
        const float al   = sinW / (2.0f * Q);
        const float inv  = 1.0f / (1.0f + al);
        b0 = ((1.0f - cosW) * 0.5f) * inv;
        b1 =  (1.0f - cosW)         * inv;
        b2 = b0;
        a1 = (-2.0f * cosW)         * inv;
        a2 =  (1.0f - al)           * inv;
    }

    void coeffHighPass(float f, float Q,
                       float& b0, float& b1, float& b2, float& a1, float& a2)
    {
        const float w0   = juce::MathConstants<float>::twoPi * f / (float)sr_;
        const float cosW = std::cos(w0), sinW = std::sin(w0);
        const float al   = sinW / (2.0f * Q);
        const float inv  = 1.0f / (1.0f + al);
        b0 =  ((1.0f + cosW) * 0.5f) * inv;
        b1 = -(1.0f + cosW)          * inv;
        b2 = b0;
        a1 = (-2.0f * cosW)          * inv;
        a2 =  (1.0f - al)            * inv;
    }

    // Bandpass for detector path (constant peak gain = 0dB)
    void coeffBandPass(float f, float Q,
                       float& b0, float& b1, float& b2, float& a1, float& a2)
    {
        const float w0   = juce::MathConstants<float>::twoPi * f / (float)sr_;
        const float cosW = std::cos(w0), sinW = std::sin(w0);
        const float al   = sinW / (2.0f * Q);
        const float inv  = 1.0f / (1.0f + al);
        b0 =  al      * inv;
        b1 =  0.0f;
        b2 = -al      * inv;
        a1 = -2*cosW  * inv;
        a2 = (1 - al) * inv;
    }

    // ---- Coefficient dispatch ---------------------------------------------

    void recomputeMainCoeffs(int i, float gainDb)
    {
        const DynEQBandParams& p = params_[i];
        DynEQBandState&        s = state_[i];

        const float f = juce::jlimit(20.0f, (float)(sr_ * 0.499), p.frequency);
        const float Q = juce::jmax(0.1f, p.Q);

        switch (p.type)
        {
            case DynEQFilterType::Bell:
                coeffBell     (f, gainDb, Q, s.b0m, s.b1m, s.b2m, s.a1m, s.a2m); break;
            case DynEQFilterType::LowShelf:
                coeffLowShelf (f, gainDb, Q, s.b0m, s.b1m, s.b2m, s.a1m, s.a2m); break;
            case DynEQFilterType::HighShelf:
                coeffHighShelf(f, gainDb, Q, s.b0m, s.b1m, s.b2m, s.a1m, s.a2m); break;
            case DynEQFilterType::LowPass:
                coeffLowPass  (f, Q,        s.b0m, s.b1m, s.b2m, s.a1m, s.a2m); break;
            case DynEQFilterType::HighPass:
                coeffHighPass (f, Q,        s.b0m, s.b1m, s.b2m, s.a1m, s.a2m); break;
        }
    }

    void recomputeBand(int i)
    {
        if (sr_ <= 0.0) return;

        const DynEQBandParams& p = params_[i];
        DynEQBandState&        s = state_[i];

        const float f = juce::jlimit(20.0f, (float)(sr_ * 0.499), p.frequency);
        const float Q = juce::jmax(0.1f, p.Q);

        // Main path at full static gain
        recomputeMainCoeffs(i, p.maxGainDb);

        // Detector path — band-matched analysis filter
        switch (p.type)
        {
            case DynEQFilterType::Bell:
                // Narrow BPF tracks energy specifically around the band frequency
                coeffBandPass(f, juce::jmax(0.5f, Q),
                              s.b0d, s.b1d, s.b2d, s.a1d, s.a2d);
                break;
            case DynEQFilterType::LowShelf:
            case DynEQFilterType::LowPass:
                coeffLowPass(f, Q, s.b0d, s.b1d, s.b2d, s.a1d, s.a2d);
                break;
            case DynEQFilterType::HighShelf:
            case DynEQFilterType::HighPass:
                coeffHighPass(f, Q, s.b0d, s.b1d, s.b2d, s.a1d, s.a2d);
                break;
        }

        // Envelope follower time constants
        s.attackCoef  = std::exp(-1.0f / (juce::jmax(0.1f, p.attackMs)  * 0.001f * (float)sr_));
        s.releaseCoef = std::exp(-1.0f / (juce::jmax(0.1f, p.releaseMs) * 0.001f * (float)sr_));

        // Gain smoother (~10ms)
        s.gainSmoothCoef = std::exp(-1.0f / (0.010f * (float)sr_));
    }
};
