/*
  ==============================================================================

    FXProcessor.h  — M14 Built-in FX Chain
    Per mixer track: Compressor → Delay → Reverb

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include "ProjectModel.h"

// ---------------------------------------------------------------------------
// FXChain — processes one mixer track's stereo bus
// ---------------------------------------------------------------------------
class FXChain
{
public:
    void prepare(double sr, int /*maxBlock*/)
    {
        sampleRate_ = sr;

        // Delay buffer: up to 2 seconds stereo
        const int maxDelay = (int)(sr * 2.0) + 1;
        delayBufL_.assign((size_t)maxDelay, 0.0f);
        delayBufR_.assign((size_t)maxDelay, 0.0f);
        delayWritePos_ = 0;

        // Reverb
        reverb_.reset();
        reverb_.setSampleRate(sr);

        compEnvL_ = compEnvR_ = 0.0f;
    }

    void processBlock(juce::AudioBuffer<float>& buf, int numSamples,
                      const FXParams& p, double bpm)
    {
        if (buf.getNumChannels() < 2) return;

        float* L = buf.getWritePointer(0);
        float* R = buf.getWritePointer(1);

        if (p.compEnabled)  applyCompressor(L, R, numSamples, p);
        if (p.delayEnabled)  applyDelay    (L, R, numSamples, p, bpm);
        if (p.reverbEnabled) applyReverb   (L, R, numSamples, p);
    }

    void reset()
    {
        if (!delayBufL_.empty()) std::fill(delayBufL_.begin(), delayBufL_.end(), 0.0f);
        if (!delayBufR_.empty()) std::fill(delayBufR_.begin(), delayBufR_.end(), 0.0f);
        delayWritePos_ = 0;
        reverb_.reset();
        compEnvL_ = compEnvR_ = 0.0f;
    }

private:
    double sampleRate_ = 44100.0;

    // Delay
    std::vector<float> delayBufL_, delayBufR_;
    int                delayWritePos_ = 0;

    // Reverb (JUCE Freeverb — in juce_audio_basics, no juce_dsp needed)
    juce::Reverb reverb_;

    // Compressor envelope followers
    float compEnvL_ = 0.0f;
    float compEnvR_ = 0.0f;

    // -------------------------------------------------------------------------
    void applyCompressor(float* L, float* R, int numSamples, const FXParams& p)
    {
        const float thresh      = juce::Decibels::decibelsToGain(p.compThreshDB);
        const float ratio       = juce::jmax(1.0f, p.compRatio);
        const float attackCoef  = std::exp(-1.0f / juce::jmax(1.0f, p.compAttackMs  * 0.001f * (float)sampleRate_));
        const float releaseCoef = std::exp(-1.0f / juce::jmax(1.0f, p.compReleaseMs * 0.001f * (float)sampleRate_));

        for (int i = 0; i < numSamples; ++i)
        {
            auto envelopeFollow = [&](float in, float& env) {
                const float abs_in = std::abs(in);
                env = (abs_in > env)
                    ? attackCoef  * env + (1.0f - attackCoef)  * abs_in
                    : releaseCoef * env + (1.0f - releaseCoef) * abs_in;
            };

            envelopeFollow(L[i], compEnvL_);
            envelopeFollow(R[i], compEnvR_);

            auto gainReduce = [&](float env, float& sample) {
                if (env > thresh && thresh > 0.0f)
                {
                    const float dbIn  = juce::Decibels::gainToDecibels(env);
                    const float dbOut = p.compThreshDB + (dbIn - p.compThreshDB) / ratio;
                    sample *= juce::Decibels::decibelsToGain(dbOut - dbIn);
                }
            };

            gainReduce(compEnvL_, L[i]);
            gainReduce(compEnvR_, R[i]);
        }
    }

    // -------------------------------------------------------------------------
    void applyDelay(float* L, float* R, int numSamples, const FXParams& p, double bpm)
    {
        if (delayBufL_.empty()) return;
        const int bufLen = (int)delayBufL_.size();

        const double samplesPerBeat = sampleRate_ * 60.0 / juce::jmax(1.0, bpm);
        const int delaySamples = juce::jlimit(1, bufLen - 1,
                                              (int)(p.delayBeats * samplesPerBeat));

        for (int i = 0; i < numSamples; ++i)
        {
            int readPos = delayWritePos_ - delaySamples;
            if (readPos < 0) readPos += bufLen;

            const float delL = delayBufL_[(size_t)readPos];
            const float delR = delayBufR_[(size_t)readPos];

            delayBufL_[(size_t)delayWritePos_] = L[i] + delL * p.delayFeedback;
            delayBufR_[(size_t)delayWritePos_] = R[i] + delR * p.delayFeedback;

            L[i] = L[i] * (1.0f - p.delayMix) + delL * p.delayMix;
            R[i] = R[i] * (1.0f - p.delayMix) + delR * p.delayMix;

            delayWritePos_ = (delayWritePos_ + 1) % bufLen;
        }
    }

    // -------------------------------------------------------------------------
    void applyReverb(float* L, float* R, int numSamples, const FXParams& p)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize  = p.reverbRoom;
        rp.damping   = p.reverbDamp;
        rp.wetLevel  = p.reverbWet;
        rp.dryLevel  = 1.0f - p.reverbWet;
        rp.width     = p.reverbWidth;
        reverb_.setParameters(rp);
        reverb_.processStereo(L, R, numSamples);
    }
};
