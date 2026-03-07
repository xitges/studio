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
    reader.reset(formatManager.createReaderFor(file));

    if (reader != nullptr)
    {
        fileBuffer.setSize((int)reader->numChannels,
                           (int)reader->lengthInSamples);
        reader->read(&fileBuffer, 0,
                     (int)reader->lengthInSamples, 0, true, true);
        DBG("Loaded: " << file.getFileName());
    }
}

void SamplePlayer::trigger()
{
    triggered = true;
}

void SamplePlayer::prepare(double sr, int)
{
    playerSampleRate = sr;
    playPosition     = -1.0;
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
    if (!isLoaded()) return;

    // Trigger detection
    if (triggered.exchange(false))
    {
        playPosition  = 0.0;
        envelopeGain  = (attackSamples > 0.0f) ? 0.0f : 1.0f;
    }

    if (playPosition < 0.0 || muted) return;

    const int srcSamples  = fileBuffer.getNumSamples();
    const int outChannels = outputBuffer.getNumChannels();
    const int srcChannels = fileBuffer.getNumChannels();

    // Constant-power pan law
    const float angle     = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float leftGain  = volume * std::cos(angle);
    const float rightGain = volume * std::sin(angle);

    for (int i = 0; i < numSamples; ++i)
    {
        const int pos0 = (int)playPosition;
        if (pos0 >= srcSamples)
        {
            playPosition = -1.0;
            break;
        }

        // Linear interpolation between adjacent samples (enables pitch shift)
        const int   pos1 = juce::jmin(pos0 + 1, srcSamples - 1);
        const float frac = (float)(playPosition - (double)pos0);

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
            const int   srcCh  = (srcChannels == 1) ? 0 : (ch % srcChannels);
            const float s0     = fileBuffer.getSample(srcCh, pos0);
            const float s1     = fileBuffer.getSample(srcCh, pos1);
            const float sample = s0 + frac * (s1 - s0);

            const float chGain = (ch == 0) ? leftGain : rightGain;
            outputBuffer.addSample(ch, i, sample * chGain * env);
        }

        playPosition += (double)pitchRatio;
    }
}
