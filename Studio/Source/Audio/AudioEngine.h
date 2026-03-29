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
// RubberBand is included only in AudioEngine.cpp — not needed in the header
#include "AudioRecorder.h"
#include "SamplePlayer.h"
#include "Sequencer.h"
#include "../SynthEngine.h"
#include "../FXProcessor.h"
#include "../PluginManager.h"
#include "../DynamicEQProcessor.h"
#include "../AutoTuneProcessor.h"

// Snapshot of active-pattern data for lock-free audio thread access.
// Built on the message thread; read-only on the audio thread.
struct PlaybackSnapshot
{
    static constexpr int kCh    = 16;
    static constexpr int kSteps = kMaxPatternSteps;
    static constexpr int kNotes = 256;  // max notes per channel

    int   patternId = -1;
    int   stepCount = 16;
    float swingAmount = 0.0f;  // 0.0 = straight, 0.33 = triplet, 0.5 = dotted
    bool  steps[kCh][kSteps] = {};

    StepParams stepParams[kCh][kSteps] = {};  // per-step expression (Pattern mode)

    struct NoteSlot { NoteEvent notes[kNotes]; int count = 0; };
    NoteSlot noteSlots[kCh];

    ChannelType       channelTypes      [kCh] = {};
    SynthParams       synthParams       [kCh] = {};
    ChannelSourceType channelSourceTypes[kCh] = {};
    SamplerParams     samplerParams     [kCh] = {};
    float             channelVolume     [kCh] = {};
    float             channelPan        [kCh] = {};
    float             channelPitch      [kCh] = {};
    int               channelMixerRouting[kCh]= {};
    bool              pluginSlotEnabled  [kCh]= {};  // per-pattern plugin flag
};

class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    void initialise();
    void shutdown();

    // Playback control
    void play(int patternStartStep = 0, double songStartBar = 0.0);
    void stop();
    void setBPM(double bpm);
    bool isPlaying() const;

    void setPatternStepCount(int stepCount);
    void setSwingAmount(float swing) { sequencer.setSwingAmount(swing); }
    void setPlayMode(PlayMode mode);
    void setProject(Project* projectPtr);

    // Sample management
    void loadSample        (int channelIndex, const juce::File& file);
    void unloadSample      (int channelIndex);   // clear player so channel plays nothing
    void triggerChannel    (int channelIndex, int step, int offsetInBuffer = 0);

    // Sampler Synth — load the oscillator-source buffer for a Sampler channel.
    // Call from the message thread when the user assigns a sample to a Sampler channel.
    void loadSamplerSource (int channelIndex, const juce::File& file);

    // Returns the current sampler source buffer for the given channel (message thread).
    // Used by the Synth Editor to render a waveform preview of the loaded sample.
    std::shared_ptr<const juce::AudioBuffer<float>> getSamplerSourceBuffer(int ch) const;

    // Step pattern
    void setStepPattern(int channelIndex, int stepIndex, bool active);

    double getSampleRate()         const { return sampleRate; }
    long   getSongSamplePosition() const { return songSamplePosition.load(std::memory_order_relaxed); }
    double getSongBeatPosition()   const { return songBeatPosition_.load(std::memory_order_relaxed); }
    double getPatternBeatPos()     const { return patternBeatPos; }   // M3
    double getBPM() const { return bpm.load(std::memory_order_relaxed); }
    // Fired on the message thread when the Song-mode sample cache is ready
    std::function<void()> onSongCacheReady;

    // Seek song playback to an arbitrary bar position
    void seekSongToBar(double bar)
    {
        songBeatPosition_.store(bar * 4.0, std::memory_order_relaxed);
        const double samplesPerBar = (sampleRate * 60.0 / bpm.load(std::memory_order_relaxed)) * 4.0;
        songSamplePosition.store((long)(bar * samplesPerBar), std::memory_order_relaxed);
        resetAudioClipTriggers();
    }

    // --- Audio clip management (Stage 1) -----------------------------------
    // Load a WAV/AIFF file as an audio clip.  Safe to call from message thread.
    // The decoded buffer is shared if the same file is used by multiple clips.
    // clipLengthBars: current clip length in bars (used to cap playback window).
    // Call again with the same clipId to update the window after a resize.
    void loadAudioClip  (int clipId, const juce::File& file, float clipLengthBars = 0.0f);
    void unloadAudioClip(int clipId);
    void unloadAllAudioClips();
    void resetAudioClipTriggers();   // mark all instances inactive — call on stop/seek

    // Rebuild the pitch-shifted buffer for a Stretch/Elastique clip.
    // Call from the message thread after changing mode or pitchSemitone.
    // No-op for Resample mode or pitchSemitone == 0.
    void reprocessAudioClipPitch(int clipId, AudioClipMode mode, float pitchSemitone);

    // Returns the decoded buffer for a given file path (used by waveform preview).
    std::shared_ptr<juce::AudioBuffer<float>> getAudioFileBuffer(const juce::String& path) const;

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

    // Pattern variation (A/B/C/D) — selects which VariationData to read in pattern mode
    void setActiveVariation(int idx);

    // M3 — preview a note from the piano roll keyboard (does not alter channelBasePitch)
    void previewNote(int ch, int midiPitch);

    // Silence all synth voices immediately (called on Stop)
    void allSynthNotesOff();
    void clearTransientPlaybackState();

    // Silence a specific pitch on a specific channel (called when piano roll note is deleted)
    void noteOffChannel(int ch, int midiPitch)
    {
        if (ch >= 0 && ch < 16)
            polySynths[(size_t)ch].noteOff(midiPitch);
    }

    float getChannelBasePitch(int ch) const
    {
        return (ch >= 0 && ch < 16) ? channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) : 0.0f;
    }

    // M13 — trigger a synth note directly (used by piano key preview for melodic synth channels)
    void previewSynthNote(int ch, int midiPitch, const SynthParams& p);

    // Stop editor preview voices immediately on channel ch.
    // Only kills voices tagged as preview — transport playback is unaffected.
    void stopEditorPreview(int ch);

    // True if any preview-tagged voice on ch is still rendering (including release tails).
    bool isEditorPreviewActive(int ch) const;

    // Snapshot — rebuild the PlaybackSnapshot from the active pattern.
    // Call on the message thread whenever pattern data changes.
    void updatePatternSnapshot();

    // Stop the cache loader thread before any structural pattern modification.
    void ensureCacheLoaderStopped()
    {
        if (cacheLoader_ != nullptr && cacheLoader_->isThreadRunning())
        {
            cacheLoader_->signalThreadShouldExit();
            if (!cacheLoader_->waitForThreadToExit(1000))
                DBG("CacheLoader did not stop within timeout");
        }
    }

    // Triggers background rebuilding of the song sample cache.
    // Safe to call while playing; audio will briefly drop out while rebuilding.
    void refreshSongCacheAsync()
    {
        rebuildRuntimeStateFromProject();
        if (cacheLoader_ != nullptr && cacheLoader_->isThreadRunning())
        {
            cacheLoader_->signalThreadShouldExit();
            if (!cacheLoader_->waitForThreadToExit(1000))
            {
                DBG("CacheLoader did not stop within timeout");
                return;
            }
        }
        cacheLoader_ = std::make_unique<CacheLoader>(*this);
        cacheLoader_->startThread();
    }

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
    juce::AudioDeviceManager& getAudioDeviceManager() { return deviceManager; }
    const juce::AudioDeviceManager& getAudioDeviceManager() const { return deviceManager; }

    // MidiInputCallback (called on MIDI thread — posts into collector)
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override;

    // Launchpad — trigger/stop pads; load sample file
    void triggerLaunchpadPad    (int padIdx);
    void stopLaunchpadPad       (int padIdx);
    void stopAllLaunchpadPads   ();
    void loadLaunchpadSample    (int padIdx, const juce::File& file);
    void unloadLaunchpadSample  (int padIdx);

    // M15 — preview a file from the sample browser (dedicated player, no channel affected)
    void previewBrowserFile(const juce::File& f);
    void stopBrowserPreview();   // immediately stops the browser preview player

    // M10 — Offline render to WAV file
    // Temporarily removes the real-time audio callback to avoid thread conflicts.
    // mode: Pattern = loops the sequencer, Song = renders the full timeline.
    // numBars: total bars to write (Pattern mode uses this directly;
    //          Song mode should pass the last clip end bar).
    // Returns true on success.
    bool renderToFile(const juce::File& outputFile, PlayMode mode, int numBars);

    // Stem export — renders each mixer track to a separate WAV file.
    // Returns number of successfully exported stems (0–9: 8 tracks + master).
    int renderStemsToFolder(const juce::File& folder, PlayMode mode, int numBars);

    // --- Recording -----------------------------------------------------------
    void setRecordArmed(bool armed)  { recordArmed_.store(armed, std::memory_order_relaxed); }
    bool isRecordArmed() const       { return recordArmed_.load(std::memory_order_relaxed); }
    bool isRecording()   const       { return recorder_.isRecording(); }

    void setInputMonitoring(bool on) { inputMonitoring_.store(on, std::memory_order_relaxed); }
    bool isInputMonitoring() const   { return inputMonitoring_.load(std::memory_order_relaxed); }

    /** Start recording to file.  Call from message thread (usually in play()). */
    bool startRecording(const juce::File& outputFile);
    /** Stop recording and return the recorded file. */
    juce::File stopRecording();

    /** Get the recording output directory (creates it if needed). */
    juce::File getRecordingDirectory() const;

    /** Total frames written so far (for elapsed-time display). */
    juce::int64 getRecordingSamplesWritten() const { return recorder_.getTotalSamplesWritten(); }

    /** Fired on message thread when recording stops (carries the recorded file). */
    std::function<void(const juce::File& recordedFile, double startBar, double lengthBars)> onRecordingFinished;

    /** Input level (peak) for metering — updated on audio thread, read on message thread. */
    float getInputLevelL() const { return inputLevelL_.load(std::memory_order_relaxed); }
    float getInputLevelR() const { return inputLevelR_.load(std::memory_order_relaxed); }

    // Dynamic EQ accessors (call on message thread; processBlock runs on audio thread)
    DynamicEQProcessor& getTrackDynEQ (int t)  { return trackDynEQs_[(size_t)juce::jlimit(0,7,t)]; }
    DynamicEQProcessor& getMasterDynEQ()        { return masterDynEQ_; }

    // Auto-Tune accessor (for UI pitch metering)
    const AutoTuneProcessor& getAutoTuneProcessor(int t) const
    { return autoTuneProcessors_[(size_t)juce::jlimit(0, 7, t)]; }

    // M8 — VST/AU instrument plugins
    // loadPlugin: creates + prepares the plugin instance (call on message thread).
    // getPlugin:  returns the raw pointer for editor creation (message thread only).
    void loadPlugin  (int ch, const juce::PluginDescription& desc, juce::String& errorMsg);
    void unloadPlugin(int ch);
    bool hasPlugin   (int ch) const;
    juce::AudioPluginInstance* getPlugin(int ch);   // message thread only — do not hold

    // Save plugin state into a MemoryBlock (for project serialisation).
    bool getPluginState(int ch, juce::MemoryBlock& stateOut) const;

    // Save all channel plugin states into PluginSlot array (for pattern switch)
    void savePluginStatesToSlots(std::array<PluginSlot, 16>& slots);
    // Restore plugins from PluginSlot array (unloads mismatched, loads missing)
    void restorePluginsFromSlots(const std::array<PluginSlot, 16>& slots);
    // Ensure plugins needed by song mode clips are loaded (union of all patterns' plugins)
    void ensureSongPluginsLoaded();

    // Snapshot current project state into the runtime double-buffer so the
    // audio thread sees it lock-free.  Light-weight (no sample cache rebuild).
    void rebuildRuntimeStateFromProject();

private:
    SynthParams makeNoteSynthParams(const SynthParams& baseParams,
                                    int midiPitch,
                                    float velocity,
                                    int noteLenSamples) const;

    struct RuntimePlaybackState
    {
        double projectBpm   = 140.0;
        float  masterVolume = 1.0f;
        float  masterPan    = 0.0f;
        std::array<FXParams, 8> fxParams = {};
        std::array<AutoTuneParams, 8> autoTuneParams = {};
        std::array<MixerTrack, 8> mixerTracks = {};
        std::vector<Pattern> patterns;
        std::vector<PlaylistClip> playlistClips;
        std::vector<AutomationLane> automationLanes;

        const Pattern* findPatternById(int patternId) const
        {
            for (const auto& pattern : patterns)
                if (pattern.id == patternId)
                    return &pattern;
            return nullptr;
        }

        bool anyMixerTrackSoloed() const
        {
            for (const auto& track : mixerTracks)
                if (track.soloed)
                    return true;
            return false;
        }
    };

    struct MixRuntimeOverrides
    {
        bool  hasMasterOverride = false;
        float masterVolume = 1.0f;
        float masterPan    = 0.0f;
        // Per-mixer-track volume multiplier (automation: "mixVol0"–"mixVol7").
        // Default 1.0 = no override.  Applied on top of runtime mixer track volume.
        std::array<float, 8> mixerVolMul {{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }};
    };

    struct ScheduledSampleTrigger
    {
        int channel = -1;
        int mixerTrack = 0;
        int offsetInBuffer = 0;
        const juce::AudioBuffer<float>* sourceBuffer = nullptr;
        std::shared_ptr<const juce::AudioBuffer<float>> ownedSourceBuffer;
        float volume = 0.8f;
        float pan = 0.0f;
        float pitchSemitones = 0.0f;
        float attackMs = 10.0f;
        float releaseMs = 0.0f;
        float bpmRatio = 1.0f;
        bool  muted = false;
        double startOffsetSamples = 0.0;  // per-step sample start position (0 = buffer start)
    };

    struct ScheduledSampleTriggerQueue
    {
        static constexpr int kMaxTriggers = 2048;
        std::array<ScheduledSampleTrigger, kMaxTriggers> triggers {};
        int count = 0;

        void clear() { count = 0; }

        void push(const ScheduledSampleTrigger& trigger)
        {
            if (count >= kMaxTriggers)
                return;
            triggers[(size_t)count++] = trigger;
        }
    };

    struct SampleVoicePool
    {
        static constexpr int kVoicesPerChannel = 8;
        std::array<SamplePlayer, kVoicesPerChannel> voices;
        int nextVoice = 0;

        void prepare(double sampleRate, int bufferSize)
        {
            for (auto& voice : voices)
                voice.prepare(sampleRate, bufferSize);
        }

        void reset()
        {
            for (auto& voice : voices)
                voice.reset();
        }

        void setMuted(bool muted)
        {
            for (auto& voice : voices)
                voice.setMuted(muted);
        }

        SamplePlayer& allocateVoice()
        {
            auto& voice = voices[(size_t)nextVoice];
            nextVoice = (nextVoice + 1) % kVoicesPerChannel;
            return voice;
        }

        void renderNextBlock(juce::AudioBuffer<float>& buffer, int numSamples)
        {
            for (auto& voice : voices)
                voice.renderNextBlock(buffer, numSamples);
        }

        void renderNextBlockRouted(std::array<juce::AudioBuffer<float>, 8>& trackBuffers, int numSamples)
        {
            for (auto& voice : voices)
                voice.renderNextBlockRouted(trackBuffers, numSamples);
        }
    };

    // --- Audio clip runtime state -------------------------------------------
    // One instance per audio clip; managed on message thread, read on audio thread.
    //
    // Resample mode (default):
    //   fileReadStart and pitchRatio are set each block from the playhead.
    //   filePos advances by pitchRatio per output sample (speed+pitch change).
    //
    // Stretch / Elastique mode:
    //   pitchedBuffer holds an offline RubberBand-processed copy of the source
    //   at the desired pitch but original tempo.  pitchRatio = 1.0.
    //   pitchedBuffer is (re)computed on the message thread when mode/pitch changes.
    struct AudioClipInstance
    {
        int  clipId              = -1;
        std::shared_ptr<juce::AudioBuffer<float>> buffer;         // original decoded file

        // Stretch/Elastique: offline pitch-processed buffer (same length as source).
        // null = use original buffer (Resample mode or pitch == 0).
        std::shared_ptr<juce::AudioBuffer<float>> pitchedBuffer;

        // Cached mode/pitch so we can detect when reprocessing is needed
        AudioClipMode cachedMode  = AudioClipMode::Resample;
        float         cachedPitch = 0.0f;

        // Set each block by processSongMode; consumed by mixToOutput
        bool   active             = false;
        int    startOffsetInBlock = 0;
        double fileReadStart      = 0.0;
        int    fileTotalSamples   = 0;      // = pitchedBuffer (if set) or buffer size
        double pitchRatio         = 1.0;    // 1.0 for Stretch/Elastique (tempo-preserving)

        // Fade envelope parameters — set each block
        double localBeatAtBlockStart = 0.0; // clip-local beat at startOffsetInBlock
        double beatsPerSample        = 0.0; // = 1.0 / samplesPerBeat
        float  fadeInBeats           = 0.0f;
        float  fadeOutBeats          = 0.0f;
        float  clipLengthBeats       = 0.0f;
    };

    mutable juce::SpinLock                                         audioClipLock_;
    std::vector<AudioClipInstance>                                 audioClipInstances_;
    std::map<juce::String, std::shared_ptr<juce::AudioBuffer<float>>> audioFileBuffers_;

    struct ChannelSourceSnapshot
    {
        std::array<std::shared_ptr<juce::AudioBuffer<float>>, 16> buffers;
    };

    using SongSampleCacheMap = std::map<int, std::array<std::shared_ptr<juce::AudioBuffer<float>>, 16>>;

    juce::AudioDeviceManager deviceManager;
    juce::MixerAudioSource   mixer;

    std::array<SampleVoicePool, 16> sampleVoicePools;
    std::array<PolySynth, 16>   polySynths;       // M13
    std::array<FXChain, 8>      fxChains;         // M14
    std::array<AutoTuneProcessor, 8> autoTuneProcessors_; // Auto-Tune per track
    std::array<SamplePlayer, 64> launchpadPlayers; // Launchpad one-shot players
    SamplePlayer browserPreviewPlayer;              // M15 — dedicated browser preview

    // M12 — MIDI
    juce::MidiMessageCollector  midiCollector;
    std::unique_ptr<juce::MidiInput> midiInput;
    int  midiTargetChannel = 0;   // DAW channel index (0-based)
    Sequencer sequencer;

    Project* project  = nullptr;
    PlayMode playMode = PlayMode::Pattern;

    std::atomic<bool> songPlaying { false };
    std::atomic<long> songSamplePosition { 0 };
    std::atomic<double> songBeatPosition_ { 0.0 };   // beat-accurate song position (BPM-aware)
    std::atomic<double> bpm { 140.0 };

    // M3 — beat tracking for NoteEvent playback
    double patternBeatPos  = 0.0;
    int    activePatternId = 0;
    std::atomic<int> activeVariationIdx_ { 0 };
    std::array<std::atomic<float>, 16> sampleChannelVolume_;
    std::array<std::atomic<float>, 16> sampleChannelPan_;
    std::array<std::atomic<float>, 16> sampleChannelAttackMs_;
    std::array<std::atomic<float>, 16> sampleChannelReleaseMs_;
    std::array<std::atomic<float>, 16> channelBasePitch {};   // stores user pitch-slider values per channel
    std::array<std::atomic<bool>, 16>  channelMuted {};       // user mute state (independent of solo)
    std::array<std::atomic<bool>, 16>  channelSoloed {};      // user solo state

    void applyChannelMuteLogic();      // recompute SamplePlayer mute from muted[]+soloed[]
    const RuntimePlaybackState& getRuntimeState() const;
    void updateRuntimeState(const std::function<void(RuntimePlaybackState&)>& updater);
    const ChannelSourceSnapshot& getChannelSourceSnapshot() const;
    void updateChannelSourceSnapshot(int channelIndex, std::shared_ptr<juce::AudioBuffer<float>> buffer);
    const SongSampleCacheMap& getSongSampleCache() const;
    void resetSongChannelMixState();

    // M5 — mixer intermediate buffers
    juce::AudioBuffer<float> stagingBuf;                         // temp per-channel render
    std::array<juce::AudioBuffer<float>, 8> mixerTrackBufs;     // 8 insert tracks

    void processPatternMode(juce::AudioBuffer<float>& buffer, int numSamples, int numOutputChannels);
    void processSongMode   (juce::AudioBuffer<float>& buffer, int numSamples, int numOutputChannels);
    void mixToOutput       (juce::AudioBuffer<float>& buffer, int numSamples,
                            const MixRuntimeOverrides* overrides = nullptr);  // M5
    void scheduleSampleTrigger(int channelIndex,
                               int offsetInBuffer,
                               int mixerTrack,
                               const juce::AudioBuffer<float>* sourceBuffer,
                               std::shared_ptr<const juce::AudioBuffer<float>> ownedSourceBuffer,
                               float volume,
                               float pan,
                               float pitchSemitones,
                               float bpmRatio,
                               double startOffsetSamples = 0.0);
    void triggerSampleVoiceNow(int channelIndex,
                               int offsetInBuffer,
                               int mixerTrack,
                               const juce::AudioBuffer<float>* sourceBuffer,
                               std::shared_ptr<const juce::AudioBuffer<float>> ownedSourceBuffer,
                               float volume,
                               float pan,
                               float pitchSemitones,
                               float bpmRatio,
                               double startOffsetSamples = 0.0);
    void dispatchScheduledSampleTriggers();
    const juce::AudioBuffer<float>* getChannelSourceBuffer(int channelIndex) const;
    std::shared_ptr<const juce::AudioBuffer<float>> getChannelSourceBufferShared(int channelIndex) const;

    const Pattern* findPatternById(int patternId) const;

    // Song mode: pre-decoded audio cache built in background before playback
    ChannelSourceSnapshot channelSourceSnapshots_[2];
    std::atomic<int> activeChannelSourceSnapshotIdx_{0};
    SongSampleCacheMap songSampleCaches_[2];
    std::atomic<int> activeSongCacheIdx_{0};

    // Sampler Synth — per-channel oscillator-source buffers (pattern mode + fallback)
    mutable juce::SpinLock samplerBufLock_;
    std::array<std::shared_ptr<juce::AudioBuffer<float>>, 16> samplerSourceBuffers_ {};
    // Per-pattern sampler source cache for song mode (same structure as songSampleCaches_)
    SongSampleCacheMap songSamplerCaches_[2];
    std::atomic<int>   activeSongSamplerCacheIdx_ {0};
    int songPlayerClipId[16] = {};
    std::array<float, 16> songChannelVolume_ = {};
    std::array<float, 16> songChannelPan_ = {};
    std::array<int, 16> songChannelMixerRouting_ = {};

    // Double-buffer snapshot: message thread writes to inactive slot,
    // audio thread reads from active slot via atomic index.
    PlaybackSnapshot snapshots_[2];
    std::atomic<int> activeSnapshotIdx_{0};
    RuntimePlaybackState runtimeStates_[2];
    std::atomic<int> activeRuntimeStateIdx_{0};
    ScheduledSampleTriggerQueue scheduledSampleTriggers_;

    // Background cache loader — avoids UI freeze on Play in Song mode
    struct CacheLoader : public juce::Thread
    {
        AudioEngine& engine;
        std::function<void()> onDone;
        CacheLoader(AudioEngine& e) : juce::Thread("SampleCacheLoader"), engine(e) {}
        void run() override
        {
            engine.buildSongSampleCache();
            if (!threadShouldExit())
                juce::MessageManager::callAsync([this] { if (onDone) onDone(); });
        }
    };
    std::unique_ptr<CacheLoader> cacheLoader_;

    void buildSongSampleCache();   // may run on background thread
    void resetMixProcessingState();

    // Sampler Synth helpers
    const SongSampleCacheMap& getSongSamplerCache() const;

    // Unified voice dispatch — routes to noteOnSampler or noteOn based on source type.
    // Sampler channels play regardless of synthParams.enabled; Synth channels require it.
    // noteLenSamples == 0 → use 1/16th note at current BPM.
    // samplerBuf must be non-null when srcType == Sampler; pass nullptr for synth channels.
    void dispatchVoiceNote(int ch, int midiPitch, float velocity, int noteLenSamples,
                           float outputGain, float outputPan, int mixerTrack,
                           const SynthParams& sp, ChannelSourceType srcType,
                           const SamplerParams& samplerParams,
                           std::shared_ptr<const juce::AudioBuffer<float>> samplerBuf);
    
    double sampleRate = 44100.0;
    int    bufferSize = 512;

    // Dynamic EQ — one per mixer track bus + one for master
    std::array<DynamicEQProcessor, 8> trackDynEQs_;
    DynamicEQProcessor                masterDynEQ_;
    std::array<float, 8>             trackInputTrim_ {};
    float                            masterInputTrim_ = 1.0f;
    float                            masterGlueEnvelope_ = 0.0f;
    juce::Random                     stepParamRng_;   // for per-step probability (audio thread)

    // --- Recording ---
    AudioRecorder recorder_;
    std::atomic<bool> recordArmed_     { false };
    std::atomic<bool> inputMonitoring_ { false };
    double recordStartBar_ = 0.0;   // song beat position when recording began
    std::atomic<float> inputLevelL_ { 0.0f };
    std::atomic<float> inputLevelR_ { 0.0f };

    // M8 — VST/AU instrument plugin hosting
    // Lock held by message thread (ScopedLock) on load/unload,
    // and by audio thread (ScopedTryLock) in mixToOutput.
    juce::CriticalSection pluginLock;
    std::array<std::unique_ptr<juce::AudioPluginInstance>, 16> instrumentPlugins;

    // Per-channel MIDI buffers — cleared at audio callback start,
    // populated by NoteEvent / live-MIDI sections, consumed in mixToOutput.
    juce::MidiBuffer instrumentMidiBuffers[16];
    juce::MidiBuffer incomingMidiBuffer_;

    // Pending note-offs for instrument plugins (tracks active VST notes).
    struct ActivePluginNote { double endBeat; int pitch; };
    struct ActivePluginNoteQueue
    {
        static constexpr int kMaxNotes = 512;
        std::array<ActivePluginNote, kMaxNotes> notes {};
        int count = 0;

        void clear() { count = 0; }

        void push(const ActivePluginNote& note)
        {
            if (count >= kMaxNotes)
            {
                notes[kMaxNotes - 1] = note;
                return;
            }
            notes[(size_t)count++] = note;
        }

        void removeAt(int index)
        {
            if (index < 0 || index >= count) return;
            notes[(size_t)index] = notes[(size_t)(count - 1)];
            --count;
        }
    };
    std::array<ActivePluginNoteQueue, 16> activePluginNotes;
    std::array<std::atomic<bool>, 16>    pendingPluginAllNotesOff_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
