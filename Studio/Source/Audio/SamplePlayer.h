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
    void triggerAt(int offsetInBuffer);   // sample-accurate trigger (audio thread only)
    void prepare(double sampleRate, int bufferSize);
    void reset();
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int numSamples);
    void renderNextBlockRouted(std::array<juce::AudioBuffer<float>, 8>& trackBuffers, int numSamples)
    {
        renderNextBlock(trackBuffers[(size_t) juce::jlimit(0, 7, mixerTrack_)], numSamples);
    }

    bool isLoaded() const
    {
        const auto* src = externalBuffer != nullptr ? externalBuffer : &fileBuffer;
        return src->getNumSamples() > 0;
    }
    void clear()
    {
        externalSharedBuffer.reset();
        externalBuffer = nullptr;
        fileBuffer.setSize(0, 0);
        playPosition = -1.0;
    }

    // Reuse an already-decoded buffer without copying. Audio thread only.
    void setExternalBuffer(const juce::AudioBuffer<float>* src)
    {
        externalBuffer = src;
        playPosition = -1.0;
    }
    void setSharedExternalBuffer(const std::shared_ptr<const juce::AudioBuffer<float>>& src)
    {
        externalSharedBuffer = src;
        externalBuffer = externalSharedBuffer.get();
        playPosition = -1.0;
    }
    std::shared_ptr<juce::AudioBuffer<float>> takeLoadedBuffer()
    {
        juce::SpinLock::ScopedLockType lock(bufferLock);
        auto result = std::make_shared<juce::AudioBuffer<float>>();
        std::swap(*result, fileBuffer);
        externalSharedBuffer.reset();
        externalBuffer = nullptr;
        playPosition = -1.0;
        return result;
    }
    const juce::AudioBuffer<float>& getBuffer() const    { return fileBuffer; }

    // M1.1 — Volume & Pan
    void  setVolume(float v)  { volume = juce::jlimit(0.0f, 1.0f, v); smoothVolume_.setTargetValue(volume); }
    void  setPan(float p)     { pan    = juce::jlimit(-1.0f, 1.0f, p); smoothPan_.setTargetValue(pan); }
    float getVolume() const   { return volume; }
    float getPan()    const   { return pan; }

    // M1.2 — Pitch (semitones, -24..+24)
    void setPitch(float semitones);

    // M1.3 — Envelope (Attack / Release in milliseconds)
    void setAttack (float ms);
    void setRelease(float ms);
    
    void setBpmRatio(double ratio){
        bpmRatio = (float) ratio;
        updateFinalRatio();
    }

    // Mute
    void setMuted(bool m) { muted = m; }
    bool isMuted()  const { return muted; }
    void setMixerTrack(int track) { mixerTrack_ = juce::jlimit(0, 7, track); }
    int getMixerTrack() const { return mixerTrack_; }

private:
    void updateFinalRatio(){
        finalPitchRatio = basePitchRatio * bpmRatio;
    }
    
    juce::AudioBuffer<float>                 fileBuffer;
    std::shared_ptr<const juce::AudioBuffer<float>> externalSharedBuffer;
    const juce::AudioBuffer<float>*          externalBuffer = nullptr;
    juce::AudioFormatManager                 formatManager;
    std::unique_ptr<juce::AudioFormatReader> reader;

    double playerSampleRate = 44100.0;

    // M1.2: fractional position enables pitch-shifted playback
    double playPosition = -1.0;

    float basePitchRatio = 1.0f;
    float bpmRatio = 1.0f;
    float finalPitchRatio = 1.0f;
    
    // M1.1
    float volume = 0.8f;
    float pan    = 0.0f;    // -1=L, 0=C, +1=R

    // Smoothed volume/pan to avoid zipper noise on parameter changes
    juce::LinearSmoothedValue<float> smoothVolume_;
    juce::LinearSmoothedValue<float> smoothPan_;

    // Mute
    bool muted = false;
    int  mixerTrack_ = 0;

    std::atomic<bool> triggered { false };
    int triggerOffset_ = 0;   // set by triggerAt(), consumed in renderNextBlock (audio thread only)
    int startOffset_   = 0;   // active start-offset for the current render block

    // Thread safety: protects fileBuffer between main-thread loadFile
    // and audio-thread renderNextBlock.  The audio thread uses a non-blocking
    // try-lock so it never spins or blocks the real-time callback.
    juce::SpinLock bufferLock;

    // M1.3 envelope state
    float attackSamples  = 441.0f;   // default ~10 ms anti-pop
    float releaseSamples = 0.0f;
    float envelopeGain   = 1.0f;
    float lastOutputL    = 0.0f;
    float lastOutputR    = 0.0f;
    float residualTailL  = 0.0f;
    float residualTailR  = 0.0f;
    int   residualFadeRemaining = 0;
    static constexpr int kResidualFadeSamples = 64;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplePlayer)
};
