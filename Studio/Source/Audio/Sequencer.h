/*
  ==============================================================================

    Sequencer.h
    Created: 6 Mar 2026 12:02:09pm
    Author:  홍준영

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <functional>
#include "../ProjectModel.h"

class Sequencer
{
public:
    using TriggerCallback = std::function<void(int channelIndex, int step, int offsetInBuffer)>;

    explicit Sequencer(TriggerCallback cb);

    void prepare(double sampleRate, int bufferSize);
    void start();
    void stop();
    void setBPM(double bpm);
    void setStep(int channel, int step, bool active);
    void setStepCount(int newCount);

    bool isPlaying() const { return playing; }
    int  getCurrentStep() const { return currentStep; }
    int  getStepCount() const { return stepCount; }

    // AudioEngine 콜백에서 매 버퍼마다 호출
    void processBlock(int numSamples);

private:
    TriggerCallback onTrigger;

    static constexpr int CHANNEL_COUNT = 16;
    static constexpr int MAX_STEPS     = kMaxPatternSteps;

    std::array<std::array<bool, MAX_STEPS>, CHANNEL_COUNT> pattern {};

    double sampleRate       = 44100.0;
    double bpm              = 140.0;
    int    currentStep      = 0;
    int    stepCount        = 16;
    double samplesPerStep   = 0.0;
    double sampleCounter    = 0.0;
    bool   playing          = false;
    bool   fireStepZeroOnNextBlock = false;

    void advanceStep(int offsetInBuffer);
    void triggerCurrentStep(int offsetInBuffer);
    void recalcSamplesPerStep();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Sequencer)
};

