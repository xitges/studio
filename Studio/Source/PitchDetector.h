/*
  ==============================================================================

    PitchDetector.h
    Created: 22 Mar 2026
    Author:  홍준영

    Monophonic pitch detector using optimized YIN algorithm.
    Designed for real-time vocal pitch correction (Auto-Tune).
    Audio-thread safe: all buffers pre-allocated in prepare().

    Key improvements over basic YIN:
    - 4x overlap (hop = windowSize/4) for ~86 Hz update rate at 44.1kHz
    - Confidence output from CMNDF minimum value
    - Exponential smoothing weighted by confidence
    - Onset detection for fast response on new notes
    - Sub-50ms latency suitable for hard-tune effects

  ==============================================================================
*/

#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

class PitchDetector
{
public:
    PitchDetector() = default;

    void prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate;

        // Analysis window: 2048 samples covers down to ~43 Hz (vocal C2+)
        windowSize_ = 2048;
        halfWindow_ = windowSize_ / 2;

        // Hop size: window/4 for 4x overlap => ~86 Hz update rate at 44.1kHz
        // This gives ~11.6ms between pitch estimates — critical for fast retune
        hopSize_ = windowSize_ / 4;

        // Pre-allocate all buffers
        inputRing_.assign((size_t)windowSize_, 0.0f);
        yinBuf_.resize((size_t)halfWindow_, 0.0f);
        linearBuf_.resize((size_t)windowSize_, 0.0f);
        ringWritePos_ = 0;
        samplesAccum_ = 0;
        currentPitchHz_ = 0.0f;
        confidence_ = 0.0f;
        smoothedPitchHz_ = 0.0f;

        // Tau search range for vocal frequencies
        minTau_ = std::max(2, (int)(sampleRate_ / kMaxFreq));
        maxTau_ = std::min(halfWindow_ - 1, (int)(sampleRate_ / kMinFreq));

        // Median filter history
        medianBuf_[0] = medianBuf_[1] = medianBuf_[2] = 0.0f;
        medianIdx_ = 0;

        // Onset detection state
        prevRms_ = 0.0f;
        onsetDetected_ = false;
    }

    /** Feed audio samples (mono). Call once per audio callback. */
    void processBlock(const float* monoData, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            inputRing_[(size_t)ringWritePos_] = monoData[i];
            ringWritePos_ = (ringWritePos_ + 1) % windowSize_;
            ++samplesAccum_;

            // Run YIN every hopSize_ samples (4x overlap)
            if (samplesAccum_ >= hopSize_)
            {
                samplesAccum_ = 0;
                runAnalysisFrame();
            }
        }
    }

    /** Returns detected pitch in Hz, or 0 if unvoiced. */
    float getPitchHz() const { return currentPitchHz_; }

    /** Returns smoothed pitch in Hz (for correction engine). */
    float getSmoothedPitchHz() const { return smoothedPitchHz_; }

    /** Returns confidence [0,1] — lower CMNDF minimum = higher confidence. */
    float getConfidence() const { return confidence_; }

    /** Returns true if a vocal onset was detected this frame. */
    bool isOnset() const { return onsetDetected_; }

    /** Returns true if signal is currently voiced. */
    bool isVoiced() const { return currentPitchHz_ > 0.0f && confidence_ > 0.3f; }

    void reset()
    {
        if (!inputRing_.empty())
            std::fill(inputRing_.begin(), inputRing_.end(), 0.0f);
        ringWritePos_ = 0;
        samplesAccum_ = 0;
        currentPitchHz_ = 0.0f;
        smoothedPitchHz_ = 0.0f;
        confidence_ = 0.0f;
        medianBuf_[0] = medianBuf_[1] = medianBuf_[2] = 0.0f;
        medianIdx_ = 0;
        prevRms_ = 0.0f;
        onsetDetected_ = false;
    }

    /** Set frequency range for tau search (call from message thread before processing). */
    void setFrequencyRange(float lowHz, float highHz)
    {
        if (lowHz > 0.0f && highHz > lowHz && sampleRate_ > 0.0)
        {
            kMinFreq = lowHz;
            kMaxFreq = highHz;
            minTau_ = std::max(2, (int)(sampleRate_ / kMaxFreq));
            maxTau_ = std::min(halfWindow_ - 1, (int)(sampleRate_ / kMinFreq));
        }
    }

private:
    double sampleRate_ = 44100.0;
    int    windowSize_ = 2048;
    int    halfWindow_ = 1024;
    int    hopSize_    = 512;  // window/4 for 4x overlap

    std::vector<float> inputRing_;
    std::vector<float> linearBuf_;
    int ringWritePos_ = 0;
    int samplesAccum_ = 0;

    std::vector<float> yinBuf_;
    float currentPitchHz_ = 0.0f;
    float smoothedPitchHz_ = 0.0f;
    float confidence_ = 0.0f;

    // Tau search range (precomputed from frequency bounds)
    int minTau_ = 29;
    int maxTau_ = 882;

    // Median filter
    float medianBuf_[3] = {};
    int   medianIdx_ = 0;

    // Onset detection
    float prevRms_ = 0.0f;
    bool  onsetDetected_ = false;

    float kYinThreshold = 0.15f;
    float kMinFreq      = 65.0f;   // lowest detectable (C2)
    float kMaxFreq      = 1200.0f; // highest detectable (D6)
    static constexpr float kMinRms = 0.002f;   // RMS gate threshold
    static constexpr float kOnsetRmsRatio = 3.0f; // onset = RMS jumps 3x

    // -----------------------------------------------------------------------
    void runAnalysisFrame()
    {
        // Compute RMS for this frame
        float energy = 0.0f;
        for (int j = 0; j < windowSize_; ++j)
        {
            const float s = inputRing_[(size_t)j];
            energy += s * s;
        }
        const float rms = std::sqrt(energy / (float)windowSize_);

        // Onset detection: sharp RMS increase indicates new note attack
        onsetDetected_ = (prevRms_ > 0.001f)
                       ? (rms / prevRms_ > kOnsetRmsRatio)
                       : (rms > 0.01f && prevRms_ < 0.002f);
        prevRms_ = rms;

        if (rms < kMinRms)
        {
            currentPitchHz_ = 0.0f;
            confidence_ = 0.0f;
            // Decay smoothed pitch toward 0 (unvoiced)
            smoothedPitchHz_ *= 0.9f;
            if (smoothedPitchHz_ < 1.0f) smoothedPitchHz_ = 0.0f;
            return;
        }

        float rawConf = 0.0f;
        const float rawPitch = runYIN(rawConf);

        // 3-sample median filter to smooth jitter
        medianBuf_[medianIdx_ % 3] = rawPitch;
        ++medianIdx_;
        const float medianPitch = median3(medianBuf_[0], medianBuf_[1], medianBuf_[2]);

        currentPitchHz_ = medianPitch;
        confidence_ = rawConf;

        // Exponential smoothing weighted by confidence
        // On onset, snap immediately; otherwise smooth proportional to confidence
        if (medianPitch > 0.0f)
        {
            if (onsetDetected_ || smoothedPitchHz_ <= 0.0f)
            {
                // Instant lock on onset or first voiced frame
                smoothedPitchHz_ = medianPitch;
            }
            else
            {
                // Smooth: higher confidence = faster tracking
                // alpha ranges from 0.3 (low conf) to 0.85 (high conf)
                const float alpha = 0.3f + 0.55f * confidence_;
                smoothedPitchHz_ += (medianPitch - smoothedPitchHz_) * alpha;
            }
        }
    }

    // -----------------------------------------------------------------------
    // YIN core algorithm (optimized: linear buffer + limited tau range)
    // -----------------------------------------------------------------------
    float runYIN(float& outConfidence)
    {
        const int W = halfWindow_;

        // Linearize the ring buffer for cache-friendly access
        const int start = (ringWritePos_ - windowSize_ + (int)inputRing_.size() * 2)
                          % (int)inputRing_.size();
        for (int i = 0; i < windowSize_; ++i)
            linearBuf_[(size_t)i] = inputRing_[(size_t)((start + i) % windowSize_)];

        const float* buf = linearBuf_.data();

        // Step 1 & 2: Difference function + Cumulative mean normalized
        yinBuf_[0] = 1.0f;
        float runningSum = 0.0f;

        float bestVal = 100.0f;
        int   bestTau = -1;

        for (int tau = 1; tau <= maxTau_ && tau < W; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                const float delta = buf[j] - buf[j + tau];
                sum += delta * delta;
            }

            runningSum += sum;
            const float cmndf = (runningSum > 0.0f)
                              ? sum * (float)tau / runningSum
                              : 1.0f;
            yinBuf_[(size_t)tau] = cmndf;

            // Track global minimum for confidence even if above threshold
            if (tau >= minTau_ && cmndf < bestVal)
            {
                bestVal = cmndf;
                bestTau = tau;
            }
        }

        // Step 3: Absolute threshold — find first dip below threshold
        int tauEstimate = -1;
        for (int tau = std::max(2, minTau_); tau <= maxTau_ && tau < W; ++tau)
        {
            if (yinBuf_[(size_t)tau] < kYinThreshold)
            {
                // Find local minimum
                while (tau + 1 <= maxTau_ && tau + 1 < W
                       && yinBuf_[(size_t)(tau + 1)] < yinBuf_[(size_t)tau])
                    ++tau;
                tauEstimate = tau;
                break;
            }
        }

        // If no tau below threshold, use global minimum if it's reasonable
        if (tauEstimate < 1)
        {
            if (bestTau > 0 && bestVal < 0.35f)
                tauEstimate = bestTau;
            else
            {
                outConfidence = 0.0f;
                return 0.0f; // unvoiced
            }
        }

        // Confidence: 1.0 - CMNDF value at the chosen tau
        // Lower CMNDF = more periodic = higher confidence
        outConfidence = std::clamp(1.0f - yinBuf_[(size_t)tauEstimate], 0.0f, 1.0f);

        // Step 4: Parabolic interpolation for sub-sample accuracy
        const float betterTau = parabolicInterp(tauEstimate);

        if (betterTau <= 0.0f)
        {
            outConfidence = 0.0f;
            return 0.0f;
        }

        const float freq = (float)(sampleRate_ / (double)betterTau);

        // Reject out-of-range
        if (freq < kMinFreq || freq > kMaxFreq)
        {
            outConfidence = 0.0f;
            return 0.0f;
        }

        return freq;
    }

    float parabolicInterp(int tau) const
    {
        if (tau < 1 || tau >= halfWindow_ - 1)
            return (float)tau;

        const float s0 = yinBuf_[(size_t)(tau - 1)];
        const float s1 = yinBuf_[(size_t)tau];
        const float s2 = yinBuf_[(size_t)(tau + 1)];

        const float denom = 2.0f * (2.0f * s1 - s2 - s0);
        if (std::abs(denom) < 1e-12f)
            return (float)tau;

        return (float)tau + (s0 - s2) / denom;
    }

    static float median3(float a, float b, float c)
    {
        if (a > b) std::swap(a, b);
        if (b > c) std::swap(b, c);
        if (a > b) std::swap(a, b);
        return b;
    }
};
