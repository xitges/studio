/*
  ==============================================================================

    SamplePlayer.h
    Created: 6 Mar 2026 12:02:03pm
    Author:  홍준영

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class SamplePlayer
{
public:
    SamplePlayer();

    void loadFile(const juce::File& file);
    void trigger();
    void prepare(double sampleRate, int bufferSize);
    void reset();
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int numSamples);

    bool isLoaded() const { return fileBuffer.getNumSamples() > 0; }

    // M1.1 — Volume & Pan
    void  setVolume(float v)  { volume = juce::jlimit(0.0f, 1.0f, v); }
    void  setPan(float p)     { pan    = juce::jlimit(-1.0f, 1.0f, p); }
    float getVolume() const   { return volume; }
    float getPan()    const   { return pan; }

    // M1.2 — Pitch (semitones, -24..+24)
    void setPitch(float semitones);

    // M1.3 — Envelope (Attack / Release in milliseconds)
    void setAttack (float ms);
    void setRelease(float ms);

    // Mute
    void setMuted(bool m) { muted = m; }
    bool isMuted()  const { return muted; }

private:
    juce::AudioBuffer<float>                 fileBuffer;
    juce::AudioFormatManager                 formatManager;
    std::unique_ptr<juce::AudioFormatReader> reader;

    double playerSampleRate = 44100.0;

    // M1.2: fractional position enables pitch-shifted playback
    double playPosition = -1.0;

    // M1.1
    float volume = 0.8f;
    float pan    = 0.0f;    // -1=L, 0=C, +1=R

    // M1.2
    float pitchRatio = 1.0f;  // 2^(semitones/12)

    // Mute
    bool muted = false;

    std::atomic<bool> triggered { false };

    // M1.3 envelope state
    float attackSamples  = 441.0f;   // default ~10 ms anti-pop
    float releaseSamples = 0.0f;
    float envelopeGain   = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplePlayer)
};
