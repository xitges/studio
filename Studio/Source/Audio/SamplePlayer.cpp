/*
  ==============================================================================

    SamplePlayer.cpp
    Created: 6 Mar 2026 12:02:03pm
    Author:  홍준영

  ==============================================================================
*/
#include "SamplePlayer.h"

SamplePlayer::SamplePlayer()
{
    formatManager.registerBasicFormats(); // WAV, AIFF, MP3
}

void SamplePlayer::loadFile(const juce::File& file)
{
    // Decode into a temporary buffer on the calling thread (no lock held yet)
    std::unique_ptr<juce::AudioFormatReader> newReader(
        formatManager.createReaderFor(file));

    juce::AudioBuffer<float> tmp;
    if (newReader != nullptr)
    {
        tmp.setSize((int)newReader->numChannels,
                    (int)newReader->lengthInSamples);
        newReader->read(&tmp, 0, (int)newReader->lengthInSamples, 0, true, true);
        DBG("Loaded: " << file.getFileName());
    }

    // Swap the decoded buffer in under the lock so the audio thread never
    // sees a partially-constructed fileBuffer.
    {
        juce::SpinLock::ScopedLockType lock(bufferLock);
        fileBuffer  = std::move(tmp);
        reader      = std::move(newReader);
        playPosition = -1.0;
    }
}

void SamplePlayer::trigger()
{
    triggerOffset_ = 0;
    triggered = true;
}

void SamplePlayer::triggerAt(int offsetInBuffer)
{
    triggerOffset_ = offsetInBuffer;
    triggered = true;
}

void SamplePlayer::prepare(double sr, int)
{
    playerSampleRate = sr;
    playPosition     = -1.0;

    // Initialise smoothed values with 15ms ramp
    smoothVolume_.reset(sr, 0.015);
    smoothVolume_.setCurrentAndTargetValue(volume);
    smoothPan_.reset(sr, 0.015);
    smoothPan_.setCurrentAndTargetValue(pan);

    // Recalculate ms-based params against new sample rate
    // (caller should re-set attack/release after prepare if needed)
}

void SamplePlayer::reset()
{
    playPosition = -1.0;
}

void SamplePlayer::setPitch(float semitones)
{
    // Frequency ratio: one octave up = 2x speed, one semitone = 2^(1/12)
    pitchRatio = std::pow(2.0f, semitones / 12.0f);
}

void SamplePlayer::setAttack(float ms)
{
    attackSamples = (float)(ms * 0.001 * playerSampleRate);
}

void SamplePlayer::setRelease(float ms)
{
    releaseSamples = (float)(ms * 0.001 * playerSampleRate);
}

void SamplePlayer::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                   int numSamples)
{
    // Non-blocking try-lock: if the main thread is loading a new file, skip
    // this block rather than blocking the real-time audio callback.
    juce::SpinLock::ScopedTryLockType tryLock(bufferLock);
    if (!tryLock.isLocked()) return;

    if (!isLoaded()) return;

    // Clamp to actual output buffer size so we never write out of bounds
    const int safeSamples = juce::jmin(numSamples, outputBuffer.getNumSamples());

    // Trigger detection
    if (triggered.exchange(false))
    {
        playPosition = 0.0;
        startOffset_ = triggerOffset_;
        triggerOffset_ = 0;
        envelopeGain = (attackSamples > 0.0f) ? 0.0f : 1.0f;
    }

    if (playPosition < 0.0 || muted) return;

    const int srcSamples  = fileBuffer.getNumSamples();
    const int outChannels = outputBuffer.getNumChannels();
    const int srcChannels = fileBuffer.getNumChannels();

    // Advance smoothers over the silent pre-trigger region so they stay in sync
    const int loopStart = juce::jlimit(0, safeSamples, startOffset_);
    if (loopStart > 0)
    {
        smoothVolume_.skip(loopStart);
        smoothPan_.skip(loopStart);
        startOffset_ = 0;
    }

    for (int i = loopStart; i < safeSamples; ++i)
    {
        const int pos0 = (int)playPosition;
        if (pos0 >= srcSamples)
        {
            // Drain smoothed values to avoid stale state on next trigger
            smoothVolume_.skip(safeSamples - i);
            smoothPan_.skip(safeSamples - i);
            playPosition = -1.0;
            break;
        }

        // Per-sample smoothed volume and pan (constant-power law)
        const float vol    = smoothVolume_.getNextValue();
        const float panVal = smoothPan_.getNextValue();
        const float angle  = (panVal + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gainL  = vol * std::cos(angle);
        const float gainR  = vol * std::sin(angle);

        // 4-point Hermite cubic interpolation (Catmull-Rom)
        // Much lower aliasing than linear when pitchRatio != 1.0
        const float frac = (float)(playPosition - (double)pos0);
        const int   pm1  = juce::jmax(pos0 - 1, 0);
        const int   pp1  = juce::jmin(pos0 + 1, srcSamples - 1);
        const int   pp2  = juce::jmin(pos0 + 2, srcSamples - 1);

        // Attack ramp
        if (attackSamples > 0.0f && envelopeGain < 1.0f)
            envelopeGain = juce::jmin(1.0f, envelopeGain + 1.0f / attackSamples);

        // Release fade at tail of sample
        float relMult = 1.0f;
        if (releaseSamples > 0.0f)
        {
            const float samplesLeft = (float)srcSamples - (float)playPosition;
            if (samplesLeft < releaseSamples)
                relMult = samplesLeft / releaseSamples;
        }

        const float env = envelopeGain * relMult;

        for (int ch = 0; ch < outChannels; ++ch)
        {
            const int   srcCh = (srcChannels == 1) ? 0 : (ch % srcChannels);
            const float y0    = fileBuffer.getSample(srcCh, pm1);
            const float y1    = fileBuffer.getSample(srcCh, pos0);
            const float y2    = fileBuffer.getSample(srcCh, pp1);
            const float y3    = fileBuffer.getSample(srcCh, pp2);

            // Catmull-Rom coefficients
            const float c1     = 0.5f * (y2 - y0);
            const float c2     = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float c3     = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            const float sample = ((c3 * frac + c2) * frac + c1) * frac + y1;

            const float chGain = (ch == 0) ? gainL : gainR;
            outputBuffer.addSample(ch, i, sample * chGain * env);
        }

        playPosition += (double)pitchRatio;
    }
}
