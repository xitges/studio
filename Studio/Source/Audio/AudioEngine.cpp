/*
  ==============================================================================

    AudioEngine.cpp
    Created: 6 Mar 2026 12:25:31pm
    Author:  홍준영

  ==============================================================================
*/

#include "AudioEngine.h"

AudioEngine::AudioEngine()
    : sequencer([this](int ch, int offset) { triggerChannel(ch, offset); })
{
    for (int i = 0; i < 16; ++i)
    {
        sampleChannelVolume_[(size_t)i].store(0.8f, std::memory_order_relaxed);
        sampleChannelPan_[(size_t)i].store(0.0f, std::memory_order_relaxed);
        sampleChannelAttackMs_[(size_t)i].store(10.0f, std::memory_order_relaxed);
        sampleChannelReleaseMs_[(size_t)i].store(4.0f, std::memory_order_relaxed);
        channelBasePitch[(size_t)i].store(0.0f, std::memory_order_relaxed);
        channelMuted[(size_t)i].store(false, std::memory_order_relaxed);
        channelSoloed[(size_t)i].store(false, std::memory_order_relaxed);
    }

    resetSongChannelMixState();
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

const AudioEngine::RuntimePlaybackState& AudioEngine::getRuntimeState() const
{
    return runtimeStates_[activeRuntimeStateIdx_.load(std::memory_order_acquire)];
}

const AudioEngine::ChannelSourceSnapshot& AudioEngine::getChannelSourceSnapshot() const
{
    return channelSourceSnapshots_[activeChannelSourceSnapshotIdx_.load(std::memory_order_acquire)];
}

const AudioEngine::SongSampleCacheMap& AudioEngine::getSongSampleCache() const
{
    return songSampleCaches_[activeSongCacheIdx_.load(std::memory_order_acquire)];
}

void AudioEngine::resetSongChannelMixState()
{
    for (int ch = 0; ch < 16; ++ch)
    {
        songChannelVolume_[(size_t)ch] = 0.8f;
        songChannelPan_[(size_t)ch] = 0.0f;
        songChannelMixerRouting_[(size_t)ch] = ch % 8;
    }
}

void AudioEngine::updateRuntimeState(const std::function<void(RuntimePlaybackState&)>& updater)
{
    const int currentIdx = activeRuntimeStateIdx_.load(std::memory_order_relaxed);
    const int nextIdx = 1 - currentIdx;
    runtimeStates_[nextIdx] = runtimeStates_[currentIdx];
    updater(runtimeStates_[nextIdx]);
    activeRuntimeStateIdx_.store(nextIdx, std::memory_order_release);
}

void AudioEngine::updateChannelSourceSnapshot(int channelIndex, std::shared_ptr<juce::AudioBuffer<float>> buffer)
{
    if (channelIndex < 0 || channelIndex >= 16)
        return;

    const int currentIdx = activeChannelSourceSnapshotIdx_.load(std::memory_order_relaxed);
    const int nextIdx = 1 - currentIdx;
    channelSourceSnapshots_[nextIdx] = channelSourceSnapshots_[currentIdx];
    channelSourceSnapshots_[nextIdx].buffers[(size_t)channelIndex] = std::move(buffer);
    activeChannelSourceSnapshotIdx_.store(nextIdx, std::memory_order_release);
}

void AudioEngine::rebuildRuntimeStateFromProject()
{
    const int nextIdx = 1 - activeRuntimeStateIdx_.load(std::memory_order_relaxed);
    auto& state = runtimeStates_[nextIdx];
    state = RuntimePlaybackState{};

    if (project != nullptr)
    {
        state.projectBpm   = project->bpm;
        state.masterVolume = project->masterVolume;
        state.masterPan    = project->masterPan;
        state.fxParams     = project->fxParams;
        state.patterns     = project->patterns;
        state.playlistClips = project->playlistClips;
        state.automationLanes = project->automationLanes;

        for (int i = 0; i < 8; ++i)
        {
            MixerTrack track;
            track.name = "Track " + juce::String(i + 1);
            if (i < (int)project->mixerTracks.size())
                track = project->mixerTracks[(size_t)i];
            state.mixerTracks[(size_t)i] = track;
        }
    }

    activeRuntimeStateIdx_.store(nextIdx, std::memory_order_release);
}

void AudioEngine::initialise()
{
    auto err = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (err.isNotEmpty())
        DBG("AudioDeviceManager error: " << err);

    deviceManager.addAudioCallback(this);
}

void AudioEngine::shutdown()
{
    closeMidiDevice();
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::play()
{
    if (playMode == PlayMode::Pattern)
    {
        patternBeatPos = 0.0;   // M3
        sequencer.start();
    }
    else
    {
        rebuildRuntimeStateFromProject();
        resetSongChannelMixState();

        // Stop any previous loader
        if (cacheLoader_ != nullptr)
        {
            cacheLoader_->signalThreadShouldExit();
            if (!cacheLoader_->waitForThreadToExit(2000))
            {
                DBG("CacheLoader did not stop within timeout");
                return;
            }
        }

        songSamplePosition.store(0, std::memory_order_relaxed);
        std::fill_n(songPlayerClipId, 16, -1);

        cacheLoader_ = std::make_unique<CacheLoader>(*this);
        cacheLoader_->onDone = [this]
        {
            songPlaying.store(true, std::memory_order_release);
            if (onSongCacheReady) onSongCacheReady();
        };
        cacheLoader_->startThread();
    }
}

void AudioEngine::stop()
{
    if (playMode == PlayMode::Pattern)
        sequencer.stop();
    else
    {
        songPlaying.store(false, std::memory_order_release);
        resetSongChannelMixState();
        // Abort any in-progress background cache build
        if (cacheLoader_ != nullptr && cacheLoader_->isThreadRunning())
            cacheLoader_->signalThreadShouldExit();
    }
}

// ---- Mute / Solo -------------------------------------------------------

void AudioEngine::applyChannelMuteLogic()
{
    // Check if any channel is soloed
    bool anySoloed = false;
    for (int i = 0; i < 16; ++i)
        if (channelSoloed[(size_t)i].load(std::memory_order_relaxed)) { anySoloed = true; break; }

    for (int i = 0; i < 16; ++i)
    {
        const bool muted = channelMuted[(size_t)i].load(std::memory_order_relaxed);
        const bool soloed = channelSoloed[(size_t)i].load(std::memory_order_relaxed);
        const bool silenced = muted || (anySoloed && !soloed);
        sampleVoicePools[(size_t)i].setMuted(silenced);
    }
}

void AudioEngine::setChannelMuted(int ch, bool muted)
{
    if (ch < 0 || ch >= 16) return;
    channelMuted[(size_t)ch].store(muted, std::memory_order_relaxed);
    applyChannelMuteLogic();
}

void AudioEngine::setChannelSolo(int ch, bool soloed)
{
    if (ch < 0 || ch >= 16) return;
    channelSoloed[(size_t)ch].store(soloed, std::memory_order_relaxed);
    applyChannelMuteLogic();
}

// ---- M1.1 Volume / Pan -------------------------------------------------

void AudioEngine::setChannelVolume(int ch, float volume)
{
    if (ch >= 0 && ch < 16)
        sampleChannelVolume_[(size_t)ch].store(juce::jlimit(0.0f, 1.0f, volume), std::memory_order_relaxed);
}

void AudioEngine::setChannelPan(int ch, float pan)
{
    if (ch >= 0 && ch < 16)
        sampleChannelPan_[(size_t)ch].store(juce::jlimit(-1.0f, 1.0f, pan), std::memory_order_relaxed);
}

// ---- M1.2 Pitch --------------------------------------------------------

void AudioEngine::setChannelPitch(int ch, float semitones)
{
    if (ch >= 0 && ch < 16)
        channelBasePitch[(size_t)ch].store(semitones, std::memory_order_relaxed);
}

// ---- M1.3 Envelope -----------------------------------------------------

void AudioEngine::setChannelAttack(int ch, float ms)
{
    if (ch >= 0 && ch < 16)
        sampleChannelAttackMs_[(size_t)ch].store(juce::jmax(0.5f, ms), std::memory_order_relaxed);
}

void AudioEngine::setChannelRelease(int ch, float ms)
{
    if (ch >= 0 && ch < 16)
        sampleChannelReleaseMs_[(size_t)ch].store(juce::jmax(2.0f, ms), std::memory_order_relaxed);
}

// ---- M3 — Active pattern / pitch ----------------------------------------

void AudioEngine::setActivePattern(int patternId)
{
    activePatternId = patternId;
    patternBeatPos  = 0.0;
}

void AudioEngine::updatePatternSnapshot()
{
    const int nextIdx = 1 - activeSnapshotIdx_.load(std::memory_order_relaxed);
    PlaybackSnapshot& snap = snapshots_[nextIdx];

    const Pattern* pat = (project != nullptr)
                         ? findPatternById(activePatternId) : nullptr;
                         
    if (pat == nullptr) 
    {
        snap.patternId = -1;
        activeSnapshotIdx_.store(nextIdx, std::memory_order_release);
        return;
    }

    snap.patternId = pat->id;
    snap.stepCount = juce::jlimit(1, PlaybackSnapshot::kSteps, pat->stepCount);

    for (int ch = 0; ch < PlaybackSnapshot::kCh; ++ch)
    {
        for (int s = 0; s < PlaybackSnapshot::kSteps; ++s)
            snap.steps[ch][s] = pat->steps[ch][s];

        snap.channelTypes[ch]        = pat->channelTypes[ch];
        snap.synthParams[ch]         = pat->synthParams[ch];
        snap.channelVolume[ch]       = pat->channelVolume[ch];
        snap.channelPan[ch]          = pat->channelPan[ch];
        snap.channelPitch[ch]        = pat->channelPitch[ch];
        snap.channelMixerRouting[ch] = pat->channelMixerRouting[ch];

        auto& slot = snap.noteSlots[ch];
        const auto& src = pat->notes[ch];
        slot.count = (int)juce::jmin((int)src.size(), PlaybackSnapshot::kNotes);
        for (int n = 0; n < slot.count; ++n)
            slot.notes[n] = src[(size_t)n];
    }

    activeSnapshotIdx_.store(nextIdx, std::memory_order_release);
}

void AudioEngine::triggerLaunchpadPad(int padIdx)
{
    if (padIdx < 0 || padIdx >= 64) return;
    if (project != nullptr)
    {
        launchpadPlayers[(size_t)padIdx].setVolume(project->launchpadPads[(size_t)padIdx].volume);
        launchpadPlayers[(size_t)padIdx].setPitch (project->launchpadPads[(size_t)padIdx].pitch);
    }
    launchpadPlayers[(size_t)padIdx].trigger();
}

void AudioEngine::loadLaunchpadSample(int padIdx, const juce::File& file)
{
    if (padIdx < 0 || padIdx >= 64) return;
    launchpadPlayers[(size_t)padIdx].loadFile(file);
}

void AudioEngine::unloadLaunchpadSample(int padIdx)
{
    if (padIdx < 0 || padIdx >= 64) return;
    launchpadPlayers[(size_t)padIdx].clear();
}

void AudioEngine::allSynthNotesOff()
{
    for (auto& ps : polySynths)
        ps.allNotesOff();
    for (auto& fx : fxChains)
        fx.reset();
}

SynthParams AudioEngine::makeNoteSynthParams(const SynthParams& baseParams,
                                             int midiPitch,
                                             float velocity,
                                             int noteLenSamples) const
{
    const double noteLengthSeconds = (sampleRate > 0.0 && noteLenSamples > 0)
        ? (double)noteLenSamples / sampleRate
        : 0.35;
    return DDSPAutoPatch::generate(baseParams, midiPitch, velocity, noteLengthSeconds);
}

void AudioEngine::previewNote(int ch, int midiPitch)
{
    if (ch < 0 || ch >= 16) return;

    // If snapshot is stale (patternId == -1), refresh it now.
    // This happens when previewNote is called before the first Play (e.g. synth just enabled).
    {
        const int idx = activeSnapshotIdx_.load(std::memory_order_acquire);
        if (snapshots_[idx].patternId < 0)
            updatePatternSnapshot();
    }

    const PlaybackSnapshot& prevSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];
    if (prevSnap.patternId >= 0 && prevSnap.synthParams[(size_t)ch].enabled)
    {
        const double samplesPerBeat = sampleRate * 60.0 / bpm.load(std::memory_order_relaxed);
        const int noteLenSamples = (int)(0.25 * samplesPerBeat);  // 16th note preview
        const auto noteParams = makeNoteSynthParams(prevSnap.synthParams[(size_t)ch],
                                                    midiPitch, 0.8f, noteLenSamples);
        polySynths[(size_t)ch].noteOn(midiPitch, 0.8f, sampleRate, noteParams, noteLenSamples);
    }
    else
    {
        const float volume = (prevSnap.patternId >= 0) ? prevSnap.channelVolume[(size_t)ch]
                                                       : sampleChannelVolume_[(size_t)ch].load(std::memory_order_relaxed);
        const float pan = (prevSnap.patternId >= 0) ? prevSnap.channelPan[(size_t)ch]
                                                    : sampleChannelPan_[(size_t)ch].load(std::memory_order_relaxed);
        auto sourceBuffer = getChannelSourceBufferShared(ch);
        triggerSampleVoiceNow(ch, 0, sourceBuffer.get(), sourceBuffer, volume, pan,
                              channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) + (float)(midiPitch - 60), 1.0f);
    }
}

void AudioEngine::previewBrowserFile(const juce::File& f)
{
    browserPreviewPlayer.loadFile(f);
    browserPreviewPlayer.setVolume(0.8f);
    browserPreviewPlayer.setPitch(0.0f);
    browserPreviewPlayer.setAttack(2.0f);
    browserPreviewPlayer.setRelease(6.0f);
    browserPreviewPlayer.trigger();
}

void AudioEngine::previewSynthNote(int ch, int midiPitch, const SynthParams& p)
{
    if (ch < 0 || ch >= 16) return;
    const double samplesPerBeat = sampleRate * 60.0 / bpm.load(std::memory_order_relaxed);
    const int noteLenSamples = (int)(0.5 * samplesPerBeat);  // 8th note preview
    const auto noteParams = makeNoteSynthParams(p, midiPitch, 0.8f, noteLenSamples);
    polySynths[(size_t)ch].noteOn(midiPitch, 0.8f, sampleRate, noteParams, noteLenSamples);
}

// ---- M5 — Mixer track controls ------------------------------------------

void AudioEngine::setMixerTrackVolume(int track, float vol)
{
    if (project && track >= 0 && track < (int)project->mixerTracks.size())
        project->mixerTracks[(size_t)track].volume = vol;

    updateRuntimeState([track, vol](RuntimePlaybackState& state)
    {
        if (track >= 0 && track < (int)state.mixerTracks.size())
            state.mixerTracks[(size_t)track].volume = vol;
    });
}
void AudioEngine::setMixerTrackPan(int track, float pan)
{
    if (project && track >= 0 && track < (int)project->mixerTracks.size())
        project->mixerTracks[(size_t)track].pan = pan;

    updateRuntimeState([track, pan](RuntimePlaybackState& state)
    {
        if (track >= 0 && track < (int)state.mixerTracks.size())
            state.mixerTracks[(size_t)track].pan = pan;
    });
}
void AudioEngine::setMixerTrackMuted(int track, bool muted)
{
    if (project && track >= 0 && track < (int)project->mixerTracks.size())
        project->mixerTracks[(size_t)track].muted = muted;

    updateRuntimeState([track, muted](RuntimePlaybackState& state)
    {
        if (track >= 0 && track < (int)state.mixerTracks.size())
            state.mixerTracks[(size_t)track].muted = muted;
    });
}
void AudioEngine::setMixerTrackSoloed(int track, bool soloed)
{
    if (project && track >= 0 && track < (int)project->mixerTracks.size())
        project->mixerTracks[(size_t)track].soloed = soloed;

    updateRuntimeState([track, soloed](RuntimePlaybackState& state)
    {
        if (track >= 0 && track < (int)state.mixerTracks.size())
            state.mixerTracks[(size_t)track].soloed = soloed;
    });
}
void AudioEngine::setMasterVolume(float vol)
{
    if (project) project->masterVolume = vol;

    updateRuntimeState([vol](RuntimePlaybackState& state)
    {
        state.masterVolume = vol;
    });
}
void AudioEngine::setMasterPan(float pan)
{
    if (project) project->masterPan = pan;

    updateRuntimeState([pan](RuntimePlaybackState& state)
    {
        state.masterPan = pan;
    });
}

// ---- BPM / Mode / Project ----------------------------------------------

void AudioEngine::setBPM(double newBpm)
{
    bpm.store(newBpm, std::memory_order_relaxed);
    sequencer.setBPM(newBpm);

    updateRuntimeState([newBpm](RuntimePlaybackState& state)
    {
        state.projectBpm = newBpm;
    });
}

bool AudioEngine::isPlaying() const
{
    if (playMode == PlayMode::Pattern)
        return sequencer.isPlaying();
    return songPlaying.load(std::memory_order_acquire);
}

void AudioEngine::setPlayMode(PlayMode mode)
{
    playMode = mode;
}

void AudioEngine::setProject(Project* projectPtr)
{
    project = projectPtr;
    rebuildRuntimeStateFromProject();
}

void AudioEngine::setPatternStepCount(int stepCount)
{
    sequencer.setStepCount(stepCount);
}

void AudioEngine::loadSample(int channelIndex, const juce::File& file)
{
    if (channelIndex >= 0 && channelIndex < 16)
    {
        SamplePlayer helper;
        helper.prepare(sampleRate, bufferSize);
        helper.loadFile(file);

        if (helper.isLoaded())
            updateChannelSourceSnapshot(channelIndex, helper.takeLoadedBuffer());
        else
            updateChannelSourceSnapshot(channelIndex, nullptr);
    }
}

void AudioEngine::unloadSample(int channelIndex)
{
    if (channelIndex >= 0 && channelIndex < 16)
        updateChannelSourceSnapshot(channelIndex, nullptr);
}

void AudioEngine::buildSongSampleCache()
{
    const int nextIdx = 1 - activeSongCacheIdx_.load(std::memory_order_relaxed);
    auto& songSampleCache = songSampleCaches_[nextIdx];
    songSampleCache.clear();
    std::fill_n(songPlayerClipId, 16, -1);

    const auto& runtime = getRuntimeState();

    for (const auto& pat : runtime.patterns)
    {
        auto& cache = songSampleCache[pat.id];

        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            if (pat.samplePaths[ch].isEmpty()) continue;

            // Decode the file on the message thread into a temporary player,
            // then copy the decoded buffer into the cache (no disk I/O at runtime)
            SamplePlayer helper;
            helper.prepare(sampleRate, bufferSize);
            helper.loadFile(juce::File(pat.samplePaths[ch]));
            if (helper.isLoaded())
                cache[(size_t)ch] = helper.takeLoadedBuffer();
            else
                cache[(size_t)ch].reset();
        }
    }

    activeSongCacheIdx_.store(nextIdx, std::memory_order_release);
}

void AudioEngine::triggerChannel(int channelIndex, int offsetInBuffer)
{
    if (channelIndex < 0 || channelIndex >= 16) return;

    // Use snapshot for lock-free access to synth params on audio thread
    const PlaybackSnapshot& trigSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];

    // If a synth is enabled on this channel, trigger a synth voice at the
    // pitch-slider note (channelBasePitch semitone offset from C4=60).
    // This lets Drum-mode channels use the synth engine with the step grid.
    if (trigSnap.patternId >= 0 && trigSnap.synthParams[(size_t)channelIndex].enabled)
    {
        const int   midiPitch = juce::jlimit(0, 127,
                                    60 + (int)std::round(channelBasePitch[(size_t)channelIndex].load(std::memory_order_relaxed)));
        const int   noteLen   = juce::jmax(1,
                                    (int)(0.25 * sampleRate * 60.0 / bpm.load(std::memory_order_relaxed))); // 1/16 note
        const auto noteParams = makeNoteSynthParams(trigSnap.synthParams[(size_t)channelIndex],
                                                    midiPitch, 0.8f, noteLen);
        polySynths[(size_t)channelIndex].noteOn(
            midiPitch, 0.8f, sampleRate,
            noteParams, noteLen);
        return;
    }

    const float volume = (trigSnap.patternId >= 0) ? trigSnap.channelVolume[(size_t)channelIndex]
                                                   : sampleChannelVolume_[(size_t)channelIndex].load(std::memory_order_relaxed);
    const float pan = (trigSnap.patternId >= 0) ? trigSnap.channelPan[(size_t)channelIndex]
                                                : sampleChannelPan_[(size_t)channelIndex].load(std::memory_order_relaxed);
    const float pitch = channelBasePitch[(size_t)channelIndex].load(std::memory_order_relaxed)
                      + ((trigSnap.patternId >= 0) ? trigSnap.channelPitch[(size_t)channelIndex] : 0.0f);
    auto sourceBuffer = getChannelSourceBufferShared(channelIndex);
    scheduleSampleTrigger(channelIndex, offsetInBuffer, sourceBuffer.get(), sourceBuffer,
                          volume, pan, pitch, 1.0f);
}

const juce::AudioBuffer<float>* AudioEngine::getChannelSourceBuffer(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= 16) return nullptr;
    const auto& snapshot = getChannelSourceSnapshot();
    const auto& buffer = snapshot.buffers[(size_t)channelIndex];
    return buffer != nullptr ? buffer.get() : nullptr;
}

std::shared_ptr<const juce::AudioBuffer<float>> AudioEngine::getChannelSourceBufferShared(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= 16) return nullptr;
    const auto& snapshot = getChannelSourceSnapshot();
    return snapshot.buffers[(size_t)channelIndex];
}

void AudioEngine::scheduleSampleTrigger(int channelIndex,
                                        int offsetInBuffer,
                                        const juce::AudioBuffer<float>* sourceBuffer,
                                        std::shared_ptr<const juce::AudioBuffer<float>> ownedSourceBuffer,
                                        float volume,
                                        float pan,
                                        float pitchSemitones,
                                        float bpmRatio)
{
    if (channelIndex < 0 || channelIndex >= 16 || sourceBuffer == nullptr)
        return;

    ScheduledSampleTrigger trigger;
    trigger.channel = channelIndex;
    trigger.offsetInBuffer = juce::jmax(0, offsetInBuffer);
    trigger.sourceBuffer = sourceBuffer;
    trigger.ownedSourceBuffer = std::move(ownedSourceBuffer);
    trigger.volume = volume;
    trigger.pan = pan;
    trigger.pitchSemitones = pitchSemitones;
    trigger.attackMs = sampleChannelAttackMs_[(size_t)channelIndex].load(std::memory_order_relaxed);
    trigger.releaseMs = sampleChannelReleaseMs_[(size_t)channelIndex].load(std::memory_order_relaxed);
    trigger.bpmRatio = bpmRatio;
    trigger.muted = channelMuted[(size_t)channelIndex].load(std::memory_order_relaxed);
    scheduledSampleTriggers_.push(trigger);
}

void AudioEngine::triggerSampleVoiceNow(int channelIndex,
                                        int offsetInBuffer,
                                        const juce::AudioBuffer<float>* sourceBuffer,
                                        std::shared_ptr<const juce::AudioBuffer<float>> ownedSourceBuffer,
                                        float volume,
                                        float pan,
                                        float pitchSemitones,
                                        float bpmRatio)
{
    if (channelIndex < 0 || channelIndex >= 16 || sourceBuffer == nullptr)
        return;

    auto& pool = sampleVoicePools[(size_t)channelIndex];
    auto& voice = pool.allocateVoice();
    if (ownedSourceBuffer)
        voice.setSharedExternalBuffer(ownedSourceBuffer);
    else
        voice.setExternalBuffer(sourceBuffer);
    voice.setVolume(volume);
    voice.setPan(pan);
    voice.setPitch(pitchSemitones);
    voice.setAttack(juce::jmax(0.5f, sampleChannelAttackMs_[(size_t)channelIndex].load(std::memory_order_relaxed)));
    voice.setRelease(juce::jmax(2.0f, sampleChannelReleaseMs_[(size_t)channelIndex].load(std::memory_order_relaxed)));
    voice.setBpmRatio(bpmRatio);
    const bool anySoloed = std::any_of(std::begin(channelSoloed), std::end(channelSoloed),
                                       [](const std::atomic<bool>& soloed) { return soloed.load(std::memory_order_relaxed); });
    const bool silenced = channelMuted[(size_t)channelIndex].load(std::memory_order_relaxed)
                       || (anySoloed && !channelSoloed[(size_t)channelIndex].load(std::memory_order_relaxed));
    voice.setMuted(silenced);
    voice.triggerAt(juce::jmax(0, offsetInBuffer));
}

void AudioEngine::dispatchScheduledSampleTriggers()
{
    for (int i = 0; i < scheduledSampleTriggers_.count; ++i)
    {
        const auto& trigger = scheduledSampleTriggers_.triggers[(size_t)i];
        triggerSampleVoiceNow(trigger.channel, trigger.offsetInBuffer, trigger.sourceBuffer,
                              trigger.ownedSourceBuffer, trigger.volume, trigger.pan, trigger.pitchSemitones,
                              trigger.bpmRatio);
    }

    scheduledSampleTriggers_.clear();
}

void AudioEngine::setStepPattern(int channelIndex, int stepIndex, bool active)
{
    sequencer.setStep(channelIndex, stepIndex, active);
}

// ---- M12 — MIDI input -------------------------------------------------------

juce::Array<juce::MidiDeviceInfo> AudioEngine::getMidiInputDevices() const
{
    return juce::MidiInput::getAvailableDevices();
}

void AudioEngine::openMidiDevice(const juce::String& deviceId)
{
    closeMidiDevice();
    midiInput = juce::MidiInput::openDevice(deviceId, this);
    if (midiInput != nullptr)
        midiInput->start();
}

void AudioEngine::closeMidiDevice()
{
    if (midiInput != nullptr)
    {
        midiInput->stop();
        midiInput.reset();
    }
}

void AudioEngine::setMidiTargetChannel(int ch)
{
    midiTargetChannel = juce::jlimit(0, 15, ch);
}

juce::String AudioEngine::getOpenMidiDeviceId() const
{
    return midiInput != nullptr ? midiInput->getIdentifier() : juce::String{};
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg)
{
    // Called on the MIDI thread — push into thread-safe collector
    midiCollector.addMessageToQueue(msg);
}

// ---- Audio callbacks ---------------------------------------------------

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const*,
    int,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    juce::AudioBuffer<float> buffer(outputChannelData, numOutputChannels, numSamples);
    buffer.clear();
    scheduledSampleTriggers_.clear();

    // M8 — clear per-channel plugin MIDI buffers at the start of each block
    for (auto& mb : instrumentMidiBuffers)
        mb.clear();

    // M12 — process incoming MIDI messages
    {
        incomingMidiBuffer_.clear();
        midiCollector.removeNextBlockOfMessages(incomingMidiBuffer_, numSamples);

        const int ch = juce::jlimit(0, 15, midiTargetChannel);

        for (const auto meta : incomingMidiBuffer_)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                const int   pitch = msg.getNoteNumber();
                const float vel   = msg.getFloatVelocity();

                // M8 — route to VST/AU plugin if one is loaded
                if (instrumentPlugins[(size_t)ch] != nullptr)
                {
                    instrumentMidiBuffers[ch].addEvent(
                        juce::MidiMessage::noteOn(1, pitch, vel),
                        meta.samplePosition);
                }
                else
                {
                    const PlaybackSnapshot& midiSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];
                    const bool useSynth = (midiSnap.patternId >= 0) &&
                                           midiSnap.synthParams[(size_t)ch].enabled;
                    if (useSynth)
                    {
                        const auto noteParams = makeNoteSynthParams(midiSnap.synthParams[(size_t)ch],
                                                                    pitch, vel, 0);
                        polySynths[(size_t)ch].noteOn(pitch, vel, sampleRate, noteParams, 0);
                    }
                    else
                    {
                        const float volume = (midiSnap.patternId >= 0)
                                           ? midiSnap.channelVolume[(size_t)ch]
                                           : sampleChannelVolume_[(size_t)ch].load(std::memory_order_relaxed);
                        const float pan = (midiSnap.patternId >= 0)
                                        ? midiSnap.channelPan[(size_t)ch]
                                        : sampleChannelPan_[(size_t)ch].load(std::memory_order_relaxed);
                        auto sourceBuffer = getChannelSourceBufferShared(ch);
                        scheduleSampleTrigger(ch, meta.samplePosition, sourceBuffer.get(), sourceBuffer,
                                              volume, pan,
                                              channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) + (float)(pitch - 60), 1.0f);
                    }
                }
            }
            else if (msg.isNoteOff())
            {
                // M8 — route note-off to plugin if loaded
                if (instrumentPlugins[(size_t)ch] != nullptr)
                {
                    instrumentMidiBuffers[ch].addEvent(
                        juce::MidiMessage::noteOff(1, msg.getNoteNumber()),
                        meta.samplePosition);
                }
                else
                {
                    polySynths[(size_t)ch].noteOff(msg.getNoteNumber());
                }
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                if (instrumentPlugins[(size_t)ch] != nullptr)
                    instrumentMidiBuffers[ch].addEvent(
                        juce::MidiMessage::allNotesOff(1), 0);
                else
                    polySynths[(size_t)ch].allNotesOff();
            }
        }
    }

    if (playMode == PlayMode::Pattern)
        processPatternMode(buffer, numSamples, numOutputChannels);
    else
        processSongMode(buffer, numSamples, numOutputChannels);

    // Safe sample count: guard against stagingBuf being smaller than numSamples
    // (can happen if the driver delivers a different block size than configured).
    const int stagingSafe = juce::jmin(numSamples, stagingBuf.getNumSamples());

    // Launchpad one-shot players — render directly into output (bypass mixer)
    const auto& runtime = getRuntimeState();
    const float mv = runtime.masterVolume;
    for (auto& lp : launchpadPlayers)
    {
        if (!lp.isLoaded()) continue;
        stagingBuf.clear();
        lp.renderNextBlock(stagingBuf, stagingSafe);
        for (int s = 0; s < stagingSafe; ++s)
        {
            if (buffer.getNumChannels() > 0)
                buffer.addSample(0, s, stagingBuf.getSample(0, s) * mv);
            if (buffer.getNumChannels() > 1)
                buffer.addSample(1, s, stagingBuf.getSample(1, s) * mv);
        }
    }

    // M15 — browser preview player (bypass mixer, always audible)
    if (browserPreviewPlayer.isLoaded())
    {
        stagingBuf.clear();
        browserPreviewPlayer.renderNextBlock(stagingBuf, stagingSafe);
        for (int s = 0; s < stagingSafe; ++s)
        {
            if (buffer.getNumChannels() > 0)
                buffer.addSample(0, s, stagingBuf.getSample(0, s));
            if (buffer.getNumChannels() > 1)
                buffer.addSample(1, s, stagingBuf.getSample(1, s));
        }
    }
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    bufferSize = device->getCurrentBufferSizeSamples();

    sequencer.prepare(sampleRate, bufferSize);

    for (auto& pool : sampleVoicePools)
        pool.prepare(sampleRate, bufferSize);

    // M12 — reset MIDI collector to current sample rate
    midiCollector.reset(sampleRate);

    // M13/M14 — prepare synths and FX chains
    for (auto& s : polySynths) s.prepare(sampleRate, bufferSize);
    for (auto& fx : fxChains)  fx.prepare(sampleRate, bufferSize);

    // Launchpad players
    for (auto& lp : launchpadPlayers) lp.prepare(sampleRate, bufferSize);

    // M15 — browser preview player
    browserPreviewPlayer.prepare(sampleRate, bufferSize);

    incomingMidiBuffer_.ensureSize(2048);
    for (auto& mb : instrumentMidiBuffers)
        mb.ensureSize(2048);

    // M5 — allocate mixer staging buffers
    stagingBuf.setSize(2, bufferSize);
    for (auto& tb : mixerTrackBufs)
        tb.setSize(2, bufferSize);

    // Dynamic EQ — prepare all processors
    for (auto& deq : trackDynEQs_) deq.prepare(sampleRate, bufferSize);
    masterDynEQ_.prepare(sampleRate, bufferSize);

    // M8 — re-prepare any already-loaded instrument plugins
    {
        juce::ScopedLock sl(pluginLock);
        for (auto& plugin : instrumentPlugins)
        {
            if (plugin != nullptr)
            {
                plugin->setPlayConfigDetails(0, 2, sampleRate, bufferSize);
                plugin->prepareToPlay(sampleRate, bufferSize);
            }
        }
    }
}

void AudioEngine::audioDeviceStopped()
{
    for (auto& pool : sampleVoicePools) pool.reset();
    for (auto& s : polySynths)         s.reset();
    for (auto& fx : fxChains)          fx.reset();
    for (auto& lp : launchpadPlayers)  lp.reset();

    // M8 — release plugin resources (audio engine stopped, safe to lock normally)
    juce::ScopedLock sl(pluginLock);
    for (auto& plugin : instrumentPlugins)
        if (plugin != nullptr)
            plugin->releaseResources();
}

// ---- M8 — VST/AU Instrument Plugin management --------------------------

void AudioEngine::loadPlugin(int ch, const juce::PluginDescription& desc,
                              juce::String& errorMsg)
{
    if (ch < 0 || ch >= 16) return;

    // Create the instance on the message thread (may take a moment — that's OK)
    auto instance = PluginManager::getInstance().createPlugin(desc, sampleRate,
                                                               bufferSize, errorMsg);
    if (instance == nullptr) return;

    // Configure I/O: 0 audio inputs, 2 audio outputs (stereo instrument)
    instance->setPlayConfigDetails(0, 2, sampleRate, bufferSize);
    instance->prepareToPlay(sampleRate, bufferSize);

    {
        juce::ScopedLock sl(pluginLock);
        if (instrumentPlugins[(size_t)ch] != nullptr)
            instrumentPlugins[(size_t)ch]->releaseResources();
        instrumentPlugins[(size_t)ch] = std::move(instance);
        activePluginNotes[ch].clear();
    }
}

void AudioEngine::unloadPlugin(int ch)
{
    if (ch < 0 || ch >= 16) return;
    juce::ScopedLock sl(pluginLock);
    if (instrumentPlugins[(size_t)ch] != nullptr)
    {
        instrumentPlugins[(size_t)ch]->releaseResources();
        instrumentPlugins[(size_t)ch].reset();
        activePluginNotes[ch].clear();
    }
}

bool AudioEngine::hasPlugin(int ch) const
{
    if (ch < 0 || ch >= 16) return false;
    return instrumentPlugins[(size_t)ch] != nullptr;
}

juce::AudioPluginInstance* AudioEngine::getPlugin(int ch)
{
    if (ch < 0 || ch >= 16) return nullptr;
    return instrumentPlugins[(size_t)ch].get();
}

bool AudioEngine::getPluginState(int ch, juce::MemoryBlock& stateOut) const
{
    if (ch < 0 || ch >= 16) return false;
    if (instrumentPlugins[(size_t)ch] == nullptr) return false;
    instrumentPlugins[(size_t)ch]->getStateInformation(stateOut);
    return true;
}

// ---- Render helpers ----------------------------------------------------

void AudioEngine::processPatternMode(juce::AudioBuffer<float>& buffer,
                                     int numSamples,
                                     int /*numOutputChannels*/)
{
    sequencer.processBlock(numSamples);

    // M3 — trigger NoteEvents for Melodic channels (only while sequencer is playing)
    // Use double-buffered snapshot for lock-free pattern data access on audio thread.
    const PlaybackSnapshot& snap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];

    if (sequencer.isPlaying() && snap.patternId >= 0 && sampleRate > 0.0)
    {
        const double samplesPerBeat = sampleRate * 60.0 / bpm.load(std::memory_order_relaxed);
        const double patternBeats   = snap.stepCount * 0.25;  // 1 step = 1/16 note

        if (patternBeats > 0.0)
        {
            const double startBeat = patternBeatPos;
            const double endBeat   = startBeat + numSamples / samplesPerBeat;

            const double loopStart = std::fmod(startBeat, patternBeats);
            const double loopEnd   = std::fmod(endBeat,   patternBeats);

            for (int ch = 0; ch < PlaybackSnapshot::kCh; ++ch)
            {
                if (snap.channelTypes[ch] != ChannelType::Melodic) continue;

                // M8 — flush pending note-offs for plugin channels
                if (instrumentPlugins[(size_t)ch] != nullptr)
                {
                    auto& notes = activePluginNotes[ch];
                    for (int i = 0; i < notes.count; )
                    {
                        const auto& activeNote = notes.notes[(size_t)i];
                        const double noteEndLoop = std::fmod(activeNote.endBeat, patternBeats);
                        const bool fires = (loopEnd < loopStart)
                                           ? (noteEndLoop >= loopStart || noteEndLoop < loopEnd)
                                           : (noteEndLoop >= loopStart && noteEndLoop < loopEnd);
                        if (fires)
                        {
                            instrumentMidiBuffers[ch].addEvent(
                                juce::MidiMessage::noteOff(1, activeNote.pitch), 0);
                            notes.removeAt(i);
                        }
                        else { ++i; }
                    }
                }

                const auto& slot = snap.noteSlots[ch];
                for (int n = 0; n < slot.count; ++n)
                {
                    const auto& note = slot.notes[n];
                    const double ns = std::fmod((double)note.startBeat, patternBeats);
                    const bool fires = (loopEnd < loopStart)
                                       ? (ns >= loopStart || ns < loopEnd)
                                       : (ns >= loopStart && ns < loopEnd);
                    if (fires)
                    {
                        // M8 — VST/AU plugin path
                        const int tp = juce::jlimit(0, 127,
                            note.pitch + (int)std::round(channelBasePitch[(size_t)ch].load(std::memory_order_relaxed)));
                        if (instrumentPlugins[(size_t)ch] != nullptr)
                        {
                            instrumentMidiBuffers[ch].addEvent(
                                juce::MidiMessage::noteOn(1, tp, note.velocity), 0);
                            // Schedule note-off at endBeat
                            const double absoluteEnd = startBeat +
                                (ns - loopStart + (loopEnd < loopStart ? patternBeats : 0.0))
                                + note.lengthBeats;
                            activePluginNotes[ch].push({ absoluteEnd, tp });
                        }
                        else
                        {
                            const SynthParams& sp = snap.synthParams[(size_t)ch];
                            if (sp.enabled)
                            {
                                const int noteLenSamples = (int)(note.lengthBeats * samplesPerBeat);
                                const auto noteParams = makeNoteSynthParams(sp, tp, note.velocity, noteLenSamples);
                                polySynths[(size_t)ch].noteOn(tp, note.velocity,
                                                               sampleRate, noteParams, noteLenSamples);
                            }
                            else
                            {
                                // Apply note velocity as volume scale for sample channels
                                auto sourceBuffer = getChannelSourceBufferShared(ch);
                                scheduleSampleTrigger(ch, 0, sourceBuffer.get(), sourceBuffer,
                                                      snap.channelVolume[ch] * note.velocity,
                                                      snap.channelPan[ch],
                                                      channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) + (float)(note.pitch - 60),
                                                      1.0f);
                            }
                        }
                    }
                }
            }

            patternBeatPos += numSamples / samplesPerBeat;
        }
    }

    dispatchScheduledSampleTriggers();
    mixToOutput(buffer, numSamples);
}

void AudioEngine::processSongMode(juce::AudioBuffer<float>& buffer,
                                  int numSamples,
                                  int /*numOutputChannels*/)
{
    const auto& runtime = getRuntimeState();
    if (!songPlaying.load(std::memory_order_acquire) || sampleRate <= 0.0)
        return;

    if (runtime.playlistClips.empty() || runtime.patterns.empty())
        return;

    double currentBpm = runtime.projectBpm;
    float effectiveMasterVolume = runtime.masterVolume;
    std::array<float, 16> automatedChannelVolumes;
    automatedChannelVolumes.fill(-1.0f);

    if (!runtime.automationLanes.empty())
    {
        const double currentSamplesPerBeat = sampleRate * 60.0 / currentBpm;
        const double currentBeat = (double)songSamplePosition.load(std::memory_order_relaxed) / currentSamplesPerBeat;

        for (const auto& lane : runtime.automationLanes)
        {
            if (lane.paramId != "bpm") continue;

            currentBpm = (double)lane.evaluate(currentBeat);
            bpm.store(currentBpm, std::memory_order_relaxed);
            sequencer.setBPM(currentBpm);

            const double baseProjectBpm = juce::jmax(1.0, runtime.projectBpm);
            for (auto& pool : sampleVoicePools)
                for (auto& voice : pool.voices)
                    voice.setBpmRatio(currentBpm / baseProjectBpm);
        }
    }

    const double samplesPerBeat = sampleRate * 60.0 / currentBpm;
    const double samplesPerBar  = samplesPerBeat * 4.0;
    const long startSample = songSamplePosition.load(std::memory_order_relaxed);
    const long endSample   = startSample + numSamples;
    const double startBar = (double)startSample / samplesPerBar;
    const double endBar   = (double)endSample / samplesPerBar;
    const double automationBeat = ((double)startSample + numSamples * 0.5) / samplesPerBeat;

    if (!runtime.automationLanes.empty())
    {
        for (const auto& lane : runtime.automationLanes)
        {
            if (lane.paramId == "bpm") continue;

            const float v = lane.evaluate(automationBeat);
            if (lane.paramId == "masterVolume")
                effectiveMasterVolume = v;
            else if (lane.paramId.startsWith("ch") && lane.paramId.endsWith("vol"))
            {
                const int ch = lane.paramId.substring(2, lane.paramId.length() - 3).getIntValue();
                if (ch >= 0 && ch < 16)
                    automatedChannelVolumes[(size_t)ch] = v;
            }
        }
    }

    const auto& songSampleCache = getSongSampleCache();
    const int timelineStepsPerBar = 16;
    const int startStep = (int)std::floor(startBar * timelineStepsPerBar);
    const int endStep   = (int)std::floor(endBar   * timelineStepsPerBar);
    const auto updateSongMixState = [this, &automatedChannelVolumes](const Pattern& pattern, int ch)
    {
        if (ch < 0 || ch >= Pattern::kMaxChannels)
            return;

        songChannelVolume_[(size_t)ch] = automatedChannelVolumes[(size_t)ch] >= 0.0f
                                       ? automatedChannelVolumes[(size_t)ch]
                                       : pattern.channelVolume[ch];
        songChannelPan_[(size_t)ch] = pattern.channelPan[ch];
        songChannelMixerRouting_[(size_t)ch] = juce::jlimit(0, 7, pattern.channelMixerRouting[ch]);
    };

    for (int stepIndex = startStep; stepIndex <= endStep; ++stepIndex)
    {
        const double stepBarPos  = (double)stepIndex / (double)timelineStepsPerBar;
        const long   stepSample  = (long)(stepBarPos * samplesPerBar);
        const int    offsetInBuf = (int)(stepSample - startSample);

        if (offsetInBuf < 0 || offsetInBuf >= numSamples)
            continue;

        for (const auto& clip : runtime.playlistClips)
        {
            if (stepBarPos < clip.startBar || stepBarPos >= clip.startBar + clip.lengthBars)
                continue;

            const Pattern* pattern = runtime.findPatternById(clip.patternId);
            if (pattern == nullptr || pattern->stepCount <= 0)
                continue;

            const double patternBars = (double)pattern->stepCount / 16.0;
            const double localBarInClip = stepBarPos - clip.startBar;
            const double localInPat     = std::fmod(juce::jmax(0.0, localBarInClip), patternBars);
            const double pos01          = localInPat / patternBars;
            const int localStep = juce::jlimit(0, pattern->stepCount - 1,
                                               (int)std::floor(pos01 * (double)pattern->stepCount));

            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            {
                updateSongMixState(*pattern, ch);
                if (!pattern->steps[ch][localStep]) continue;

                const bool useSynth = pattern->synthParams[(size_t)ch].enabled;
                if (useSynth)
                {
                    const int midiPitch = juce::jlimit(0, 127,
                        60 + (int)std::round(channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) + pattern->channelPitch[ch]));
                    const int noteLenSamples = juce::jmax(1, (int)(0.25 * samplesPerBeat));
                    const auto noteParams = makeNoteSynthParams(pattern->synthParams[(size_t)ch],
                                                                midiPitch, 0.8f, noteLenSamples);
                    polySynths[(size_t)ch].noteOn(midiPitch, 0.8f, sampleRate, noteParams, noteLenSamples);
                }
                else
                {
                    auto cit = songSampleCache.find(clip.patternId);
                    std::shared_ptr<const juce::AudioBuffer<float>> songBuffer =
                        (cit != songSampleCache.end()) ? cit->second[(size_t)ch] : nullptr;

                    scheduleSampleTrigger(ch, offsetInBuf, songBuffer.get(), songBuffer,
                                          songChannelVolume_[(size_t)ch],
                                          songChannelPan_[(size_t)ch],
                                          channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) + pattern->channelPitch[ch],
                                          (float)(currentBpm / juce::jmax(1.0, runtime.projectBpm)));
                }
            }
        }
    }

    {
        const double startBeatSong = (double)startSample / samplesPerBeat;
        const double endBeatSong   = (double)endSample   / samplesPerBeat;

        for (const auto& clip : runtime.playlistClips)
        {
            const Pattern* pat = runtime.findPatternById(clip.patternId);
            if (pat == nullptr || pat->stepCount <= 0) continue;

            const double patternBeats  = pat->stepCount * 0.25;
            const double clipStartBeat = clip.startBar * 4.0;
            const double clipEndBeat   = (clip.startBar + clip.lengthBars) * 4.0;

            if (endBeatSong <= clipStartBeat || startBeatSong >= clipEndBeat) continue;

            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            {
                if (pat->channelTypes[ch] != ChannelType::Melodic) continue;
                updateSongMixState(*pat, ch);

                for (const auto& note : pat->notes[ch])
                {
                    const double notePhase = std::fmod((double)note.startBeat, patternBeats);
                    const double relStart  = startBeatSong - clipStartBeat;
                    const double loopIdxD  = (relStart - notePhase) / patternBeats;
                    const int kStart = (loopIdxD < 0.0) ? 0 : (int)loopIdxD;

                    for (int k = kStart; k <= kStart + 1; ++k)
                    {
                        const double fireBeat = clipStartBeat + k * patternBeats + notePhase;
                        if (fireBeat < clipStartBeat || fireBeat >= clipEndBeat) continue;
                        if (fireBeat < startBeatSong || fireBeat >= endBeatSong)  continue;

                        const SynthParams& sp = pat->synthParams[(size_t)ch];
                        const int tp2 = juce::jlimit(0, 127, note.pitch + (int)std::round(channelBasePitch[(size_t)ch].load(std::memory_order_relaxed)));

                        if (sp.enabled)
                        {
                            const int noteLenSamples = (int)(note.lengthBeats * samplesPerBeat);
                            const auto noteParams = makeNoteSynthParams(sp, tp2, note.velocity, noteLenSamples);
                            polySynths[(size_t)ch].noteOn(tp2, note.velocity, sampleRate, noteParams, noteLenSamples);
                        }
                        else
                        {
                            auto cit = songSampleCache.find(clip.patternId);
                            std::shared_ptr<const juce::AudioBuffer<float>> songBuffer =
                                (cit != songSampleCache.end()) ? cit->second[(size_t)ch] : nullptr;
                            const int offset = juce::jlimit(0, numSamples - 1,
                                                            (int)((fireBeat - startBeatSong) * samplesPerBeat));
                            scheduleSampleTrigger(ch, offset, songBuffer.get(), songBuffer,
                                                  songChannelVolume_[(size_t)ch] * note.velocity,
                                                  songChannelPan_[(size_t)ch],
                                                  channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) + (float)(note.pitch - 60),
                                                  (float)(currentBpm / juce::jmax(1.0, runtime.projectBpm)));
                        }
                    }
                }
            }
        }
    }

    dispatchScheduledSampleTriggers();
    MixRuntimeOverrides overrides;
    overrides.hasMasterOverride = true;
    overrides.masterVolume = effectiveMasterVolume;
    overrides.masterPan = runtime.masterPan;
    mixToOutput(buffer, numSamples, &overrides);
    songSamplePosition.store(startSample + numSamples, std::memory_order_relaxed);
}

// M5 — route channels through mixer tracks and sum to output
void AudioEngine::mixToOutput(juce::AudioBuffer<float>& output, int numSamples,
                              const MixRuntimeOverrides* overrides)
{
    // Guard: stagingBuf and mixerTrackBufs are allocated to bufferSize in
    // audioDeviceAboutToStart. If the driver delivers a smaller block, clamp.
    const int safe = juce::jmin(numSamples, stagingBuf.getNumSamples());

    // Clear mixer track buses
    for (auto& tb : mixerTrackBufs)
        tb.clear();

    const auto& runtime = getRuntimeState();

    // Use snapshot for routing and synth params — lock-free audio thread access
    const PlaybackSnapshot& mixSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];

    // Render each channel into staging, accumulate into its assigned mixer track
    for (int ch = 0; ch < 16; ++ch)
    {
        stagingBuf.clear();

        // M8 — VST/AU instrument plugin takes priority over synth / sample player
        const bool usePlugin = (instrumentPlugins[(size_t)ch] != nullptr);
        const bool useSynth  = !usePlugin &&
                               (polySynths[(size_t)ch].isAnyActive() ||
                                ((mixSnap.patternId >= 0) && mixSnap.synthParams[(size_t)ch].enabled));

        if (usePlugin)
        {
            // Try-lock: if the message thread is loading/unloading, skip this block
            const juce::ScopedTryLock sl(pluginLock);
            if (sl.isLocked() && instrumentPlugins[(size_t)ch] != nullptr)
            {
                // Build a sub-buffer view sized to `safe` samples (no allocation)
                juce::AudioBuffer<float> plugBuf(stagingBuf.getArrayOfWritePointers(),
                                                  2, safe);
                instrumentPlugins[(size_t)ch]->processBlock(plugBuf,
                                                             instrumentMidiBuffers[ch]);

                // If the plugin is mono, duplicate L → R
                const int plugOuts = instrumentPlugins[(size_t)ch]->getTotalNumOutputChannels();
                if (plugOuts == 1 && stagingBuf.getNumChannels() > 1)
                    stagingBuf.copyFrom(1, 0, stagingBuf, 0, 0, safe);
            }
        }
        else if (useSynth)
        {
            float* L = stagingBuf.getWritePointer(0);
            float* R = stagingBuf.getWritePointer(1);
            polySynths[(size_t)ch].renderNextBlock(L, R, safe, sampleRate);
        }
        else
        {
            sampleVoicePools[(size_t)ch].renderNextBlock(stagingBuf, safe);
        }

        if (usePlugin || useSynth)
        {
            const float channelVolume = (playMode == PlayMode::Song)
                                      ? songChannelVolume_[(size_t)ch]
                                      : ((mixSnap.patternId >= 0) ? mixSnap.channelVolume[(size_t)ch] : 0.8f);
            const float channelPan = (playMode == PlayMode::Song)
                                   ? songChannelPan_[(size_t)ch]
                                   : ((mixSnap.patternId >= 0) ? mixSnap.channelPan[(size_t)ch] : 0.0f);
            const float panAngle = (channelPan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float gainL = channelVolume * std::cos(panAngle);
            const float gainR = channelVolume * std::sin(panAngle);

            stagingBuf.applyGain(0, 0, safe, gainL);
            stagingBuf.applyGain(1, 0, safe, gainR);
        }

        // Use snapshot routing for lock-free per-pattern mixer assignment
        const int trackIdx = (playMode == PlayMode::Song)
                             ? juce::jlimit(0, 7, songChannelMixerRouting_[(size_t)ch])
                             : ((mixSnap.patternId >= 0)
                                ? juce::jlimit(0, 7, mixSnap.channelMixerRouting[(size_t)ch])
                                : 0);

        for (int s = 0; s < safe; ++s)
        {
            mixerTrackBufs[(size_t)trackIdx].addSample(0, s, stagingBuf.getSample(0, s));
            mixerTrackBufs[(size_t)trackIdx].addSample(1, s, stagingBuf.getSample(1, s));
        }
    }

    // M14 — apply FX chains per mixer track bus
    for (int t = 0; t < 8; ++t)
    {
        const FXParams& fp = runtime.fxParams[(size_t)t];
        fxChains[(size_t)t].processBlock(mixerTrackBufs[(size_t)t], safe, fp, bpm.load(std::memory_order_relaxed));

        // Dynamic EQ insert on each track bus (post-FX)
        if (trackDynEQs_[(size_t)t].isEnabled())
        {
            float* L = mixerTrackBufs[(size_t)t].getWritePointer(0);
            float* R = mixerTrackBufs[(size_t)t].getWritePointer(1);
            trackDynEQs_[(size_t)t].processBlock(L, R, safe);
        }
    }

    // Check if any track is soloed
    const bool anySoloed = runtime.anyMixerTrackSoloed();

    const float mv  = (overrides != nullptr && overrides->hasMasterOverride)
                    ? overrides->masterVolume
                    : runtime.masterVolume;
    const float mp  = (overrides != nullptr && overrides->hasMasterOverride)
                    ? overrides->masterPan
                    : runtime.masterPan;
    const float mAng = (mp + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float mL  = mv * std::cos(mAng);
    const float mR  = mv * std::sin(mAng);

    // Mix each track bus into output with track volume/pan
    for (int t = 0; t < 8; ++t)
    {
        const auto& mt = runtime.mixerTracks[(size_t)t];
        if (mt.muted || (anySoloed && !mt.soloed))
            continue;

        const float ang = (mt.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float L   = mt.volume * std::cos(ang);
        const float R   = mt.volume * std::sin(ang);

        for (int s = 0; s < safe; ++s)
        {
            if (output.getNumChannels() > 0)
                output.addSample(0, s, mixerTrackBufs[(size_t)t].getSample(0, s) * L * mL);
            if (output.getNumChannels() > 1)
                output.addSample(1, s, mixerTrackBufs[(size_t)t].getSample(1, s) * R * mR);
        }
    }

    // Master bus Dynamic EQ (post-sum, pre-output)
    if (masterDynEQ_.isEnabled() && output.getNumChannels() >= 2)
    {
        float* L = output.getWritePointer(0);
        float* R = output.getWritePointer(1);
        masterDynEQ_.processBlock(L, R, safe);
    }
}

// ---------------------------------------------------------------------------
// M10 — Offline render
// ---------------------------------------------------------------------------

bool AudioEngine::renderToFile(const juce::File& outputFile, PlayMode mode, int numBars)
{
    if (sampleRate <= 0.0 || numBars <= 0)
        return false;

    rebuildRuntimeStateFromProject();

    // Create WAV writer
    juce::WavAudioFormat wavFormat;
    auto fileStream = outputFile.createOutputStream();
    if (fileStream == nullptr)
        return false;

    constexpr int numOutChannels = 2;
    constexpr int bitDepth       = 24;

    auto* rawWriter = wavFormat.createWriterFor(
        fileStream.get(), sampleRate, numOutChannels, bitDepth, {}, 0);

    if (rawWriter == nullptr)
        return false;

    fileStream.release();                                     // writer owns stream
    std::unique_ptr<juce::AudioFormatWriter> writer(rawWriter);

    // ---- Remove real-time callback so the audio thread doesn't race us ----
    deviceManager.removeAudioCallback(this);

    // Reset all sample players and synths to a clean state
    for (auto& pool : sampleVoicePools) pool.reset();
    for (auto& s : polySynths) s.reset();
    for (auto& fx : fxChains)  fx.reset();

    // Set up playback state for offline pass
    const double samplesPerBar = (sampleRate * 60.0 / bpm.load(std::memory_order_relaxed)) * 4.0;
    const long   totalSamples  = (long)std::ceil((double)numBars * samplesPerBar);

    if (mode == PlayMode::Pattern)
    {
        patternBeatPos = 0.0;   // must reset before offline render
        sequencer.start();
    }
    else
    {
        buildSongSampleCache();
        songSamplePosition.store(0, std::memory_order_relaxed);
        songPlaying.store(true, std::memory_order_release);
    }

    // ---- Render loop -------------------------------------------------------
    constexpr int kBufSize = 512;
    juce::AudioBuffer<float> buffer(numOutChannels, kBufSize);

    long  rendered = 0;
    bool  ok       = true;

    while (rendered < totalSamples)
    {
        const int n = (int)juce::jmin((long)kBufSize, totalSamples - rendered);
        buffer.clear();

        if (mode == PlayMode::Pattern)
            processPatternMode(buffer, n, numOutChannels);
        else
            processSongMode(buffer, n, numOutChannels);

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, n))
        {
            ok = false;
            break;
        }

        rendered += n;
    }

    // ---- Tear down offline state -------------------------------------------
    if (mode == PlayMode::Pattern)
        sequencer.stop();
    else
        songPlaying.store(false, std::memory_order_release);

    // Re-attach real-time callback
    deviceManager.addAudioCallback(this);

    return ok;
}

// ---------------------------------------------------------------------------

const Pattern* AudioEngine::findPatternById(int patternId) const
{
    if (project == nullptr) return nullptr;
    for (const auto& p : project->patterns)
        if (p.id == patternId)
            return &p;
    return nullptr;
}
