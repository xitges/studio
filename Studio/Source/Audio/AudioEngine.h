/*
  ==============================================================================

    AudioEngine.h
    Created: 6 Mar 2026 12:25:31pm
    Author:  홍준영

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "../ProjectModel.h"
#include "SamplePlayer.h"
#include "Sequencer.h"

class AudioEngine : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    void initialise();
    void shutdown();

    // Playback control
    void play();
    void stop();
    void setBPM(double bpm);
    bool isPlaying() const;

    void setPatternStepCount(int stepCount);
    void setPlayMode(PlayMode mode);
    void setProject(Project* projectPtr);

    // Sample management
    void loadSample(int channelIndex, const juce::File& file);
    void triggerChannel(int channelIndex);

    // Step pattern
    void setStepPattern(int channelIndex, int stepIndex, bool active);

    double getSampleRate()         const { return sampleRate; }
    long   getSongSamplePosition() const { return songSamplePosition; }

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    Sequencer& getSequencer() { return sequencer; }

    // M1.1 — Mute / Solo / Volume / Pan
    void setChannelMuted  (int ch, bool muted);
    void setChannelSolo   (int ch, bool soloed);
    void setChannelVolume (int ch, float volume);
    void setChannelPan    (int ch, float pan);

    // M1.2 — Pitch
    void setChannelPitch  (int ch, float semitones);

    // M1.3 — Envelope
    void setChannelAttack (int ch, float ms);
    void setChannelRelease(int ch, float ms);

    // M10 — Offline render to WAV file
    // Temporarily removes the real-time audio callback to avoid thread conflicts.
    // mode: Pattern = loops the sequencer, Song = renders the full timeline.
    // numBars: total bars to write (Pattern mode uses this directly;
    //          Song mode should pass the last clip end bar).
    // Returns true on success.
    bool renderToFile(const juce::File& outputFile, PlayMode mode, int numBars);

private:
    juce::AudioDeviceManager deviceManager;
    juce::MixerAudioSource   mixer;

    std::array<SamplePlayer, 16> players;
    Sequencer sequencer;

    Project* project  = nullptr;
    PlayMode playMode = PlayMode::Pattern;

    bool   songPlaying        = false;
    long   songSamplePosition = 0;
    double bpm                = 140.0;

    void processPatternMode(juce::AudioBuffer<float>& buffer, int numSamples, int numOutputChannels);
    void processSongMode   (juce::AudioBuffer<float>& buffer, int numSamples, int numOutputChannels);

    const Pattern* findPatternById(int patternId) const;

    double sampleRate = 44100.0;
    int    bufferSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
