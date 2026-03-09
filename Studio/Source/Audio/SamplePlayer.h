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
    void clear()          { fileBuffer.setSize(0, 0); playPosition = -1.0; }

    // Copy pre-decoded audio data in (used by song-mode sample cache — no disk I/O)
    void loadBuffer(const juce::AudioBuffer<float>& src)
    {
        // Called from the audio thread in song mode — bufferLock is already held
        // by the audio thread itself (via renderNextBlock's try-lock), so we must
        // NOT try to lock here. Song-mode cache swaps happen between render blocks
        // (called from processSongMode before renderNextBlock), so no concurrent
        // renderNextBlock is running at this point. Direct assignment is safe.
        fileBuffer.makeCopyOf(src);
        playPosition = -1.0;
    }
    const juce::AudioBuffer<float>& getBuffer() const    { return fileBuffer; }

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

    // Thread safety: protects fileBuffer between main-thread loadFile
    // and audio-thread renderNextBlock.  The audio thread uses a non-blocking
    // try-lock so it never spins or blocks the real-time callback.
    juce::SpinLock bufferLock;

    // M1.3 envelope state
    float attackSamples  = 441.0f;   // default ~10 ms anti-pop
    float releaseSamples = 0.0f;
    float envelopeGain   = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplePlayer)
};
