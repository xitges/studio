/*
  ==============================================================================

    PSOLAPitchShifter.h
    Created: 23 Mar 2026
    Author:  홍준영

    Time-Domain Pitch-Synchronous Overlap-Add (TD-PSOLA) pitch shifter.
    Designed specifically for monophonic vocal pitch correction (Auto-Tune).

    Algorithm:
      1. Accumulate input into ring buffer
      2. Detect pitch marks aligned to pitch period boundaries
      3. Extract Hanning-windowed grains (2× pitch period)
      4. Optionally resample grains for formant compensation
      5. Place grains at new spacing (period / pitchRatio) via overlap-add
      6. Read output from accumulator

    Properties:
      - Latency: ~1 pitch period (5-10ms for voice)
      - CPU: minimal (no FFT)
      - Formant control: continuous 0-1 via grain resampling
      - Audio-thread safe: all buffers pre-allocated

  ==============================================================================
*/

#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

class PSOLAPitchShifter
{
public:
    PSOLAPitchShifter() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Input ring: must hold several pitch periods.
        // Longest period at 50 Hz / 44100 Hz = 882 samples.
        // We need at least 4 periods for lookahead + extraction.
        inputRingSize_ = 8192;
        inputRing_.assign((size_t)inputRingSize_, 0.0f);
        inputWritePos_ = 0;
        inputAvailable_ = 0;

        // Output accumulator: must hold enough for overlap-add of multiple grains.
        // Max grain size ~1764 samples, grains overlap by 50%, plus output block size.
        outputAccumSize_ = 8192;
        outputAccum_.assign((size_t)outputAccumSize_, 0.0f);
        outputWritePos_ = 0;
        outputReadPos_ = 0;
        outputAvailable_ = 0;

        // Max grain length: 2 × max period (~1764 at 50Hz/44.1kHz), round up
        maxGrainLen_ = 2048;
        grainBuf_.resize((size_t)maxGrainLen_, 0.0f);
        resampleBuf_.resize((size_t)maxGrainLen_, 0.0f);

        // Pre-compute Hanning windows for common grain sizes.
        // We'll recompute on the fly for the actual grain length.
        windowBuf_.resize((size_t)maxGrainLen_, 0.0f);

        // State
        currentPeriodSamples_ = 220.0f; // default ~200 Hz
        synthPhase_ = 0.0f;
        analysisPos_ = 0.0f;
        prevPitchRatio_ = 1.0f;

        // Crossfade for voiced/unvoiced transitions
        crossfadeCounter_ = 0;
        wasVoiced_ = false;
    }

    /**
     * Set the detected pitch period in samples.
     * Call this before processBlock each time pitch detection updates.
     * period = sampleRate / detectedHz
     */
    void setPitchPeriod(float periodSamples)
    {
        if (periodSamples > 20.0f && periodSamples < 1200.0f)
        {
            // Smooth period changes to avoid grain size jumps
            const float alpha = 0.3f;
            currentPeriodSamples_ += (periodSamples - currentPeriodSamples_) * alpha;
        }
    }

    /**
     * Process a block of mono audio.
     *
     * @param in          Input mono samples
     * @param out         Output mono samples (pitch-shifted)
     * @param numSamples  Block size
     * @param pitchRatio  Target pitch ratio (>1 = higher, <1 = lower)
     * @param formantAmount 0=formants shift with pitch, 1=full formant preservation
     * @param voiced      True if signal is currently voiced (periodic)
     */
    void processBlock(const float* in, float* out, int numSamples,
                      float pitchRatio, float formantAmount, bool voiced)
    {
        // Clamp ratio to safe range
        pitchRatio = std::max(0.5f, std::min(pitchRatio, 2.0f));

        // Write input into ring buffer
        for (int i = 0; i < numSamples; ++i)
        {
            inputRing_[(size_t)inputWritePos_] = in[i];
            inputWritePos_ = (inputWritePos_ + 1) % inputRingSize_;
            inputAvailable_++;
        }

        if (!voiced || pitchRatio == 1.0f)
        {
            // Unvoiced or unity ratio: pass through with crossfade
            if (wasVoiced_)
            {
                crossfadeCounter_ = kCrossfadeSamples;
                wasVoiced_ = false;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                const float drySample = in[i];

                if (crossfadeCounter_ > 0)
                {
                    // Crossfade from PSOLA output to dry
                    const float t = (float)crossfadeCounter_ / (float)kCrossfadeSamples;
                    const float psolaOut = readOutputAccum();
                    out[i] = psolaOut * t + drySample * (1.0f - t);
                    crossfadeCounter_--;
                }
                else
                {
                    out[i] = drySample;
                    // Drain accumulator to stay in sync
                    drainOutputAccum(1);
                }
            }

            // Keep analysis position tracking the input
            analysisPos_ += (float)numSamples;
            return;
        }

        // Voiced processing: TD-PSOLA
        if (!wasVoiced_)
        {
            crossfadeCounter_ = kCrossfadeSamples;
            wasVoiced_ = true;
            // Reset synthesis phase on voice onset
            synthPhase_ = 0.0f;
        }

        // === Generate PSOLA output grains into accumulator ===
        const float analysisPeriod = currentPeriodSamples_;
        const float synthesisPeriod = analysisPeriod / pitchRatio;
        const int grainLen = std::max(4, std::min((int)(analysisPeriod * 2.0f), maxGrainLen_ - 1));

        // How many output samples we need to produce
        int samplesNeeded = numSamples;

        // Generate grains until we have enough output
        while (outputAvailable_ < samplesNeeded + grainLen)
        {
            // Check if we have enough input to extract a grain
            if (inputAvailable_ < grainLen + (int)analysisPeriod)
                break;

            // Extract grain centered at current analysis position
            extractGrain(grainLen);

            // Apply Hanning window
            applyHanningWindow(grainBuf_.data(), grainLen);

            // Formant compensation: resample grain if needed
            if (formantAmount > 0.01f && std::abs(pitchRatio - 1.0f) > 0.01f)
            {
                // To preserve formants when changing pitch:
                // Resample the grain by 1/pitchRatio to shift its spectral
                // envelope back to the original position.
                // Full formant preserve = resample by full 1/pitchRatio
                // Partial = interpolate between original and resampled
                const float resampleFactor = 1.0f + (1.0f / pitchRatio - 1.0f) * formantAmount;
                const int resampledLen = std::max(4, std::min((int)(grainLen * resampleFactor), maxGrainLen_ - 1));

                resampleGrain(grainBuf_.data(), grainLen,
                              resampleBuf_.data(), resampledLen);

                // Place resampled grain into output accumulator
                addGrainToOutput(resampleBuf_.data(), resampledLen);
            }
            else
            {
                // No formant compensation: place grain directly
                addGrainToOutput(grainBuf_.data(), grainLen);
            }

            // Advance analysis position by one analysis period
            analysisPos_ += analysisPeriod;
            inputAvailable_ -= (int)analysisPeriod;

            // Advance synthesis position by one synthesis period
            // (this determines output grain spacing)
            synthPhase_ += synthesisPeriod;
        }

        // === Read output ===
        for (int i = 0; i < numSamples; ++i)
        {
            float psolaOut = readOutputAccum();

            if (crossfadeCounter_ > 0)
            {
                // Crossfade from dry to PSOLA on voice onset
                const float t = (float)crossfadeCounter_ / (float)kCrossfadeSamples;
                psolaOut = in[i] * t + psolaOut * (1.0f - t);
                crossfadeCounter_--;
            }

            out[i] = psolaOut;
        }

        prevPitchRatio_ = pitchRatio;
    }

    void reset()
    {
        std::fill(inputRing_.begin(), inputRing_.end(), 0.0f);
        std::fill(outputAccum_.begin(), outputAccum_.end(), 0.0f);
        inputWritePos_ = 0;
        inputAvailable_ = 0;
        outputWritePos_ = 0;
        outputReadPos_ = 0;
        outputAvailable_ = 0;
        synthPhase_ = 0.0f;
        analysisPos_ = 0.0f;
        currentPeriodSamples_ = 220.0f;
        prevPitchRatio_ = 1.0f;
        crossfadeCounter_ = 0;
        wasVoiced_ = false;
    }

private:
    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 512;

    // Input ring buffer
    std::vector<float> inputRing_;
    int inputRingSize_ = 8192;
    int inputWritePos_ = 0;
    int inputAvailable_ = 0;

    // Output overlap-add accumulator
    std::vector<float> outputAccum_;
    int outputAccumSize_ = 8192;
    int outputWritePos_ = 0;
    int outputReadPos_ = 0;
    int outputAvailable_ = 0;

    // Grain buffers
    int maxGrainLen_ = 2048;
    std::vector<float> grainBuf_;
    std::vector<float> resampleBuf_;
    std::vector<float> windowBuf_;

    // PSOLA state
    float currentPeriodSamples_ = 220.0f;
    float synthPhase_ = 0.0f;
    float analysisPos_ = 0.0f;
    float prevPitchRatio_ = 1.0f;

    // Voiced/unvoiced crossfade
    static constexpr int kCrossfadeSamples = 64;
    int  crossfadeCounter_ = 0;
    bool wasVoiced_ = false;

    // -----------------------------------------------------------------------
    // Extract a grain from the input ring buffer, centered on current
    // analysis position. Grain length = grainLen samples.
    // -----------------------------------------------------------------------
    void extractGrain(int grainLen)
    {
        // The read position in the ring: inputWritePos_ - inputAvailable_ + offset
        const int ringBase = ((inputWritePos_ - inputAvailable_) % inputRingSize_
                              + inputRingSize_) % inputRingSize_;

        for (int i = 0; i < grainLen; ++i)
        {
            const int ringIdx = (ringBase + i) % inputRingSize_;
            grainBuf_[(size_t)i] = inputRing_[(size_t)ringIdx];
        }
    }

    // -----------------------------------------------------------------------
    // Apply Hanning window in-place
    // -----------------------------------------------------------------------
    void applyHanningWindow(float* data, int len)
    {
        const float invLen = 1.0f / (float)(len - 1);
        for (int i = 0; i < len; ++i)
        {
            // Hanning: 0.5 * (1 - cos(2*pi*i/(N-1)))
            const float w = 0.5f * (1.0f - std::cos(6.283185307f * (float)i * invLen));
            data[i] *= w;
        }
    }

    // -----------------------------------------------------------------------
    // Resample a grain using cubic (Hermite) interpolation.
    // Used for formant compensation: resample by 1/pitchRatio to undo
    // the spectral envelope shift caused by period change.
    // -----------------------------------------------------------------------
    void resampleGrain(const float* src, int srcLen, float* dst, int dstLen)
    {
        if (srcLen <= 1 || dstLen <= 1)
        {
            for (int i = 0; i < dstLen; ++i) dst[i] = 0.0f;
            return;
        }

        const float ratio = (float)(srcLen - 1) / (float)(dstLen - 1);

        for (int i = 0; i < dstLen; ++i)
        {
            const float srcPos = (float)i * ratio;
            const int idx = (int)srcPos;
            const float frac = srcPos - (float)idx;

            // Hermite cubic interpolation (Catmull-Rom)
            const float s0 = (idx > 0) ? src[idx - 1] : src[0];
            const float s1 = src[std::min(idx, srcLen - 1)];
            const float s2 = src[std::min(idx + 1, srcLen - 1)];
            const float s3 = src[std::min(idx + 2, srcLen - 1)];

            const float c0 = s1;
            const float c1 = 0.5f * (s2 - s0);
            const float c2 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
            const float c3 = 0.5f * (s3 - s0) + 1.5f * (s1 - s2);

            dst[i] = ((c3 * frac + c2) * frac + c1) * frac + c0;
        }
    }

    // -----------------------------------------------------------------------
    // Add a windowed grain into the output accumulator via overlap-add
    // -----------------------------------------------------------------------
    void addGrainToOutput(const float* grain, int len)
    {
        for (int i = 0; i < len; ++i)
        {
            const int pos = (outputWritePos_ + i) % outputAccumSize_;
            outputAccum_[(size_t)pos] += grain[i];
        }

        // Advance write position by synthesis period (half a grain for 50% overlap)
        const int advance = std::max(1, len / 2);
        outputWritePos_ = (outputWritePos_ + advance) % outputAccumSize_;
        outputAvailable_ += advance;
    }

    // -----------------------------------------------------------------------
    // Read one sample from output accumulator
    // -----------------------------------------------------------------------
    float readOutputAccum()
    {
        if (outputAvailable_ <= 0)
            return 0.0f;

        const float val = outputAccum_[(size_t)outputReadPos_];
        outputAccum_[(size_t)outputReadPos_] = 0.0f; // clear after read
        outputReadPos_ = (outputReadPos_ + 1) % outputAccumSize_;
        outputAvailable_--;
        return val;
    }

    // -----------------------------------------------------------------------
    // Drain N samples from output accumulator (discard)
    // -----------------------------------------------------------------------
    void drainOutputAccum(int count)
    {
        const int toDrain = std::min(count, outputAvailable_);
        for (int i = 0; i < toDrain; ++i)
        {
            outputAccum_[(size_t)outputReadPos_] = 0.0f;
            outputReadPos_ = (outputReadPos_ + 1) % outputAccumSize_;
        }
        outputAvailable_ -= toDrain;
    }
};
