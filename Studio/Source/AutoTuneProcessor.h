/*
  ==============================================================================

    AutoTuneProcessor.h
    Created: 22 Mar 2026
    Author:  홍준영

    Real-time auto-tune effect using YIN pitch detection + RubberBand.
    Audio-thread safe: all buffers pre-allocated in prepare().

  ==============================================================================
*/

#pragma once
#include "ProjectModel.h"
#include "PitchDetector.h"

namespace RubberBand { class RubberBandStretcher; }

class AutoTuneProcessor
{
public:
    AutoTuneProcessor();
    ~AutoTuneProcessor();

    AutoTuneProcessor(AutoTuneProcessor&&) noexcept;
    AutoTuneProcessor& operator=(AutoTuneProcessor&&) noexcept;

    void prepare(double sampleRate, int maxBlockSize);
    void processBlock(float* L, float* R, int numSamples, const AutoTuneParams& params);
    void reset();

    /** Detected pitch in Hz (for UI metering). */
    float getDetectedPitchHz() const { return detectedPitchHz_; }

    /** Target pitch in Hz after scale snap (for UI). */
    float getTargetPitchHz() const { return targetPitchHz_; }

    /** Input RMS level (for UI diagnostics). */
    float getInputRms() const { return inputRms_; }

private:
    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 512;

    PitchDetector pitchDetector_;

    // RubberBand real-time stretcher (pimpl — header only forward-declares)
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher_;

    // Smoothed pitch ratio for retune speed control
    float currentPitchRatio_ = 1.0f;
    float targetPitchRatio_  = 1.0f;

    // Pre-allocated work buffers
    std::vector<float> monoBuf_;
    std::vector<float> dryL_, dryR_;
    std::vector<float> retrieveL_, retrieveR_;

    // Output FIFO for RubberBand latency compensation
    std::vector<float> outFifoL_, outFifoR_;
    int outFifoReadPos_  = 0;
    int outFifoWritePos_ = 0;
    int outFifoSize_     = 0;
    int outFifoCapacity_ = 0;

    // UI metering (read from message thread — relaxed atomic not needed for simple floats)
    float detectedPitchHz_ = 0.0f;
    float targetPitchHz_   = 0.0f;
    float inputRms_        = 0.0f;

    bool  lastFormantPreserve_ = true;

    // Pitch stability tracking
    float prevDetectedHz_ = 0.0f;
    int   stablePitchCount_ = 0;

    void primeStretcher();
    void pushToOutFifo(const float* L, const float* R, int count);
    int  pullFromOutFifo(float* L, float* R, int count);
    float hzToMidi(float hz) const;
    float midiToHz(float midi) const;
    float snapToScale(float midiNote, int keyTonic, int scaleType) const;
};
