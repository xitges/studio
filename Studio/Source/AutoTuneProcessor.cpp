/*
  ==============================================================================

    AutoTuneProcessor.cpp
    Created: 22 Mar 2026
    Author:  홍준영

  ==============================================================================
*/

#include "AutoTuneProcessor.h"
#include "Audio/RubberBandStretcher.h"
#include <cmath>
#include <algorithm>

using RBS = RubberBand::RubberBandStretcher;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AutoTuneProcessor::AutoTuneProcessor() = default;
AutoTuneProcessor::~AutoTuneProcessor() = default;

AutoTuneProcessor::AutoTuneProcessor(AutoTuneProcessor&&) noexcept = default;
AutoTuneProcessor& AutoTuneProcessor::operator=(AutoTuneProcessor&&) noexcept = default;

// ---------------------------------------------------------------------------
// Prime stretcher: feed start padding + drain processed silence
// ---------------------------------------------------------------------------

void AutoTuneProcessor::primeStretcher()
{
    if (stretcher_ == nullptr) return;

    const size_t startPad = stretcher_->getPreferredStartPad();
    if (startPad > 0)
    {
        std::vector<float> silence(startPad, 0.0f);
        float* silPtrs[2] = { silence.data(), silence.data() };
        stretcher_->process(silPtrs, startPad, false);

        const int avail = (int)stretcher_->available();
        if (avail > 0)
        {
            std::vector<float> drain((size_t)avail, 0.0f);
            float* drainPtrs[2] = { drain.data(), drain.data() };
            stretcher_->retrieve(drainPtrs, (size_t)avail);
        }
    }
}

// ---------------------------------------------------------------------------
// Output FIFO helpers
// ---------------------------------------------------------------------------

void AutoTuneProcessor::pushToOutFifo(const float* L, const float* R, int count)
{
    for (int i = 0; i < count && outFifoSize_ < outFifoCapacity_; ++i)
    {
        outFifoL_[(size_t)outFifoWritePos_] = L[i];
        outFifoR_[(size_t)outFifoWritePos_] = R[i];
        outFifoWritePos_ = (outFifoWritePos_ + 1) % outFifoCapacity_;
        ++outFifoSize_;
    }
}

int AutoTuneProcessor::pullFromOutFifo(float* L, float* R, int count)
{
    const int toPull = std::min(count, outFifoSize_);
    for (int i = 0; i < toPull; ++i)
    {
        L[i] = outFifoL_[(size_t)outFifoReadPos_];
        R[i] = outFifoR_[(size_t)outFifoReadPos_];
        outFifoReadPos_ = (outFifoReadPos_ + 1) % outFifoCapacity_;
    }
    outFifoSize_ -= toPull;
    return toPull;
}

// ---------------------------------------------------------------------------
// prepare / reset
// ---------------------------------------------------------------------------

void AutoTuneProcessor::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = maxBlockSize;

    pitchDetector_.prepare(sampleRate, maxBlockSize);

    // RubberBand real-time mode — NO OptionWindowShort (causes artifacts on tonal material)
    const int opts = RBS::OptionProcessRealTime
                   | RBS::OptionPitchHighQuality
                   | RBS::OptionFormantPreserved;

    stretcher_ = std::make_unique<RBS>((size_t)sampleRate, 2, opts, 1.0, 1.0);
    stretcher_->setMaxProcessSize((size_t)maxBlockSize);

    primeStretcher();

    // Pre-allocate work buffers
    const size_t sz = (size_t)maxBlockSize;
    monoBuf_.resize(sz, 0.0f);
    dryL_.resize(sz, 0.0f);
    dryR_.resize(sz, 0.0f);

    // RubberBand may return up to 2x input in some cases
    retrieveL_.resize(sz * 4, 0.0f);
    retrieveR_.resize(sz * 4, 0.0f);

    // Output FIFO — 8 blocks worth of buffering for latency compensation
    outFifoCapacity_ = maxBlockSize * 8;
    outFifoL_.resize((size_t)outFifoCapacity_, 0.0f);
    outFifoR_.resize((size_t)outFifoCapacity_, 0.0f);
    outFifoReadPos_  = 0;
    outFifoWritePos_ = 0;
    outFifoSize_     = 0;

    currentPitchRatio_  = 1.0f;
    targetPitchRatio_   = 1.0f;
    lastFormantPreserve_ = true;
    prevDetectedHz_     = 0.0f;
    stablePitchCount_   = 0;
}

void AutoTuneProcessor::reset()
{
    pitchDetector_.reset();

    if (stretcher_)
    {
        stretcher_->reset();
        primeStretcher();
    }

    if (!dryL_.empty())      std::fill(dryL_.begin(),      dryL_.end(),      0.0f);
    if (!dryR_.empty())      std::fill(dryR_.begin(),      dryR_.end(),      0.0f);
    if (!retrieveL_.empty()) std::fill(retrieveL_.begin(), retrieveL_.end(), 0.0f);
    if (!retrieveR_.empty()) std::fill(retrieveR_.begin(), retrieveR_.end(), 0.0f);

    outFifoReadPos_  = 0;
    outFifoWritePos_ = 0;
    outFifoSize_     = 0;

    currentPitchRatio_  = 1.0f;
    targetPitchRatio_   = 1.0f;
    detectedPitchHz_    = 0.0f;
    targetPitchHz_      = 0.0f;
    inputRms_           = 0.0f;
    prevDetectedHz_     = 0.0f;
    stablePitchCount_   = 0;
}

// ---------------------------------------------------------------------------
// processBlock — called from audio thread
// ---------------------------------------------------------------------------

void AutoTuneProcessor::processBlock(float* L, float* R, int numSamples,
                                     const AutoTuneParams& params)
{
    if (!params.enabled || stretcher_ == nullptr || numSamples <= 0)
        return;

    // Update formant option if changed
    if (params.formantPreserve != lastFormantPreserve_)
    {
        stretcher_->setFormantOption(params.formantPreserve
                                    ? RBS::OptionFormantPreserved
                                    : RBS::OptionFormantShifted);
        lastFormantPreserve_ = params.formantPreserve;
    }

    // --- Save original input (for dry mix) ---
    for (int i = 0; i < numSamples; ++i)
    {
        dryL_[(size_t)i] = L[i];
        dryR_[(size_t)i] = R[i];
    }

    // --- Pitch detection (mono mix) ---
    float energy = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        monoBuf_[(size_t)i] = (L[i] + R[i]) * 0.5f;
        energy += monoBuf_[(size_t)i] * monoBuf_[(size_t)i];
    }
    inputRms_ = std::sqrt(energy / (float)numSamples);

    pitchDetector_.processBlock(monoBuf_.data(), numSamples);
    const float detectedHz = pitchDetector_.getPitchHz();
    detectedPitchHz_ = detectedHz;

    // --- Compute target pitch ratio with stability check ---
    if (detectedHz > 0.0f && !std::isnan(detectedHz) && !std::isinf(detectedHz))
    {
        // Stability: require pitch to be consistent for a few readings
        // before applying correction (prevents crackling from jittery detection)
        const float hzDiff = std::abs(detectedHz - prevDetectedHz_);
        const float hzThreshold = prevDetectedHz_ * 0.05f; // 5% tolerance (~1 semitone)

        if (prevDetectedHz_ > 0.0f && hzDiff < hzThreshold)
            ++stablePitchCount_;
        else
            stablePitchCount_ = 0;

        prevDetectedHz_ = detectedHz;

        // Only compute correction once pitch is stable (at least 2 consistent readings)
        if (stablePitchCount_ >= 1)
        {
            const float detectedMidi = hzToMidi(detectedHz);
            const float targetMidi   = snapToScale(detectedMidi, params.keyTonic, params.scaleType);
            const float targetHz     = midiToHz(targetMidi);
            targetPitchHz_ = targetHz;

            float ratio = targetHz / detectedHz;
            ratio = std::clamp(ratio, 0.5f, 2.0f);
            targetPitchRatio_ = ratio;
        }
    }
    else
    {
        targetPitchHz_ = 0.0f;
        // Smoothly return to unity when unvoiced
        targetPitchRatio_ = 1.0f;
        prevDetectedHz_ = 0.0f;
        stablePitchCount_ = 0;
    }

    // --- Apply retune speed smoothing (per-sample for smooth transitions) ---
    // retuneSpeed 0 = instant, 1 = very slow
    // Smoothing coefficient per block (not per sample — avoids overshooting)
    const float speed = std::clamp(params.retuneSpeed, 0.0f, 0.999f);
    const float smoothCoef = 1.0f - speed * speed; // quadratic curve for more natural feel
    currentPitchRatio_ += (targetPitchRatio_ - currentPitchRatio_) * smoothCoef;

    // --- RubberBand pitch shift ---
    stretcher_->setPitchScale((double)currentPitchRatio_);

    float* inPtrs[2] = { dryL_.data(), dryR_.data() };
    stretcher_->process(inPtrs, (size_t)numSamples, false);

    // Retrieve ALL available output into FIFO (not just numSamples)
    const int avail = (int)stretcher_->available();
    if (avail > 0)
    {
        const int toRetrieve = std::min(avail, (int)retrieveL_.size());
        float* outPtrs[2] = { retrieveL_.data(), retrieveR_.data() };
        stretcher_->retrieve(outPtrs, (size_t)toRetrieve);
        pushToOutFifo(retrieveL_.data(), retrieveR_.data(), toRetrieve);
    }

    // --- Pull from FIFO and apply dry/wet mix ---
    const float wet = std::clamp(params.mix, 0.0f, 1.0f);
    const float dry = 1.0f - wet;

    // Try to pull numSamples from FIFO
    const int pulled = pullFromOutFifo(L, R, numSamples);

    // Apply dry/wet mix for pulled samples
    for (int i = 0; i < pulled; ++i)
    {
        L[i] = dryL_[(size_t)i] * dry + L[i] * wet;
        R[i] = dryR_[(size_t)i] * dry + R[i] * wet;
    }

    // If FIFO didn't have enough, use dry signal for remaining (startup only)
    for (int i = pulled; i < numSamples; ++i)
    {
        L[i] = dryL_[(size_t)i];
        R[i] = dryR_[(size_t)i];
    }
}

// ---------------------------------------------------------------------------
// Pitch helpers
// ---------------------------------------------------------------------------

float AutoTuneProcessor::hzToMidi(float hz) const
{
    return 12.0f * std::log2(hz / 440.0f) + 69.0f;
}

float AutoTuneProcessor::midiToHz(float midi) const
{
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

float AutoTuneProcessor::snapToScale(float midiNote, int keyTonic, int scaleType) const
{
    const int roundedMidi = (int)std::round(midiNote);

    KeySignature key;
    key.tonic = keyTonic;
    key.scale = static_cast<ScaleType>(scaleType);

    const int snappedMidi = MusicTheory::snapPitchToScale(roundedMidi, key);
    return (float)snappedMidi;
}
