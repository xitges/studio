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
#include "../SynthEngine.h"
#include "../FXProcessor.h"
#include "../PluginManager.h"
#include "../DynamicEQProcessor.h"

class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
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
    void loadSample   (int channelIndex, const juce::File& file);
    void unloadSample (int channelIndex);   // clear player so channel plays nothing
    void triggerChannel(int channelIndex);

    // Step pattern
    void setStepPattern(int channelIndex, int stepIndex, bool active);

    double getSampleRate()         const { return sampleRate; }
    long   getSongSamplePosition() const { return songSamplePosition; }
    double getPatternBeatPos()     const { return patternBeatPos; }   // M3

    // Seek song playback to an arbitrary bar position
    void seekSongToBar(double bar)
    {
        const double samplesPerBar = (sampleRate * 60.0 / bpm) * 4.0;
        songSamplePosition = (long)(bar * samplesPerBar);
    }

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

    // M3 — set active pattern for NoteEvent playback in pattern mode
    void setActivePattern(int patternId);

    // M3 — preview a note from the piano roll keyboard (does not alter channelBasePitch)
    void previewNote(int ch, int midiPitch);

    // Silence all synth voices immediately (called on Stop)
    void allSynthNotesOff();

    // Silence a specific pitch on a specific channel (called when piano roll note is deleted)
    void noteOffChannel(int ch, int midiPitch)
    {
        if (ch >= 0 && ch < 16)
            polySynths[(size_t)ch].noteOff(midiPitch);
    }

    float getChannelBasePitch(int ch) const
    {
        return (ch >= 0 && ch < 16) ? channelBasePitch[ch] : 0.0f;
    }

    // M13 — trigger a synth note directly (used by piano key preview for melodic synth channels)
    void previewSynthNote(int ch, int midiPitch, const SynthParams& p);

    // M5 — mixer track controls
    void setMixerTrackVolume(int track, float vol);
    void setMixerTrackPan   (int track, float pan);
    void setMixerTrackMuted (int track, bool muted);
    void setMixerTrackSoloed(int track, bool soloed);
    void setMasterVolume    (float vol);
    void setMasterPan       (float pan);

    // M12 — MIDI input
    juce::Array<juce::MidiDeviceInfo> getMidiInputDevices() const;
    void openMidiDevice (const juce::String& deviceId);
    void closeMidiDevice();
    void setMidiTargetChannel(int ch);   // which DAW channel receives live MIDI (0-based)
    juce::String getOpenMidiDeviceId() const;

    // MidiInputCallback (called on MIDI thread — posts into collector)
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override;

    // Launchpad — trigger one-shot sample on a pad; load sample file
    void triggerLaunchpadPad  (int padIdx);
    void loadLaunchpadSample  (int padIdx, const juce::File& file);

    // M15 — preview a file from the sample browser (dedicated player, no channel affected)
    void previewBrowserFile(const juce::File& f);

    // M10 — Offline render to WAV file
    // Temporarily removes the real-time audio callback to avoid thread conflicts.
    // mode: Pattern = loops the sequencer, Song = renders the full timeline.
    // numBars: total bars to write (Pattern mode uses this directly;
    //          Song mode should pass the last clip end bar).
    // Returns true on success.
    bool renderToFile(const juce::File& outputFile, PlayMode mode, int numBars);

    // Dynamic EQ accessors (call on message thread; processBlock runs on audio thread)
    DynamicEQProcessor& getTrackDynEQ (int t)  { return trackDynEQs_[(size_t)juce::jlimit(0,7,t)]; }
    DynamicEQProcessor& getMasterDynEQ()        { return masterDynEQ_; }

    // M8 — VST/AU instrument plugins
    // loadPlugin: creates + prepares the plugin instance (call on message thread).
    // getPlugin:  returns the raw pointer for editor creation (message thread only).
    void loadPlugin  (int ch, const juce::PluginDescription& desc, juce::String& errorMsg);
    void unloadPlugin(int ch);
    bool hasPlugin   (int ch) const;
    juce::AudioPluginInstance* getPlugin(int ch);   // message thread only — do not hold

    // Save plugin state into a MemoryBlock (for project serialisation).
    bool getPluginState(int ch, juce::MemoryBlock& stateOut) const;

private:
    juce::AudioDeviceManager deviceManager;
    juce::MixerAudioSource   mixer;

    std::array<SamplePlayer, 16> players;
    std::array<PolySynth, 16>   polySynths;       // M13
    std::array<FXChain, 8>      fxChains;         // M14
    std::array<SamplePlayer, 64> launchpadPlayers; // Launchpad one-shot players
    SamplePlayer browserPreviewPlayer;              // M15 — dedicated browser preview

    // M12 — MIDI
    juce::MidiMessageCollector  midiCollector;
    std::unique_ptr<juce::MidiInput> midiInput;
    int  midiTargetChannel = 0;   // DAW channel index (0-based)
    Sequencer sequencer;

    Project* project  = nullptr;
    PlayMode playMode = PlayMode::Pattern;

    bool   songPlaying        = false;
    long   songSamplePosition = 0;
    double bpm                = 140.0;

    // M3 — beat tracking for NoteEvent playback
    double patternBeatPos  = 0.0;
    int    activePatternId = 0;
    float  channelBasePitch[16] = {};   // stores user pitch-slider values per channel
    bool   channelMuted [16]   = {};   // user mute state (independent of solo)
    bool   channelSoloed[16]   = {};   // user solo state

    void applyChannelMuteLogic();      // recompute SamplePlayer mute from muted[]+soloed[]

    // M5 — mixer intermediate buffers
    juce::AudioBuffer<float> stagingBuf;                         // temp per-channel render
    std::array<juce::AudioBuffer<float>, 8> mixerTrackBufs;     // 8 insert tracks

    void processPatternMode(juce::AudioBuffer<float>& buffer, int numSamples, int numOutputChannels);
    void processSongMode   (juce::AudioBuffer<float>& buffer, int numSamples, int numOutputChannels);
    void mixToOutput       (juce::AudioBuffer<float>& buffer, int numSamples);  // M5

    const Pattern* findPatternById(int patternId) const;

    // Song mode: pre-decoded audio cache built before playback starts
    // Key = patternId, Value = one AudioBuffer per channel (empty if no sample assigned)
    std::map<int, std::array<juce::AudioBuffer<float>, 16>> songSampleCache;
    int songPlayerPatternId[16] = {};   // which patternId is currently loaded in players[ch]
    void buildSongSampleCache();        // called on message thread before song play/render

    double sampleRate = 44100.0;
    int    bufferSize = 512;

    // Dynamic EQ — one per mixer track bus + one for master
    std::array<DynamicEQProcessor, 8> trackDynEQs_;
    DynamicEQProcessor                masterDynEQ_;

    // M8 — VST/AU instrument plugin hosting
    // Lock held by message thread (ScopedLock) on load/unload,
    // and by audio thread (ScopedTryLock) in mixToOutput.
    juce::CriticalSection pluginLock;
    std::array<std::unique_ptr<juce::AudioPluginInstance>, 16> instrumentPlugins;

    // Per-channel MIDI buffers — cleared at audio callback start,
    // populated by NoteEvent / live-MIDI sections, consumed in mixToOutput.
    juce::MidiBuffer instrumentMidiBuffers[16];

    // Pending note-offs for instrument plugins (tracks active VST notes).
    struct ActivePluginNote { double endBeat; int pitch; };
    std::vector<ActivePluginNote> activePluginNotes[16];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
