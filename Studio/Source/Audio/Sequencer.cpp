/*
  ==============================================================================

    Sequencer.cpp
    Created: 6 Mar 2026 12:02:09pm
    Author:  홍준영

  ==============================================================================
*/

#include "Sequencer.h"

Sequencer::Sequencer(TriggerCallback cb)
    : onTrigger(std::move(cb))
{
}

void Sequencer::prepare(double sr, int)
{
    sampleRate = sr;
    recalcSamplesPerStep();
}

void Sequencer::start()
{
    currentStep   = 0;
    sampleCounter = 0.0;
    playing       = true;

    for (int ch = 0; ch < CHANNEL_COUNT; ++ch)
        if (pattern[ch][currentStep])
            onTrigger(ch, 0);
}

void Sequencer::stop()
{
    playing = false;
}

void Sequencer::setBPM(double newBpm)
{
    bpm = newBpm;
    recalcSamplesPerStep();
}

void Sequencer::setStep(int channel, int step, bool active)
{
    if (channel >= 0 && channel < CHANNEL_COUNT &&
        step    >= 0 && step    < stepCount &&
        step    <  MAX_STEPS)
        pattern[channel][step] = active;
}

void Sequencer::setStepCount(int newCount)
{
    stepCount = juce::jlimit(1, MAX_STEPS, newCount);
    if (currentStep >= stepCount)
        currentStep = 0;
}

void Sequencer::recalcSamplesPerStep()
{
    // 4/4박자 기준 16분음표
    // 1분 = bpm 박자 → 1박 = 60/bpm 초 → 16분음표 = (60/bpm)/4 초
    samplesPerStep = (60.0 / bpm / 4.0) * sampleRate;
}

void Sequencer::processBlock(int numSamples)
{
    if (!playing) return;

    const double prevCounter = sampleCounter;
    sampleCounter += numSamples;

    // timeToFire: samples from the start of this buffer until the next step fires
    double timeToFire = samplesPerStep - prevCounter;

    while (sampleCounter >= samplesPerStep)
    {
        sampleCounter -= samplesPerStep;
        const int offsetInBuffer = juce::jlimit(0, numSamples - 1, (int)timeToFire);
        advanceStep(offsetInBuffer);
        timeToFire += samplesPerStep;
    }
}

void Sequencer::advanceStep(int offsetInBuffer)
{
    if (stepCount <= 0)
        return;

    currentStep = (currentStep + 1) % stepCount;

    for (int ch = 0; ch < CHANNEL_COUNT; ++ch)
        if (pattern[ch][currentStep])
            onTrigger(ch, offsetInBuffer);
}

