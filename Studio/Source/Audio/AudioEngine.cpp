/*
  ==============================================================================

    AudioEngine.cpp
    Created: 6 Mar 2026 12:25:31pm
    Author:  홍준영

  ==============================================================================
*/

#include "AudioEngine.h"
#include <rubberband/RubberBandStretcher.h>

namespace
{
    inline float smoothingTowards(float current, float target, float alpha)
    {
        return current + (target - current) * juce::jlimit(0.0f, 1.0f, alpha);
    }

    inline float softSaturate(float x)
    {
        return x / (1.0f + 0.45f * std::abs(x));
    }

    inline float computeAdaptiveTrim(int routedSources, float peak)
    {
        const float sourceComp = 1.0f / std::sqrt(1.0f + 0.65f * (float) juce::jmax(0, routedSources - 1));
        const float peakComp = peak > 0.72f ? 0.72f / peak : 1.0f;
        return juce::jlimit(0.42f, 1.0f, sourceComp * peakComp);
    }

    // Fade gain for a clip at localBars position within the clip.
    // Handles proportional clamping when fade-in + fade-out > clip length.
    inline float computeClipFadeGain(const PlaylistClip& clip, float localBars)
    {
        if (clip.fadeInBars <= 0.0f && clip.fadeOutBars <= 0.0f) return 1.0f;

        float fadeIn  = clip.fadeInBars;
        float fadeOut = clip.fadeOutBars;

        // Proportional clamp: keep the ratio when they overlap
        const float total = fadeIn + fadeOut;
        if (total > clip.lengthBars && total > 0.0f)
        {
            const float scale = clip.lengthBars / total;
            fadeIn  *= scale;
            fadeOut *= scale;
        }

        float gain = 1.0f;
        if (fadeIn > 0.0f && localBars < fadeIn)
            gain = juce::jlimit(0.0f, 1.0f, localBars / fadeIn);
        if (fadeOut > 0.0f)
        {
            const float remain = clip.lengthBars - localBars;
            if (remain < fadeOut)
                gain = juce::jmin(gain, juce::jlimit(0.0f, 1.0f, remain / fadeOut));
        }
        return gain;
    }
}

AudioEngine::AudioEngine()
    : sequencer([this](int ch, int step, int offset) { triggerChannel(ch, step, offset); })
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
        channelPatternOverride_  [i] = -1;
        channelVariationOverride_[i] = 0;
        // CC real-time params: volume = 1.0 (no attenuation), pan = 0.5 (centre)
        ccVol_[i].setTarget(1.0f);  ccVol_[i].smoothed = 1.0f;
        ccPan_[i].setTarget(0.5f);  ccPan_[i].smoothed = 0.5f;
    }

    resetSongChannelMixState();
    resetMixProcessingState();

    for (auto& ch : midiHeldNotes_)
        for (auto& n : ch)
            n.store(0, std::memory_order_relaxed);

    // Wire LiveLoopEngine playback callbacks into the synth/plugin chain
    liveLoopEngine_.onNoteOn = [this](int ch, int pitch, float vel) noexcept
    {
        if (ch < 0 || ch >= 16) return;
        const PlaybackSnapshot& snap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];

        // Plugin path: inject NoteOn into plugin MIDI buffer
        if (snap.pluginSlotEnabled[(size_t)ch] && instrumentPlugins[(size_t)ch] != nullptr)
        {
            const int v = juce::jlimit(0, 127, (int)(vel * 127.0f));
            instrumentMidiBuffers[ch].addEvent(
                juce::MidiMessage::noteOn(1, pitch, (uint8_t)v), 0);
            return;
        }

        const ChannelSourceType srcType = snap.channelSourceTypes[(size_t)ch];
        SynthParams sp = snap.synthParams[(size_t)ch];
        // Only treat as synth if a pattern is active AND synth is explicitly enabled.
        // Without this guard the fallback saw fires during loop playback in live mode.
        const bool hasSynth = snap.patternId >= 0 && sp.enabled;
        const int  mixer    = juce::jlimit(0, 7,
            snap.patternId >= 0 ? snap.channelMixerRouting[(size_t)ch] : 0);
        const float vol = snap.patternId >= 0
                          ? snap.channelVolume[(size_t)ch]
                          : sampleChannelVolume_[(size_t)ch].load(std::memory_order_relaxed);
        const float pan = snap.patternId >= 0
                          ? snap.channelPan[(size_t)ch]
                          : sampleChannelPan_[(size_t)ch].load(std::memory_order_relaxed);

        // -- polySynth Sampler path (ChannelSourceType::Sampler) ---------------
        if (srcType == ChannelSourceType::Sampler)
        {
            auto samplerBuf = getSamplerSourceBuffer(ch);
            if (samplerBuf)
            {
                const int timbreLen = juce::jmax(1,
                    (int)(0.25 * sampleRate * 60.0 / bpm.load(std::memory_order_relaxed)));
                const int sustainLen = (int)(sampleRate * 60.0);
                const auto noteParams = makeNoteSynthParams(sp, pitch, vel, timbreLen);
                polySynths[(size_t)ch].noteOnSampler(pitch, vel, sampleRate,
                                                     noteParams,
                                                     snap.samplerParams[(size_t)ch],
                                                     std::move(samplerBuf),
                                                     sustainLen,
                                                     vol, pan, mixer);
                return;
            }
        }

        // -- Synth path --------------------------------------------------------
        if (hasSynth)
        {
            const int timbreLen = juce::jmax(1,
                (int)(0.25 * sampleRate * 60.0 / bpm.load(std::memory_order_relaxed)));
            const int sustainLen = (int)(sampleRate * 60.0);
            const auto noteParams = makeNoteSynthParams(sp, pitch, vel, timbreLen);
            polySynths[(size_t)ch].noteOn(pitch, vel, sampleRate, noteParams,
                                          sustainLen, vol, pan, mixer);
            return;
        }

        // -- Channel source buffer path (drag-dropped samples, old-style) -----
        {
            auto sourceBuffer = getChannelSourceBufferShared(ch);
            if (sourceBuffer != nullptr)
            {
                const float pitchShift = channelBasePitch[(size_t)ch].load(std::memory_order_relaxed)
                                         + (float)(pitch - 60);
                scheduleSampleTrigger(ch, 0, mixer,
                                      sourceBuffer.get(), sourceBuffer,
                                      vol, pan, pitchShift, 1.0f);
                return;
            }
        }

        // -- Ultimate fallback: basic saw so the loop is never silent ----------
        {
            SynthParams fbSp{};
            fbSp.enabled  = true;
            fbSp.waveform = 1;
            fbSp.attack   = 10.0f;
            fbSp.release  = 300.0f;
            const int timbreLen  = juce::jmax(1,
                (int)(0.25 * sampleRate * 60.0 / bpm.load(std::memory_order_relaxed)));
            const int sustainLen = (int)(sampleRate * 60.0);
            const auto np = makeNoteSynthParams(fbSp, pitch, vel, timbreLen);
            polySynths[(size_t)ch].noteOn(pitch, vel, sampleRate, np,
                                          sustainLen, vol, pan, mixer);
        }
    };
    liveLoopEngine_.onNoteOff = [this](int ch, int pitch) noexcept
    {
        if (ch < 0 || ch >= 16) return;
        const PlaybackSnapshot& snap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];

        // Plugin path: inject NoteOff into plugin MIDI buffer
        if (snap.pluginSlotEnabled[(size_t)ch] && instrumentPlugins[(size_t)ch] != nullptr)
        {
            instrumentMidiBuffers[ch].addEvent(juce::MidiMessage::noteOff(1, pitch), 0);
            return;
        }

        // Synth + Sampler voices both live in polySynths[ch]
        polySynths[(size_t)ch].noteOff(pitch);
    };
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

void AudioEngine::resetMixProcessingState()
{
    trackInputTrim_.fill(1.0f);
    masterInputTrim_ = 1.0f;
    masterGlueEnvelope_ = 0.0f;
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
        state.fxParams       = project->fxParams;
        state.autoTuneParams = project->autoTuneParams;
        state.patterns       = project->patterns;
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
    auto err = deviceManager.initialiseWithDefaultDevices(2, 2);
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

void AudioEngine::play(int patternStartStep, double songStartBar)
{
    // Clear stale note-off entries from previous playback
    for (auto& q : activePluginNotes) q.clear();

    if (playMode == PlayMode::Pattern)
    {
        const int stepCount = juce::jmax(1, sequencer.getStepCount());
        const int safeStartStep = juce::jlimit(0, stepCount - 1, patternStartStep);
        patternBeatPos = (double)safeStartStep * 0.25;   // M3
        sequencer.start(safeStartStep);
    }
    else
    {
        rebuildRuntimeStateFromProject();
        resetSongChannelMixState();
        ensureSongPluginsLoaded();

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

        const double safeSongStartBar = juce::jmax(0.0, songStartBar);
        songBeatPosition_.store(safeSongStartBar * 4.0, std::memory_order_relaxed);
        const double samplesPerBar = (sampleRate * 60.0 / bpm.load(std::memory_order_relaxed)) * 4.0;
        songSamplePosition.store((long)(safeSongStartBar * samplesPerBar), std::memory_order_relaxed);
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
    // Clear scheduled note-offs so they don't fire on next play
    for (auto& q : activePluginNotes) q.clear();

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
    resetAudioClipTriggers();
    // Note: liveLoopEngine_ is NOT reset on transport stop -- loops are independent of transport.
}

// --- Recording ----------------------------------------------------------

bool AudioEngine::startRecording(const juce::File& outputFile)
{
    recordStartBar_ = (playMode == PlayMode::Song)
                      ? songBeatPosition_.load(std::memory_order_relaxed) / 4.0
                      : 0.0;
    return recorder_.startRecording(outputFile, sampleRate, 2);
}

juce::File AudioEngine::stopRecording()
{
    if (!recorder_.isRecording())
        return {};

    // Compute length from actual samples written (reliable in both Pattern and Song mode)
    const juce::int64 samplesWritten = recorder_.getTotalSamplesWritten();
    const double currentBpm = bpm.load(std::memory_order_relaxed);
    const double samplesPerBar = (sampleRate * 60.0 / currentBpm) * 4.0;
    const double lengthBars = (samplesPerBar > 0.0)
                              ? (double)samplesWritten / samplesPerBar
                              : 1.0;
    const double startBar = recordStartBar_;

    recorder_.stopRecording();
    auto file = recorder_.getRecordedFile();

    if (onRecordingFinished && file.existsAsFile() && lengthBars > 0.0)
    {
        juce::MessageManager::callAsync([this, file, startBar, lengthBars]
        {
            if (onRecordingFinished)
                onRecordingFinished(file, startBar, lengthBars);
        });
    }

    return file;
}

juce::File AudioEngine::getRecordingDirectory() const
{
    // If a project file is set, store recordings next to it
    if (project != nullptr)
    {
        const auto& clips = project->playlistClips;
        // Look for any existing audio clip to infer project location
        for (const auto& clip : clips)
        {
            if (clip.clipType == ClipType::Audio && clip.audioFilePath.isNotEmpty())
            {
                juce::File f(clip.audioFilePath);
                if (f.existsAsFile())
                {
                    auto dir = f.getParentDirectory().getChildFile("Recordings");
                    dir.createDirectory();
                    return dir;
                }
            }
        }
    }

    // Fallback: ~/Documents/Studio/Recordings/
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("Studio").getChildFile("Recordings");
    dir.createDirectory();
    return dir;
}

// --- Audio clip management (Stage 1) ------------------------------------

void AudioEngine::loadAudioClip(int clipId, const juce::File& file, float clipLengthBars)
{
    if (!file.existsAsFile()) return;

    const juce::String path = file.getFullPathName();

    // Decode into shared buffer (one buffer per unique file path)
    std::shared_ptr<juce::AudioBuffer<float>> buf;
    {
        const juce::SpinLock::ScopedLockType lock(audioClipLock_);
        auto it = audioFileBuffers_.find(path);
        if (it != audioFileBuffers_.end())
        {
            buf = it->second;
        }
    }

    if (buf == nullptr)
    {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (reader == nullptr) return;

        auto decoded = std::make_shared<juce::AudioBuffer<float>>(
            (int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read(decoded.get(), 0, (int)reader->lengthInSamples, 0, true, true);
        buf = decoded;
    }

    const juce::SpinLock::ScopedLockType lock(audioClipLock_);
    audioFileBuffers_[path] = buf;

    // clipLengthBars is no longer used to cap playback: loop mode handles clip length
    // entirely via playhead-based calculation in processSongMode/mixToOutput.
    juce::ignoreUnused(clipLengthBars);

    // Update or add instance
    for (auto& inst : audioClipInstances_)
    {
        if (inst.clipId == clipId)
        {
            inst.buffer = buf;
            inst.active = false;
            return;
        }
    }
    AudioClipInstance inst;
    inst.clipId = clipId;
    inst.buffer = buf;
    audioClipInstances_.push_back(inst);
}

void AudioEngine::reprocessAudioClipPitch(int clipId, AudioClipMode mode, float pitchSemitone)
{
    // Resample mode or no pitch shift: clear any pitched buffer -- play from original
    if (mode == AudioClipMode::Resample || pitchSemitone == 0.0f)
    {
        const juce::SpinLock::ScopedLockType lock(audioClipLock_);
        for (auto& inst : audioClipInstances_)
            if (inst.clipId == clipId)
            {
                inst.pitchedBuffer = nullptr;
                inst.cachedMode    = mode;
                inst.cachedPitch   = pitchSemitone;
                inst.active        = false;
                break;
            }
        return;
    }

    // Find original source buffer
    std::shared_ptr<juce::AudioBuffer<float>> srcBuf;
    {
        const juce::SpinLock::ScopedLockType lock(audioClipLock_);
        for (auto& inst : audioClipInstances_)
            if (inst.clipId == clipId) { srcBuf = inst.buffer; break; }
    }
    if (srcBuf == nullptr || srcBuf->getNumSamples() <= 0) return;

    const int    srcLen    = srcBuf->getNumSamples();
    const int    numCh     = srcBuf->getNumChannels();
    const double pitchScale = std::pow(2.0, (double)pitchSemitone / 12.0);
    const bool   highQ     = (mode == AudioClipMode::Elastique);

    // ---- RubberBand offline pitch shift (timeRatio=1.0, pitchScale=pitchRatio) ----
    using RBS = RubberBand::RubberBandStretcher;
    const int rbOptions = RBS::OptionProcessOffline
                        | (highQ ? RBS::OptionPitchHighQuality : RBS::OptionPitchHighSpeed);

    const double processRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    RBS stretcher((size_t)processRate, (size_t)numCh, rbOptions, 1.0, pitchScale);
    stretcher.setExpectedInputDuration((size_t)srcLen);

    // Build array of const pointers for RubberBand
    std::vector<const float*> inPtrs((size_t)numCh);
    for (int ch = 0; ch < numCh; ++ch)
        inPtrs[(size_t)ch] = srcBuf->getReadPointer(ch);

    // Study pass
    stretcher.study(inPtrs.data(), (size_t)srcLen, true);

    // Process pass
    stretcher.process(inPtrs.data(), (size_t)srcLen, true);

    // Retrieve output (may be slightly shorter/longer due to latency)
    const int outLen = stretcher.available();
    auto outBuf = std::make_shared<juce::AudioBuffer<float>>(numCh, juce::jmax(1, outLen));

    if (outLen > 0)
    {
        std::vector<float*> outPtrs((size_t)numCh);
        for (int ch = 0; ch < numCh; ++ch)
            outPtrs[(size_t)ch] = outBuf->getWritePointer(ch);
        stretcher.retrieve(outPtrs.data(), (size_t)outLen);
    }

    // Push result to instance (lock required -- audio thread may be running)
    const juce::SpinLock::ScopedLockType lock(audioClipLock_);
    for (auto& inst : audioClipInstances_)
        if (inst.clipId == clipId)
        {
            inst.pitchedBuffer = outBuf;
            inst.cachedMode    = mode;
            inst.cachedPitch   = pitchSemitone;
            inst.active        = false;   // reset so processSongMode picks up new buffer
            break;
        }
}

void AudioEngine::unloadAudioClip(int clipId)
{
    const juce::SpinLock::ScopedLockType lock(audioClipLock_);
    audioClipInstances_.erase(
        std::remove_if(audioClipInstances_.begin(), audioClipInstances_.end(),
                       [clipId](const AudioClipInstance& i) { return i.clipId == clipId; }),
        audioClipInstances_.end());
}

void AudioEngine::unloadAllAudioClips()
{
    const juce::SpinLock::ScopedLockType lock(audioClipLock_);
    audioClipInstances_.clear();
    audioFileBuffers_.clear();
}

void AudioEngine::resetAudioClipTriggers()
{
    const juce::SpinLock::ScopedTryLockType tryLock(audioClipLock_);
    if (!tryLock.isLocked()) return;
    for (auto& inst : audioClipInstances_)
        inst.active = false;
}

std::shared_ptr<juce::AudioBuffer<float>> AudioEngine::getAudioFileBuffer(const juce::String& path) const
{
    const juce::SpinLock::ScopedTryLockType tryLock(audioClipLock_);
    if (!tryLock.isLocked()) return nullptr;
    auto it = audioFileBuffers_.find(path);
    return (it != audioFileBuffers_.end()) ? it->second : nullptr;
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

// -- CC Real-Time Parameter Control ------------------------------------------

void AudioEngine::setCcChannelVolume(int ch, float normalised01)
{
    if (ch >= 0 && ch < 16)
        ccVol_[(size_t)ch].setTarget(juce::jlimit(0.0f, 1.0f, normalised01));
}

void AudioEngine::setCcChannelPan(int ch, float normalised01)
{
    if (ch >= 0 && ch < 16)
        ccPan_[(size_t)ch].setTarget(juce::jlimit(0.0f, 1.0f, normalised01));
}

float AudioEngine::getCcChannelVolume(int ch) const noexcept
{
    if (ch < 0 || ch >= 16) return 1.0f;
    return ccVol_[(size_t)ch].target.load(std::memory_order_relaxed);
}

float AudioEngine::getCcChannelPan(int ch) const noexcept
{
    if (ch < 0 || ch >= 16) return 0.5f;
    return ccPan_[(size_t)ch].target.load(std::memory_order_relaxed);
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

// ---- M3 -- Active pattern / pitch ----------------------------------------

void AudioEngine::setActivePattern(int patternId)
{
    activePatternId = patternId;
    patternBeatPos  = 0.0;
}

void AudioEngine::setActiveVariation(int idx)
{
    activeVariationIdx_.store(juce::jlimit(0, 3, idx), std::memory_order_relaxed);
}

void AudioEngine::setChannelPattern(int channel, int patternId, int variationIdx)
{
    if (channel < 0 || channel >= 16) return;
    channelPatternOverride_  [channel] = patternId;           // -1 = revert to global
    channelVariationOverride_[channel] = juce::jlimit(0, 3, variationIdx);

    // Rebuild snapshot so the audio thread picks up the change immediately.
    updatePatternSnapshot();
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
    snap.swingAmount = pat->swingAmount;

    // Sync swing + per-step timing to sequencer
    sequencer.setSwingAmount(pat->swingAmount);
    for (int s = 0; s < PlaybackSnapshot::kSteps; ++s)
    {
        // Aggregate per-step timing offset from all channels (use channel 0 as reference,
        // since timing offset is a rhythmic property, not per-channel)
        sequencer.setStepTimingOffset(s, pat->variations[activeVariationIdx_.load(std::memory_order_relaxed)]
                                           .stepParams[0][s].timingOffset);
    }

    const int varIdx = activeVariationIdx_.load(std::memory_order_relaxed);
    for (int ch = 0; ch < PlaybackSnapshot::kCh; ++ch)
    {
        // Live Performance: per-channel pattern override takes priority over global.
        const int chPatId  = channelPatternOverride_[ch];
        const int chVarIdx = (chPatId >= 0) ? channelVariationOverride_[ch] : varIdx;
        const Pattern* chPat = (chPatId >= 0) ? findPatternById(chPatId) : pat;

        snap.channelPatternId   [ch] = chPatId;
        snap.channelVariationIdx[ch] = chVarIdx;

        // Use the resolved per-channel pattern for steps/notes/params
        const Pattern* srcPat = (chPat != nullptr) ? chPat : pat;
        if (srcPat == nullptr)
        {
            snap.channelTypes      [ch] = {};
            snap.synthParams       [ch] = {};
            snap.channelSourceTypes[ch] = {};
            snap.samplerParams     [ch] = {};
            snap.channelVolume     [ch] = 0.8f;
            snap.channelPan        [ch] = 0.0f;
            snap.channelPitch      [ch] = 0.0f;
            snap.channelMixerRouting[ch]= 0;
            snap.pluginSlotEnabled [ch] = false;
            snap.noteSlots[ch].count    = 0;
            for (int s = 0; s < PlaybackSnapshot::kSteps; ++s)
            {
                snap.steps     [ch][s] = false;
                snap.stepParams[ch][s] = {};
            }
            continue;
        }

        for (int s = 0; s < PlaybackSnapshot::kSteps; ++s)
        {
            snap.steps     [ch][s] = srcPat->variations[chVarIdx].steps     [ch][s];
            snap.stepParams[ch][s] = srcPat->variations[chVarIdx].stepParams[ch][s];
        }

        snap.channelTypes      [ch] = srcPat->channelTypes[ch];
        snap.synthParams       [ch] = srcPat->synthParams[ch];
        snap.channelSourceTypes[ch] = srcPat->channelSourceTypes[ch];
        snap.samplerParams     [ch] = srcPat->samplerParams[ch];
        snap.channelVolume     [ch] = srcPat->channelVolume[ch];
        snap.channelPan        [ch] = srcPat->channelPan[ch];
        snap.channelPitch      [ch] = srcPat->channelPitch[ch];
        snap.channelMixerRouting[ch]= srcPat->channelMixerRouting[ch];
        snap.pluginSlotEnabled [ch] = srcPat->pluginSlots[(size_t)ch].enabled;

        auto& slot = snap.noteSlots[ch];
        const auto& src = srcPat->variations[chVarIdx].notes[ch];
        slot.count = (int)juce::jmin((int)src.size(), PlaybackSnapshot::kNotes);
        for (int n = 0; n < slot.count; ++n)
            slot.notes[n] = src[(size_t)n];
    }

    activeSnapshotIdx_.store(nextIdx, std::memory_order_release);
}

void AudioEngine::triggerLaunchpadPad(int padIdx)
{
    if (padIdx < 0 || padIdx >= 64) return;
    auto& player = launchpadPlayers[(size_t)padIdx];
    if (project != nullptr)
    {
        const auto& pad = project->launchpadPads[(size_t)padIdx];
        player.setVolume(pad.volume);
        player.setPitch(pad.pitch);

        const bool loop = (pad.playMode == PadPlayMode::Loop);
        if (loop && player.isPlaying())
        {
            player.stop();
            return;
        }
        player.setLooping(loop);
    }
    player.trigger();
}

void AudioEngine::stopLaunchpadPad(int padIdx)
{
    if (padIdx < 0 || padIdx >= 64) return;
    launchpadPlayers[(size_t)padIdx].stop();
}

void AudioEngine::stopAllLaunchpadPads()
{
    for (auto& lp : launchpadPlayers)
        lp.stop();
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
    for (auto& at : autoTuneProcessors_)
        at.reset();

    // M8 -- send all-notes-off to loaded plugins via pendingMidiAllOff flag
    // (writing directly to instrumentMidiBuffers from the message thread is
    // unreliable -- they are cleared at the start of each audio callback)
    for (int ch = 0; ch < 16; ++ch)
    {
        activePluginNotes[ch].clear();
        pendingPluginAllNotesOff_[ch].store(true, std::memory_order_release);
    }
}

void AudioEngine::clearTransientPlaybackState()
{
    scheduledSampleTriggers_.clear();
    for (auto& pool : sampleVoicePools)
        pool.reset();
    for (auto& synth : polySynths)
        synth.reset();
    for (auto& fx : fxChains)
        fx.reset();
    for (auto& at : autoTuneProcessors_)
        at.reset();
    for (auto& lp : launchpadPlayers)
        lp.reset();
    browserPreviewPlayer.reset();
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

void AudioEngine::dispatchVoiceNote(int ch, int midiPitch, float velocity,
                                    int noteLenSamples, float outputGain, float outputPan,
                                    int mixerTrack,
                                    const SynthParams& sp, ChannelSourceType srcType,
                                    const SamplerParams& samplerParams,
                                    std::shared_ptr<const juce::AudioBuffer<float>> samplerBuf)
{
    // Sampler channels always play through the voice engine (source type is authoritative).
    // Synth channels require sp.enabled (preserves legacy opt-in behaviour).
    const bool voiceEnabled = sp.enabled || (srcType == ChannelSourceType::Sampler);
    if (!voiceEnabled) return;

    const int noteLen = (noteLenSamples > 0)
        ? noteLenSamples
        : juce::jmax(1, (int)(0.25 * sampleRate * 60.0 / bpm.load(std::memory_order_relaxed)));

    const auto noteParams = makeNoteSynthParams(sp, midiPitch, velocity, noteLen);

    if (srcType == ChannelSourceType::Sampler)
    {
        if (samplerBuf)
        {
            polySynths[(size_t)ch].noteOnSampler(midiPitch, velocity, sampleRate,
                                                 noteParams, samplerParams,
                                                 std::move(samplerBuf), noteLen,
                                                 outputGain, outputPan, mixerTrack);
            return;
        }
        // No sampler buffer loaded -- only fall through to raw synth if explicitly enabled
        if (!sp.enabled) return;
    }

    polySynths[(size_t)ch].noteOn(midiPitch, velocity, sampleRate, noteParams, noteLen,
                                  outputGain, outputPan, mixerTrack);
}

void AudioEngine::previewNote(int ch, int midiPitch)
{
    if (ch < 0 || ch >= 16) return;

    // If snapshot is stale (patternId == -1), refresh it now.
    {
        const int idx = activeSnapshotIdx_.load(std::memory_order_acquire);
        if (snapshots_[idx].patternId < 0)
            updatePatternSnapshot();
    }

    const PlaybackSnapshot& snap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];
    const double samplesPerBeat  = sampleRate * 60.0 / bpm.load(std::memory_order_relaxed);
    const int    noteLenSamples  = (int)(0.25 * samplesPerBeat);  // 16th note preview
    const int    mixerTrack      = snap.patternId >= 0 ? juce::jlimit(0, 7, snap.channelMixerRouting[(size_t)ch]) : 0;
    const float  volume          = snap.patternId >= 0 ? snap.channelVolume[(size_t)ch]
                                                       : sampleChannelVolume_[(size_t)ch].load(std::memory_order_relaxed);
    const float  pan             = snap.patternId >= 0 ? snap.channelPan[(size_t)ch]
                                                       : sampleChannelPan_[(size_t)ch].load(std::memory_order_relaxed);
    // M8 -- route to plugin if loaded AND enabled for this pattern (via midiCollector for thread safety)
    if (snap.pluginSlotEnabled[ch] && instrumentPlugins[(size_t)ch] != nullptr)
    {
        const int safePitch = juce::jlimit(0, 127, midiPitch);
        // Encode target channel in MIDI channel (1-based: ch+1) so the audio
        // thread routes it to the correct plugin instead of midiTargetChannel.
        auto noteOn = juce::MidiMessage::noteOn(ch + 1, safePitch, 0.8f);
        noteOn.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        midiCollector.addMessageToQueue(noteOn);

        // Schedule note-off
        auto noteOff = juce::MidiMessage::noteOff(ch + 1, safePitch);
        noteOff.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001
                             + noteLenSamples / sampleRate);
        midiCollector.addMessageToQueue(noteOff);
        return;
    }

    const ChannelSourceType srcType = snap.channelSourceTypes[(size_t)ch];
    const bool hasSynth = snap.patternId >= 0 && snap.synthParams[(size_t)ch].enabled;

    if (hasSynth || srcType == ChannelSourceType::Sampler)
    {
        auto samplerBuf = (srcType == ChannelSourceType::Sampler) ? getSamplerSourceBuffer(ch) : nullptr;
        const SynthParams& snapSp = snap.patternId >= 0 ? snap.synthParams[(size_t)ch] : SynthParams{};
        const SamplerParams& snapSamp = snap.patternId >= 0 ? snap.samplerParams[(size_t)ch] : SamplerParams{};
        dispatchVoiceNote(ch, midiPitch, 0.8f, noteLenSamples, volume, pan, mixerTrack,
                          snapSp, srcType, snapSamp, std::move(samplerBuf));
    }
    else
    {
        auto sourceBuffer = getChannelSourceBufferShared(ch);
        if (sourceBuffer != nullptr)
        {
            triggerSampleVoiceNow(ch, 0, mixerTrack, sourceBuffer.get(), sourceBuffer, volume, pan,
                                  channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) + (float)(midiPitch - 60), 1.0f);
        }
        else
        {
            // No sample loaded -- fall back to built-in synth so clicking piano keys always produces sound
            SynthParams sp;
            sp.enabled = true;
            dispatchVoiceNote(ch, midiPitch, 0.8f, noteLenSamples, volume, pan, mixerTrack,
                              sp, ChannelSourceType::Synth, SamplerParams{}, nullptr);
        }
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

void AudioEngine::stopBrowserPreview()
{
    browserPreviewPlayer.reset();
}

void AudioEngine::previewSynthNote(int ch, int midiPitch, const SynthParams& p)
{
    if (ch < 0 || ch >= 16) return;

    // Refresh stale snapshot (same guard as previewNote).
    {
        const int idx = activeSnapshotIdx_.load(std::memory_order_acquire);
        if (snapshots_[idx].patternId < 0)
            updatePatternSnapshot();
    }

    const double samplesPerBeat = sampleRate * 60.0 / bpm.load(std::memory_order_relaxed);
    const int noteLenSamples = (int)(0.5 * samplesPerBeat);  // 8th note preview

    // If this channel is in Sampler mode, route to sampler playback instead of synth.
    const int snapIdx = activeSnapshotIdx_.load(std::memory_order_acquire);
    const PlaybackSnapshot& snap = snapshots_[snapIdx];
    const ChannelSourceType srcType = snap.channelSourceTypes[(size_t)ch];
    if (srcType == ChannelSourceType::Sampler)
    {
        auto buf = getSamplerSourceBuffer(ch);
        if (buf)
        {
            const auto noteParams = makeNoteSynthParams(p, midiPitch, 0.8f, noteLenSamples);
            polySynths[(size_t)ch].noteOnSampler(midiPitch, 0.8f, sampleRate,
                                                 noteParams, snap.samplerParams[(size_t)ch],
                                                 std::move(buf), noteLenSamples);
            polySynths[(size_t)ch].markLastVoiceAsPreview();
            return;
        }
        // No buffer loaded -- fall through to synth only if synth is also enabled.
        if (!p.enabled) return;
    }

    const auto noteParams = makeNoteSynthParams(p, midiPitch, 0.8f, noteLenSamples);
    polySynths[(size_t)ch].noteOn(midiPitch, 0.8f, sampleRate, noteParams, noteLenSamples);
    polySynths[(size_t)ch].markLastVoiceAsPreview();
}

void AudioEngine::stopEditorPreview(int ch)
{
    if (ch < 0 || ch >= 16) return;
    polySynths[(size_t)ch].killPreviewVoices();
}

bool AudioEngine::isEditorPreviewActive(int ch) const
{
    if (ch < 0 || ch >= 16) return false;
    return polySynths[(size_t)ch].hasActivePreviewVoice();
}

// ---- M5 -- Mixer track controls ------------------------------------------

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
    auto& songSampleCache   = songSampleCaches_  [nextIdx];
    auto& songSamplerCache  = songSamplerCaches_ [nextIdx];
    songSampleCache.clear();
    songSamplerCache.clear();
    std::fill_n(songPlayerClipId, 16, -1);

    const auto& runtime = getRuntimeState();

    for (const auto& pat : runtime.patterns)
    {
        auto& cache        = songSampleCache  [pat.id];
        auto& samplerCache = songSamplerCache [pat.id];

        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            // Drum/raw sample path
            if (!pat.samplePaths[ch].isEmpty())
            {
                SamplePlayer helper;
                helper.prepare(sampleRate, bufferSize);
                helper.loadFile(juce::File(pat.samplePaths[ch]));
                cache[(size_t)ch] = helper.isLoaded() ? helper.takeLoadedBuffer() : nullptr;
            }

            // Sampler Synth source path
            if (pat.channelSourceTypes[ch] == ChannelSourceType::Sampler
                && !pat.samplerParams[ch].samplePath.isEmpty())
            {
                SamplePlayer helper;
                helper.prepare(sampleRate, bufferSize);
                helper.loadFile(juce::File(pat.samplerParams[ch].samplePath));
                samplerCache[(size_t)ch] = helper.isLoaded() ? helper.takeLoadedBuffer() : nullptr;
            }
        }
    }

    // Swap both caches atomically (sampler cache uses same index)
    activeSongCacheIdx_.store(nextIdx, std::memory_order_release);
    activeSongSamplerCacheIdx_.store(nextIdx, std::memory_order_release);
}

void AudioEngine::loadSamplerSource(int channelIndex, const juce::File& file)
{
    if (channelIndex < 0 || channelIndex >= 16) return;

    std::shared_ptr<juce::AudioBuffer<float>> newBuf;
    if (file.existsAsFile())
    {
        SamplePlayer helper;
        helper.prepare(sampleRate, bufferSize);
        helper.loadFile(file);
        if (helper.isLoaded())
            newBuf = helper.takeLoadedBuffer();
    }

    {
        juce::SpinLock::ScopedLockType lock(samplerBufLock_);
        samplerSourceBuffers_[(size_t)channelIndex] = std::move(newBuf);
        samplerSourceFilenames_[(size_t)channelIndex] =
            file.existsAsFile() ? file.getFileNameWithoutExtension() : juce::String();
    }
}

std::shared_ptr<const juce::AudioBuffer<float>> AudioEngine::getSamplerSourceBuffer(int ch) const
{
    if (ch < 0 || ch >= 16) return nullptr;
    juce::SpinLock::ScopedLockType lock(samplerBufLock_);
    return samplerSourceBuffers_[(size_t)ch];
}

const AudioEngine::SongSampleCacheMap& AudioEngine::getSongSamplerCache() const
{
    return songSamplerCaches_[activeSongSamplerCacheIdx_.load(std::memory_order_acquire)];
}

void AudioEngine::triggerChannel(int channelIndex, int step, int offsetInBuffer)
{
    if (channelIndex < 0 || channelIndex >= 16) return;

    // Use snapshot for lock-free access to synth params on audio thread
    const PlaybackSnapshot& trigSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];

    // --- Per-step params (default StepParams if step out of range) ---
    const bool validStep = (step >= 0 && step < trigSnap.stepCount);
    const StepParams& sp = validStep ? trigSnap.stepParams[channelIndex][step] : StepParams{};

    // Probability gate -- skip this trigger based on random chance
    if (sp.probability < 1.0f && stepParamRng_.nextFloat() > sp.probability)
        return;

    // Step-level velocity and pitch
    const float stepVel  = juce::jlimit(0.0f, 1.0f, 0.8f * sp.velocity);
    const int   baseLen  = juce::jmax(1, (int)(0.25 * sampleRate * 60.0 / bpm.load(std::memory_order_relaxed)));
    const int   noteLen  = juce::jmax(1, (int)(baseLen * sp.gate));

    // M8 -- route to VST/AU plugin if one is loaded AND enabled for this pattern
    {
        const PlaybackSnapshot& trigSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];
        const bool trigPluginActive = trigSnap.pluginSlotEnabled[channelIndex]
                                   && instrumentPlugins[(size_t)channelIndex] != nullptr;
        if (trigPluginActive)
        {
            const int midiPitch = juce::jlimit(0, 127,
                60 + (int)std::round(channelBasePitch[(size_t)channelIndex].load(std::memory_order_relaxed))
                   + sp.pitchOffset);
            instrumentMidiBuffers[channelIndex].addEvent(
                juce::MidiMessage::noteOn(1, midiPitch, stepVel), offsetInBuffer);

            // Schedule note-off after gate length
            const double samplesPerBeat = sampleRate * 60.0 / bpm.load(std::memory_order_relaxed);
            const double noteEndBeat = patternBeatPos + (double)offsetInBuffer / samplesPerBeat
                                       + sp.gate * 0.25;
            activePluginNotes[channelIndex].push({ noteEndBeat, midiPitch });
            return;
        }
    }

    // If a synth is enabled on this channel, trigger a synth voice at the
    // pitch-slider note (channelBasePitch semitone offset from C4=60).
    // This lets Drum-mode channels use the synth engine with the step grid.
    // Melodic channels are driven exclusively by NoteEvents (piano roll) -- skip step trigger.
    if (trigSnap.patternId >= 0 && trigSnap.synthParams[(size_t)channelIndex].enabled
        && trigSnap.channelTypes[(size_t)channelIndex] != ChannelType::Melodic)
    {
        const int   midiPitch = juce::jlimit(0, 127,
                                    60 + (int)std::round(channelBasePitch[(size_t)channelIndex].load(std::memory_order_relaxed))
                                       + sp.pitchOffset);

        // Phase 2 -- apply per-step cutoff modulation to a local SynthParams copy
        SynthParams modSP = trigSnap.synthParams[(size_t)channelIndex];
        if (sp.cutoffMod != 0.0f)
            modSP.cutoff = juce::jlimit(40.0f, 20000.0f,
                                        modSP.cutoff * std::pow(2.0f, sp.cutoffMod));

        const auto noteParams = makeNoteSynthParams(modSP, midiPitch, stepVel, noteLen);
        const int mixerTrack  = juce::jlimit(0, 7, trigSnap.channelMixerRouting[(size_t)channelIndex]);

        if (trigSnap.channelSourceTypes[(size_t)channelIndex] == ChannelSourceType::Sampler)
        {
            auto buf = getSamplerSourceBuffer(channelIndex);
            if (buf)
            {
                // Phase 2 -- apply per-step sample start offset
                SamplerParams samplerP = trigSnap.samplerParams[(size_t)channelIndex];
                if (sp.startOffsetFrac > 0.0f && buf->getNumSamples() > 0)
                    samplerP.startOffsetSamples = (int)(sp.startOffsetFrac * (float)buf->getNumSamples());

                polySynths[(size_t)channelIndex].noteOnSampler(
                    midiPitch, stepVel, sampleRate,
                    noteParams, samplerP,
                    std::move(buf), noteLen, 1.0f, 0.0f, mixerTrack);
                return;
            }
            // No sampler buffer loaded -- fall through to raw sample trigger
        }
        else
        {
            polySynths[(size_t)channelIndex].noteOn(
                midiPitch, stepVel, sampleRate,
                noteParams, noteLen, 1.0f, 0.0f, mixerTrack);
            return;
        }
    }

    const float volume = (trigSnap.patternId >= 0) ? trigSnap.channelVolume[(size_t)channelIndex]
                                                   : sampleChannelVolume_[(size_t)channelIndex].load(std::memory_order_relaxed);
    const float pan = (trigSnap.patternId >= 0) ? trigSnap.channelPan[(size_t)channelIndex]
                                                : sampleChannelPan_[(size_t)channelIndex].load(std::memory_order_relaxed);
    // channelBasePitch is the realtime pitch-slider value (semitones).
    // trigSnap.channelPitch is the same value snapshotted into the pattern.
    // Use the snapshot when a pattern is active to avoid doubling.
    const float pitch = ((trigSnap.patternId >= 0)
                          ? trigSnap.channelPitch[(size_t)channelIndex]
                          : channelBasePitch[(size_t)channelIndex].load(std::memory_order_relaxed))
                        + (float)sp.pitchOffset;   // per-step pitch offset
    const int mixerTrack = (trigSnap.patternId >= 0) ? juce::jlimit(0, 7, trigSnap.channelMixerRouting[(size_t)channelIndex]) : 0;
    auto sourceBuffer = getChannelSourceBufferShared(channelIndex);

    // Phase 2 -- per-step sample start offset for raw sample triggers
    double rawStartOffSamples = 0.0;
    if (sp.startOffsetFrac > 0.0f && sourceBuffer && sourceBuffer->getNumSamples() > 0)
        rawStartOffSamples = sp.startOffsetFrac * (double)sourceBuffer->getNumSamples();

    scheduleSampleTrigger(channelIndex, offsetInBuffer, mixerTrack, sourceBuffer.get(), sourceBuffer,
                          volume * sp.velocity, pan, pitch, 1.0f, rawStartOffSamples);
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
                                        int mixerTrack,
                                        const juce::AudioBuffer<float>* sourceBuffer,
                                        std::shared_ptr<const juce::AudioBuffer<float>> ownedSourceBuffer,
                                        float volume,
                                        float pan,
                                        float pitchSemitones,
                                        float bpmRatio,
                                        double startOffsetSamples)
{
    if (channelIndex < 0 || channelIndex >= 16 || sourceBuffer == nullptr)
        return;

    ScheduledSampleTrigger trigger;
    trigger.channel = channelIndex;
    trigger.mixerTrack = juce::jlimit(0, 7, mixerTrack);
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
    trigger.startOffsetSamples = juce::jmax(0.0, startOffsetSamples);
    scheduledSampleTriggers_.push(trigger);
}

void AudioEngine::triggerSampleVoiceNow(int channelIndex,
                                        int offsetInBuffer,
                                        int mixerTrack,
                                        const juce::AudioBuffer<float>* sourceBuffer,
                                        std::shared_ptr<const juce::AudioBuffer<float>> ownedSourceBuffer,
                                        float volume,
                                        float pan,
                                        float pitchSemitones,
                                        float bpmRatio,
                                        double startOffsetSamples)
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
    voice.setMixerTrack(mixerTrack);
    voice.setPitch(pitchSemitones);
    voice.setAttack(juce::jmax(0.5f, sampleChannelAttackMs_[(size_t)channelIndex].load(std::memory_order_relaxed)));
    voice.setRelease(juce::jmax(2.0f, sampleChannelReleaseMs_[(size_t)channelIndex].load(std::memory_order_relaxed)));
    voice.setBpmRatio(bpmRatio);
    const bool anySoloed = std::any_of(std::begin(channelSoloed), std::end(channelSoloed),
                                       [](const std::atomic<bool>& soloed) { return soloed.load(std::memory_order_relaxed); });
    const bool silenced = channelMuted[(size_t)channelIndex].load(std::memory_order_relaxed)
                       || (anySoloed && !channelSoloed[(size_t)channelIndex].load(std::memory_order_relaxed));
    voice.setMuted(silenced);
    if (startOffsetSamples > 0.0)
        voice.setPlayStartPosition(startOffsetSamples);
    voice.triggerAt(juce::jmax(0, offsetInBuffer));
}

void AudioEngine::dispatchScheduledSampleTriggers()
{
    for (int i = 0; i < scheduledSampleTriggers_.count; ++i)
    {
        const auto& trigger = scheduledSampleTriggers_.triggers[(size_t)i];
        triggerSampleVoiceNow(trigger.channel, trigger.offsetInBuffer, trigger.mixerTrack, trigger.sourceBuffer,
                              trigger.ownedSourceBuffer, trigger.volume, trigger.pan, trigger.pitchSemitones,
                              trigger.bpmRatio, trigger.startOffsetSamples);
    }

    scheduledSampleTriggers_.clear();
}

void AudioEngine::setStepPattern(int channelIndex, int stepIndex, bool active)
{
    sequencer.setStep(channelIndex, stepIndex, active);
}

// ---- M12 -- MIDI input -------------------------------------------------------

juce::Array<juce::MidiDeviceInfo> AudioEngine::getMidiInputDevices() const
{
    return juce::MidiInput::getAvailableDevices();
}

void AudioEngine::openMidiDevice(const juce::String& deviceId)
{
    closeMidiDevice();
    if (deviceId.isEmpty())
        return;

    const auto dmDevices = juce::MidiInput::getAvailableDevices();

    juce::String resolvedId = deviceId;
    bool idFound = false;
    for (const auto& d : dmDevices)
    {
        if (d.identifier == resolvedId)
        {
            idFound = true;
            break;
        }
    }

    if (!idFound)
    {
        // Fallback: resolve by name in case identifier format differs between APIs.
        for (const auto& d : dmDevices)
        {
            if (d.name == deviceId)
            {
                resolvedId = d.identifier;
                idFound = true;
                break;
            }
        }
    }

    if (!idFound)
    {
        DBG("MIDI open failed: device not found for id/name = " << deviceId);
        return;
    }

    // Register as "all enabled MIDI inputs" listener to avoid identifier-specific
    // routing misses across virtual/aggregate ports.
    deviceManager.addMidiInputDeviceCallback({}, this);
    midiInputCallbackRegistered_ = true;
    deviceManager.setMidiInputDeviceEnabled(resolvedId, true);
    openMidiDeviceId_ = resolvedId;
    DBG("MIDI opened: " << resolvedId);
}

void AudioEngine::closeMidiDevice()
{
    if (midiInputCallbackRegistered_)
    {
        deviceManager.removeMidiInputDeviceCallback({}, this);
        midiInputCallbackRegistered_ = false;
    }

    if (openMidiDeviceId_.isNotEmpty())
    {
        deviceManager.setMidiInputDeviceEnabled(openMidiDeviceId_, false);
        DBG("MIDI closed: " << openMidiDeviceId_);
        openMidiDeviceId_.clear();
    }
}

void AudioEngine::setMidiTargetChannel(int ch)
{
    midiTargetChannel = juce::jlimit(0, 15, ch);
    midiRouter_.setDefaultInstrumentChannel(midiTargetChannel);
}

juce::String AudioEngine::getOpenMidiDeviceId() const
{
    return openMidiDeviceId_;
}

void AudioEngine::getMidiHeldNotesForChannel(int ch, std::array<bool, 128>& out) const
{
    out.fill(false);
    if (ch < 0 || ch >= 16)
        return;

    for (int note = 0; note < 128; ++note)
        out[(size_t)note] = midiHeldNotes_[(size_t)ch][(size_t)note].load(std::memory_order_relaxed) != 0;
}

// ---- Loop MIDI Recording -----------------------------------------------

void AudioEngine::setLoopMarkers(double startBeat, double endBeat)
{
    loopMarkers_.startBeat = juce::jmax(0.0, startBeat);
    loopMarkers_.endBeat   = juce::jmax(loopMarkers_.startBeat + 0.0625, endBeat);
}

void AudioEngine::enableLoopRecord(bool shouldEnable)
{
    loopRecordEnabled_ = shouldEnable;
    if (!shouldEnable)
        for (auto& row : liveNotes_)
            for (auto& n : row)
                n.active = false;
}

void AudioEngine::injectFirstLoopNotes(int ch) noexcept
{
    if (ch < 0 || ch >= 16) return;
    // Read currently-held pitches + velocities (stored in midiHeldNotes_ as 1-127)
    pendingInjection_.channel = ch;
    for (int i = 0; i < 128; ++i)
        pendingInjection_.velocities[i] = midiHeldNotes_[(size_t)ch][(size_t)i].load(std::memory_order_relaxed);
    hasPendingInjection_.store(true, std::memory_order_release);
}

bool AudioEngine::drainCommittedNote(CommittedNote& out) noexcept
{
    int s1, bs1, s2, bs2;
    commitFifo_.prepareToRead(1, s1, bs1, s2, bs2);
    if (bs1 == 0) return false;
    out = commitQueue_[(size_t)s1];
    commitFifo_.finishedRead(bs1);
    return true;
}

void AudioEngine::commitNote(int ch, int pitch, double startBeat, float lengthBeats, float velocity)
{
    int s1, bs1, s2, bs2;
    commitFifo_.prepareToWrite(1, s1, bs1, s2, bs2);
    if (bs1 == 0) return;   // queue full -- drop rather than block
    auto& slot            = commitQueue_[(size_t)s1];
    slot.channel          = ch;
    slot.variation        = activeVariationIdx_.load(std::memory_order_relaxed);
    slot.note.pitch       = pitch;
    slot.note.startBeat   = (float)startBeat;
    slot.note.lengthBeats = juce::jmax(0.0625f, lengthBeats);
    slot.note.velocity    = velocity;
    commitFifo_.finishedWrite(bs1);
}

void AudioEngine::processLoopRecordEvent(const juce::MidiMessage& msg, int ch, double beatPos)
{
    const double loopLen = loopMarkers_.endBeat - loopMarkers_.startBeat;
    if (loopLen <= 0.0) return;

    // Position relative to loop start, wrapped within loop
    double relBeat = std::fmod(beatPos - loopMarkers_.startBeat, loopLen);
    if (relBeat < 0.0) relBeat += loopLen;

    const int pitch = juce::jlimit(0, 127, msg.getNoteNumber());

    if (msg.isNoteOn())
    {
        liveNotes_[ch][pitch] = { true, relBeat, msg.getFloatVelocity() };
    }
    else if (msg.isNoteOff())
    {
        auto& ln = liveNotes_[ch][pitch];
        if (!ln.active) return;

        double len = relBeat - ln.startBeat;
        if (len <= 0.0) len += loopLen;   // note crossed loop boundary while held

        commitNote(ch, pitch,
                   ln.startBeat + loopMarkers_.startBeat,
                   (float)juce::jmax(0.0625, len),
                   ln.velocity);
        ln.active = false;
    }
}

void AudioEngine::commitHangingNotes()
{
    const double loopLen = loopMarkers_.endBeat - loopMarkers_.startBeat;
    for (int ch = 0; ch < 16; ++ch)
    {
        for (int pitch = 0; pitch < 128; ++pitch)
        {
            auto& ln = liveNotes_[ch][pitch];
            if (!ln.active) continue;

            // Commit note extending to loop end
            const double len = juce::jmax(0.0625, loopLen - ln.startBeat);
            commitNote(ch, pitch,
                       ln.startBeat + loopMarkers_.startBeat,
                       (float)len,
                       ln.velocity);
            ln.active = false;

            // Stop the sound at loop boundary
            polySynths[(size_t)ch].noteOff(pitch);
            if (instrumentPlugins[(size_t)ch] != nullptr)
                pendingPluginAllNotesOff_[ch].store(true, std::memory_order_release);
        }
    }

    // Notify message thread that a loop wrap occurred
    juce::MessageManager::callAsync([this] { if (onLoopWrap) onLoopWrap(); });
}

// -- Live Performance Loop Recording -----------------------------------------

// -- Live Loop Engine -- public API wrappers ------------------------------------
void AudioEngine::liveLoopArm(int ch, double loopBeats) noexcept { liveLoopEngine_.arm(ch, loopBeats); }
void AudioEngine::liveLoopArmFree(int ch) noexcept              { liveLoopEngine_.armFree(ch); }
void AudioEngine::liveLoopStop(int ch) noexcept        { liveLoopEngine_.stop(ch); }
void AudioEngine::liveLoopResetAll() noexcept          { liveLoopEngine_.resetAll(); }
void AudioEngine::liveLoopOverdub(int ch) noexcept     { liveLoopEngine_.overdub(ch); }
void AudioEngine::liveLoopUndo(int ch) noexcept        { liveLoopEngine_.undo(ch); }
void AudioEngine::liveLoopHalfLoop(int ch) noexcept    { liveLoopEngine_.halfLoop(ch); }
void AudioEngine::liveLoopDoubleLoop(int ch) noexcept  { liveLoopEngine_.doubleLoop(ch); }
void AudioEngine::liveLoopLaunchAll(double b) noexcept { liveLoopEngine_.launchAll(b); }
void AudioEngine::liveLoopSetQuantize(double s) noexcept { liveLoopEngine_.setQuantize(s); }
void AudioEngine::liveLoopSetMute(int ch, bool m) noexcept    { liveLoopEngine_.setMute(ch, m); }
bool AudioEngine::liveLoopGetMute(int ch) const noexcept      { return liveLoopEngine_.getMute(ch); }
void AudioEngine::liveLoopSetVolume(int ch, float v) noexcept  { liveLoopEngine_.setVolume(ch, v); }
float AudioEngine::liveLoopGetVolume(int ch) const noexcept    { return liveLoopEngine_.getVolume(ch); }
LiveLoopEngine::State AudioEngine::liveLoopGetState(int ch) const noexcept { return liveLoopEngine_.getState(ch); }
double AudioEngine::liveLoopGetChannelLength(int ch) const noexcept { return liveLoopEngine_.getChannelLoopLength(ch); }
double AudioEngine::liveLoopGetBeatsTillRecord(int ch) const noexcept { return liveLoopEngine_.getBeatsTillRecord(ch); }
double AudioEngine::liveLoopGetGlobalBeat() const noexcept            { return liveLoopEngine_.getGlobalBeat(); }
void   AudioEngine::liveLoopSetCountInBars(int bars) noexcept         { liveLoopEngine_.setCountInBars(bars); }
int    AudioEngine::liveLoopGetCountInBars() const noexcept           { return liveLoopEngine_.getCountInBars(); }
void   AudioEngine::liveLoopSetSnapForward(bool fwd) noexcept         { liveLoopEngine_.setSnapForward(fwd); }
bool   AudioEngine::liveLoopGetSnapForward() const noexcept           { return liveLoopEngine_.getSnapForward(); }
int    AudioEngine::liveLoopGetNotesForDisplay(int ch, LiveLoopEngine::NoteDisplayItem* out, int max) const noexcept
       { return liveLoopEngine_.getNotesForDisplay(ch, out, max); }
double AudioEngine::liveLoopGetPhase(int ch) const noexcept         { return liveLoopEngine_.getLoopPhase(ch); }

juce::String AudioEngine::getLiveChannelInstrumentName(int ch) const
{
    if (ch < 0 || ch >= 16) return {};

    // Plugin?
    const PlaybackSnapshot& snap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];
    if (snap.pluginSlotEnabled[(size_t)ch] && instrumentPlugins[(size_t)ch] != nullptr)
    {
        const auto desc = instrumentPlugins[(size_t)ch]->getPluginDescription();
        return desc.name.isEmpty() ? "Plugin" : desc.name;
    }

    // Sampler source with stored filename?
    {
        juce::SpinLock::ScopedLockType lock(samplerBufLock_);
        if (samplerSourceBuffers_[(size_t)ch] != nullptr)
            return samplerSourceFilenames_[(size_t)ch].isEmpty()
                   ? "Sample" : samplerSourceFilenames_[(size_t)ch];
    }

    // Snapshot sampler?
    if (snap.patternId >= 0 && snap.channelSourceTypes[(size_t)ch] == ChannelSourceType::Sampler)
        return "Sampler";

    // Synth?
    if (snap.patternId >= 0 && snap.synthParams[(size_t)ch].enabled)
        return "Synth";

    // Channel source buffer (legacy drag-drop samples)?
    if (getChannelSourceBufferShared(ch) != nullptr)
        return "Sample";

    return {};  // nothing assigned
}

// -- Metronome -----------------------------------------------------------------
void AudioEngine::setMetronomeEnabled(bool on) noexcept { metronomeEnabled_.store(on); }
void AudioEngine::setMetronomeVolume(float v) noexcept  { metronomeGain_.store(juce::jlimit(0.0f, 1.0f, v)); }
bool AudioEngine::getMetronomeEnabled() const noexcept  { return metronomeEnabled_.load(); }

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg)
{
    DBG("MIDI IN raw: ch=" << msg.getChannel() << " note=" << msg.getNoteNumber()
        << " on=" << (msg.isNoteOn() ? 1 : 0) << " off=" << (msg.isNoteOff() ? 1 : 0));

    // Called on the MIDI thread -- push into thread-safe collector.
    // Normalize external hardware input to MIDI channel 1 so live keyboards
    // always follow midiTargetChannel. Internal engine-generated MIDI events
    // still use explicit channel routing (ch+1) when queued directly.
    auto normalized = msg;
    const int incomingChannel = normalized.getChannel();
    if (incomingChannel >= 1 && incomingChannel <= 16)
        normalized.setChannel(1);

    midiCollector.addMessageToQueue(normalized);
}

// ---- Audio callbacks ---------------------------------------------------

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    juce::AudioBuffer<float> buffer(outputChannelData, numOutputChannels, numSamples);
    buffer.clear();
    scheduledSampleTriggers_.clear();

    // --- Recording: push input into lock-free FIFO ---
    if (recorder_.isRecording() && inputChannelData != nullptr && numInputChannels > 0)
        recorder_.writeBlock(inputChannelData, numInputChannels, numSamples);

    // --- Input monitoring: mix input directly to output ---
    // Mono input -> both channels; stereo input -> L to L, R to R
    if (inputMonitoring_.load(std::memory_order_relaxed)
        && inputChannelData != nullptr && numInputChannels > 0)
    {
        buffer.addFrom(0, 0, inputChannelData[0], numSamples);
        if (numInputChannels >= 2 && numOutputChannels >= 2)
            buffer.addFrom(1, 0, inputChannelData[1], numSamples);
        else if (numOutputChannels >= 2)
            buffer.addFrom(1, 0, inputChannelData[0], numSamples);  // mono -> both
    }

    // --- Input level metering (peak detection for UI) ---
    if (inputChannelData != nullptr && numInputChannels > 0)
    {
        float peakL = 0.0f, peakR = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float absL = std::abs(inputChannelData[0][i]);
            if (absL > peakL) peakL = absL;
        }
        if (numInputChannels > 1)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float absR = std::abs(inputChannelData[1][i]);
                if (absR > peakR) peakR = absR;
            }
        }
        else
        {
            peakR = peakL;
        }
        inputLevelL_.store(peakL, std::memory_order_relaxed);
        inputLevelR_.store(peakR, std::memory_order_relaxed);
    }

    // M8 -- clear per-channel plugin MIDI buffers at the start of each block
    for (auto& mb : instrumentMidiBuffers)
        mb.clear();

    // LiveLoopEngine: always advance (transport-independent)
    // Save block-start beat BEFORE processBlock advances globalBeat_, so we can
    // compute a per-event accurate beat position for each incoming MIDI message.
    const double liveBlockStartBeat = liveLoopEngine_.getGlobalBeat();
    {
        const double bpmVal = bpm.load(std::memory_order_relaxed);
        if (bpmVal > 0.0)
            liveLoopEngine_.processBlock(numSamples, bpmVal, sampleRate);
    }

    // Flush pending all-notes-off from message thread (set by allSynthNotesOff)
    for (int ch = 0; ch < 16; ++ch)
    {
        if (pendingPluginAllNotesOff_[ch].exchange(false, std::memory_order_acq_rel))
        {
            instrumentMidiBuffers[ch].addEvent(juce::MidiMessage::allNotesOff(1), 0);
            instrumentMidiBuffers[ch].addEvent(juce::MidiMessage::allSoundOff(1), 0);
        }
    }

    // M12 -- process incoming MIDI messages
    {
        incomingMidiBuffer_.clear();
        midiCollector.removeNextBlockOfMessages(incomingMidiBuffer_, numSamples);

        // First-note injection: seed liveNotes_ with notes held at recording trigger time
        if (hasPendingInjection_.load(std::memory_order_acquire))
        {
            hasPendingInjection_.store(false, std::memory_order_relaxed);
            if (loopRecordEnabled_)
            {
                const int injCh = pendingInjection_.channel;
                if (injCh >= 0 && injCh < 16)
                {
                    for (int pitch = 0; pitch < 128; ++pitch)
                    {
                        const uint8_t vel = pendingInjection_.velocities[pitch];
                        if (vel > 0)
                            liveNotes_[injCh][pitch] = { true, 0.0,
                                                         vel / 127.0f };
                    }
                }
            }
        }

        const int defaultCh = juce::jlimit(0, 15, midiTargetChannel);

        for (const auto meta : incomingMidiBuffer_)
        {
            const auto msg = meta.getMessage();

            // -- MIDI channel routing -----------------------------------------
            // Messages on MIDI channels 2-16 are engine-generated (previewNote,
            // per-channel playback). They bypass MidiRouter and use the explicit
            // channel number directly.
            // Messages on MIDI channel 1 are from the external keyboard/controller
            // and go through MidiRouter for classification.
            const int midiChan = msg.getChannel();  // 1-based
            const bool isExternalMidi = (midiChan == 1);

            // -- CC handling (external MIDI only) ----------------------------
            if (isExternalMidi && msg.isController())
            {
                CcEvent cc;
                cc.ccNumber   = (uint8_t)msg.getControllerNumber();
                cc.value      = (uint8_t)msg.getControllerValue();
                cc.dawChannel = defaultCh;
                midiRouter_.enqueueCc(cc);
                continue;   // CC does not go to instrument or loop recorder
            }

            // -- Determine target DAW channel + routing type ------------------
            int ch = defaultCh;
            bool isClipTrigger = false;

            if (!isExternalMidi)
            {
                // Engine-generated: explicit channel (MIDI ch 2 -> DAW ch 1, etc.)
                ch = juce::jlimit(0, 15, midiChan - 1);
            }
            else if (msg.isNoteOn() || msg.isNoteOff())
            {
                const auto route = midiRouter_.classifyNote(msg.getNoteNumber());
                if (route.type == MidiRoutingType::ClipTrigger)
                {
                    // Capture pad event for message thread (ClipLauncher etc.)
                    ClipTriggerEvent ev;
                    ev.note     = (uint8_t)msg.getNoteNumber();
                    ev.velocity = (uint8_t)msg.getVelocity();
                    ev.padIndex = route.padIndex;
                    midiRouter_.enqueueClipTrigger(ev);
                    isClipTrigger = true;
                }
                else
                {
                    ch = route.dawChannel;
                }
            }

            if (msg.isNoteOn())
            {
                // ClipTrigger notes don't play instruments -- already queued above
                if (isClipTrigger) continue;

                const int   pitch = msg.getNoteNumber();
                const float vel   = msg.getFloatVelocity();
                midiHeldNotes_[(size_t)ch][(size_t)juce::jlimit(0, 127, pitch)].store(
                    (uint8_t)juce::jlimit(1, 127, (int)msg.getVelocity()), std::memory_order_relaxed);

                // M8 -- route to VST/AU plugin if one is loaded AND enabled for current pattern
                const PlaybackSnapshot& midiSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];
                const bool midiPluginActive = midiSnap.pluginSlotEnabled[ch]
                                           && instrumentPlugins[(size_t)ch] != nullptr;
                if (midiPluginActive)
                {
                    instrumentMidiBuffers[ch].addEvent(
                        juce::MidiMessage::noteOn(1, pitch, vel),
                        meta.samplePosition);
                }
                else
                {
                    const int    midiMixer = midiSnap.patternId >= 0 ? juce::jlimit(0, 7, midiSnap.channelMixerRouting[(size_t)ch]) : 0;
                    const float  midiVol   = midiSnap.patternId >= 0 ? midiSnap.channelVolume[(size_t)ch]
                                                                      : sampleChannelVolume_[(size_t)ch].load(std::memory_order_relaxed);
                    const float  midiPan   = midiSnap.patternId >= 0 ? midiSnap.channelPan[(size_t)ch]
                                                                      : sampleChannelPan_[(size_t)ch].load(std::memory_order_relaxed);
                    const ChannelSourceType srcType = midiSnap.channelSourceTypes[(size_t)ch];
                    const bool hasSynth  = midiSnap.patternId >= 0 && midiSnap.synthParams[(size_t)ch].enabled;

                    if (hasSynth || srcType == ChannelSourceType::Sampler)
                    {
                        auto samplerBuf2 = (srcType == ChannelSourceType::Sampler) ? getSamplerSourceBuffer(ch) : nullptr;
                        const SynthParams& midiSp   = midiSnap.patternId >= 0 ? midiSnap.synthParams[(size_t)ch]   : SynthParams{};
                        const SamplerParams& midiSamp = midiSnap.patternId >= 0 ? midiSnap.samplerParams[(size_t)ch] : SamplerParams{};
                        dispatchVoiceNote(ch, pitch, vel, 0, midiVol, midiPan, midiMixer,
                                          midiSp, srcType, midiSamp, std::move(samplerBuf2));
                    }
                    else
                    {
                        auto sourceBuffer = getChannelSourceBufferShared(ch);
                        if (sourceBuffer != nullptr)
                        {
                            scheduleSampleTrigger(ch, meta.samplePosition, midiMixer,
                                                  sourceBuffer.get(), sourceBuffer,
                                                  midiVol, midiPan,
                                                  channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) + (float)(pitch - 60), 1.0f);
                        }
                        else
                        {
                            // No sample loaded -- fall back to built-in synth for MIDI input feedback
                            SynthParams sp;
                            sp.enabled = true;
                            dispatchVoiceNote(ch, pitch, vel, 0, midiVol, midiPan, midiMixer,
                                              sp, ChannelSourceType::Synth, SamplerParams{}, nullptr);
                        }
                    }
                }
            }
            else if (msg.isNoteOff())
            {
                // ClipTrigger note-off already enqueued above; no instrument to notify
                if (isClipTrigger) continue;

                const int pitch = juce::jlimit(0, 127, msg.getNoteNumber());
                midiHeldNotes_[(size_t)ch][(size_t)pitch].store(0, std::memory_order_relaxed);

                // M8 -- route note-off to plugin if loaded AND enabled for current pattern
                const PlaybackSnapshot& offSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];
                const bool offPluginActive = offSnap.pluginSlotEnabled[ch]
                                          && instrumentPlugins[(size_t)ch] != nullptr;
                if (offPluginActive)
                {
                    instrumentMidiBuffers[ch].addEvent(
                        juce::MidiMessage::noteOff(1, pitch),
                        meta.samplePosition);
                }
                else
                {
                    polySynths[(size_t)ch].noteOff(pitch);
                }
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                for (auto& n : midiHeldNotes_[(size_t)ch])
                    n.store(0, std::memory_order_relaxed);

                const PlaybackSnapshot& allSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];
                if (allSnap.pluginSlotEnabled[ch] && instrumentPlugins[(size_t)ch] != nullptr)
                    instrumentMidiBuffers[ch].addEvent(
                        juce::MidiMessage::allNotesOff(1), 0);
                else
                    polySynths[(size_t)ch].allNotesOff();
            }

            // Loop recording: capture note on/off with beat timestamp
            if (loopRecordEnabled_ && sequencer.isPlaying() && playMode == PlayMode::Pattern
                && (msg.isNoteOn() || msg.isNoteOff()))
            {
                const double beatsPerSample = bpm.load(std::memory_order_relaxed) / (60.0 * sampleRate);
                const double beatPos = patternBeatPos + meta.samplePosition * beatsPerSample;
                processLoopRecordEvent(msg, ch, beatPos);
            }

            // Live performance: route MIDI events to LiveLoopEngine.
            // Compute the precise beat position of this event using its sample offset
            // within the buffer. This ensures notes near the loop boundary are recorded
            // at the correct phase, not shifted to the post-wrap position.
            if (msg.isNoteOn() || msg.isNoteOff())
            {
                const double bpmNow = bpm.load(std::memory_order_relaxed);
                const double eventBeat = liveBlockStartBeat
                    + meta.samplePosition * (bpmNow > 0.0 ? bpmNow / (60.0 * sampleRate) : 0.0);
                liveLoopEngine_.processMidiEvent(msg, ch, eventBeat);
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

    // Launchpad one-shot players -- render directly into output (bypass mixer)
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

    // M15 -- browser preview player (bypass mixer, always audible)
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

    // M12 -- reset MIDI collector to current sample rate
    midiCollector.reset(sampleRate);

    // M13/M14 -- prepare synths and FX chains
    for (auto& s : polySynths) s.prepare(sampleRate, bufferSize);
    for (auto& fx : fxChains)  fx.prepare(sampleRate, bufferSize);
    for (auto& at : autoTuneProcessors_) at.prepare(sampleRate, bufferSize);

    // Launchpad players
    for (auto& lp : launchpadPlayers) lp.prepare(sampleRate, bufferSize);

    // M15 -- browser preview player
    browserPreviewPlayer.prepare(sampleRate, bufferSize);

    incomingMidiBuffer_.ensureSize(2048);
    for (auto& mb : instrumentMidiBuffers)
        mb.ensureSize(2048);

    // M5 -- allocate mixer staging buffers
    stagingBuf.setSize(2, bufferSize);
    for (auto& tb : mixerTrackBufs)
        tb.setSize(2, bufferSize);

    // Dynamic EQ -- prepare all processors
    for (auto& deq : trackDynEQs_) deq.prepare(sampleRate, bufferSize);
    masterDynEQ_.prepare(sampleRate, bufferSize);
    resetMixProcessingState();

    // M8 -- re-prepare any already-loaded instrument plugins
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
    for (auto& at : autoTuneProcessors_) at.reset();
    for (auto& lp : launchpadPlayers)  lp.reset();
    resetMixProcessingState();

    // Keep pluginLock hold-time short to minimize audio-thread try-lock misses.
    std::array<std::unique_ptr<juce::AudioPluginInstance>, 16> pluginsToRelease;
    {
        juce::ScopedLock sl(pluginLock);
        for (int ch = 0; ch < 16; ++ch)
            pluginsToRelease[(size_t)ch] = std::move(instrumentPlugins[(size_t)ch]);
    }
    for (auto& plugin : pluginsToRelease)
        if (plugin != nullptr)
            plugin->releaseResources();
}

// ---- M8 -- VST/AU Instrument Plugin management --------------------------

void AudioEngine::loadPlugin(int ch, const juce::PluginDescription& desc,
                              juce::String& errorMsg)
{
    if (ch < 0 || ch >= 16) return;

    // Create the instance on the message thread (may take a moment -- that's OK)
    auto instance = PluginManager::getInstance().createPlugin(desc, sampleRate,
                                                               bufferSize, errorMsg);
    if (instance == nullptr) return;

    // Configure I/O: 0 audio inputs, 2 audio outputs (stereo instrument)
    instance->setPlayConfigDetails(0, 2, sampleRate, bufferSize);
    instance->prepareToPlay(sampleRate, bufferSize);

    std::unique_ptr<juce::AudioPluginInstance> oldPlugin;
    {
        juce::ScopedLock sl(pluginLock);
        oldPlugin = std::move(instrumentPlugins[(size_t)ch]);
        instrumentPlugins[(size_t)ch] = std::move(instance);
        activePluginNotes[ch].clear();
    }
    if (oldPlugin != nullptr)
        oldPlugin->releaseResources();
}

void AudioEngine::unloadPlugin(int ch)
{
    if (ch < 0 || ch >= 16) return;
    std::unique_ptr<juce::AudioPluginInstance> pluginToRelease;
    {
        juce::ScopedLock sl(pluginLock);
        pluginToRelease = std::move(instrumentPlugins[(size_t)ch]);
        activePluginNotes[ch].clear();
    }
    if (pluginToRelease != nullptr)
        pluginToRelease->releaseResources();
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

void AudioEngine::savePluginStatesToSlots(std::array<PluginSlot, 16>& slots)
{
    for (int ch = 0; ch < 16; ++ch)
    {
        auto& slot = slots[(size_t)ch];
        if (instrumentPlugins[(size_t)ch] != nullptr)
        {
            slot.enabled = true;
            slot.pluginId = instrumentPlugins[(size_t)ch]->getPluginDescription()
                                .createIdentifierString();
            juce::MemoryBlock state;
            instrumentPlugins[(size_t)ch]->getStateInformation(state);
            slot.pluginStateBase64 = state.toBase64Encoding();
        }
        else
        {
            slot = PluginSlot{};  // clear
        }
    }
}

void AudioEngine::restorePluginsFromSlots(const std::array<PluginSlot, 16>& slots)
{
    for (int ch = 0; ch < 16; ++ch)
    {
        const auto& slot = slots[(size_t)ch];
        const bool currentHasPlugin = (instrumentPlugins[(size_t)ch] != nullptr);

        if (!slot.enabled || slot.pluginId.isEmpty())
        {
            // New pattern has no plugin on this channel -- unload if present
            if (currentHasPlugin)
                unloadPlugin(ch);
            continue;
        }

        // Check if same plugin is already loaded
        if (currentHasPlugin)
        {
            const auto currentId = instrumentPlugins[(size_t)ch]->getPluginDescription()
                                       .createIdentifierString();
            if (currentId == slot.pluginId)
            {
                // Same plugin -- just restore state
                if (slot.pluginStateBase64.isNotEmpty())
                {
                    juce::MemoryBlock stateData;
                    stateData.fromBase64Encoding(slot.pluginStateBase64);
                    instrumentPlugins[(size_t)ch]->setStateInformation(
                        stateData.getData(), (int)stateData.getSize());
                }
                continue;
            }
            // Different plugin -- unload first
            unloadPlugin(ch);
        }

        // Load the plugin from known list
        const auto& types = PluginManager::getInstance().getKnownPlugins().getTypes();
        for (const auto& desc : types)
        {
            if (desc.createIdentifierString() == slot.pluginId)
            {
                juce::String err;
                loadPlugin(ch, desc, err);
                if (err.isEmpty() && slot.pluginStateBase64.isNotEmpty())
                {
                    juce::MemoryBlock stateData;
                    stateData.fromBase64Encoding(slot.pluginStateBase64);
                    if (instrumentPlugins[(size_t)ch] != nullptr)
                        instrumentPlugins[(size_t)ch]->setStateInformation(
                            stateData.getData(), (int)stateData.getSize());
                }
                break;
            }
        }
    }
}

void AudioEngine::ensureSongPluginsLoaded()
{
    if (project == nullptr) return;

    // Build a merged set of plugin slots from all patterns referenced by playlist clips
    std::array<PluginSlot, 16> needed {};

    for (const auto& clip : project->playlistClips)
    {
        const Pattern* pat = nullptr;
        for (const auto& p : project->patterns)
            if (p.id == clip.patternId) { pat = &p; break; }
        if (pat == nullptr) continue;

        for (int ch = 0; ch < 16; ++ch)
        {
            // First pattern with a plugin on this channel wins
            if (!needed[(size_t)ch].enabled && pat->pluginSlots[(size_t)ch].enabled)
                needed[(size_t)ch] = pat->pluginSlots[(size_t)ch];
        }
    }

    // Also include the currently active pattern's plugins (so pattern mode still works)
    for (const auto& p : project->patterns)
    {
        if (p.id == project->activePatternId)
        {
            for (int ch = 0; ch < 16; ++ch)
                if (!needed[(size_t)ch].enabled && p.pluginSlots[(size_t)ch].enabled)
                    needed[(size_t)ch] = p.pluginSlots[(size_t)ch];
            break;
        }
    }

    restorePluginsFromSlots(needed);
}

// ---- Render helpers ----------------------------------------------------

void AudioEngine::processPatternMode(juce::AudioBuffer<float>& buffer,
                                     int numSamples,
                                     int /*numOutputChannels*/)
{
    sequencer.processBlock(numSamples);

    // M3 -- trigger NoteEvents for Melodic channels (only while sequencer is playing)
    // Use double-buffered snapshot for lock-free pattern data access on audio thread.
    const PlaybackSnapshot& snap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];

    if (sequencer.isPlaying() && snap.patternId >= 0 && sampleRate > 0.0)
    {
        const double samplesPerBeat  = sampleRate * 60.0 / bpm.load(std::memory_order_relaxed);
        const double globalPatBeats  = snap.stepCount * 0.25;  // global pattern length in beats

        if (globalPatBeats > 0.0)
        {
            const double startBeat = patternBeatPos;
            const double endBeat   = startBeat + numSamples / samplesPerBeat;

            for (int ch = 0; ch < PlaybackSnapshot::kCh; ++ch)
            {
                // Per-channel loop may have its own length (live performance recording).
                // If a channel-override pattern exists, use its stepCount; else use global.
                const int chPatId = snap.channelPatternId[ch];
                double patternBeats = globalPatBeats;
                if (chPatId >= 0)
                {
                    if (const Pattern* chPat = findPatternById(chPatId))
                        patternBeats = juce::jmax(0.25, chPat->stepCount * 0.25);
                }

                // Only Melodic channels OR channels with a live-performance override play notes.
                if (snap.channelTypes[ch] != ChannelType::Melodic && chPatId < 0) continue;

                const double loopStart = std::fmod(startBeat, patternBeats);
                const double loopEnd   = std::fmod(endBeat,   patternBeats);
                const auto beatsFromBlockStart = [loopStart, loopEnd, patternBeats](double loopBeat) -> double
                {
                    if (loopEnd < loopStart)
                    {
                        if (loopBeat >= loopStart)
                            return loopBeat - loopStart;
                        return (patternBeats - loopStart) + loopBeat;
                    }
                    return loopBeat - loopStart;
                };

                // M8 -- flush pending note-offs for plugin channels
                // Only process plugin path if the CURRENT pattern has plugin enabled
                const bool patPluginActive = snap.pluginSlotEnabled[ch]
                                          && instrumentPlugins[(size_t)ch] != nullptr;
                if (patPluginActive)
                {
                    auto& notes = activePluginNotes[ch];
                    for (int i = 0; i < notes.count; )
                    {
                        const auto& activeNote = notes.notes[(size_t)i];
                        if (activeNote.endBeat < endBeat)
                        {
                            const int noteOffOffset = (activeNote.endBeat <= startBeat) ? 0
                                : juce::jlimit(0, juce::jmax(0, numSamples - 1),
                                                (int)std::round((activeNote.endBeat - startBeat) * samplesPerBeat));
                            instrumentMidiBuffers[ch].addEvent(
                                juce::MidiMessage::noteOff(1, activeNote.pitch), noteOffOffset);
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
                        // M8 -- VST/AU plugin path
                        const int tp = juce::jlimit(0, 127,
                            note.pitch + (int)std::round(channelBasePitch[(size_t)ch].load(std::memory_order_relaxed)));
                        if (patPluginActive)
                        {
                            const double onBeatFromStart = beatsFromBlockStart(ns);
                            const int noteOnOffset = juce::jlimit(0, juce::jmax(0, numSamples - 1),
                                                                   (int)std::round(onBeatFromStart * samplesPerBeat));
                            instrumentMidiBuffers[ch].addEvent(
                                juce::MidiMessage::noteOn(1, tp, note.velocity), noteOnOffset);
                            // Schedule note-off at endBeat
                            const double absoluteEnd = startBeat + onBeatFromStart + note.lengthBeats;
                            activePluginNotes[ch].push({ absoluteEnd, tp });
                        }
                        else
                        {
                            const ChannelSourceType srcType = snap.channelSourceTypes[(size_t)ch];
                            const SynthParams&      sp      = snap.synthParams[(size_t)ch];
                            const int noteLenSamples = (int)(note.lengthBeats * samplesPerBeat);
                            const int mixerTrack     = juce::jlimit(0, 7, snap.channelMixerRouting[(size_t)ch]);

                            if (sp.enabled || srcType == ChannelSourceType::Sampler)
                            {
                                auto samplerBuf = (srcType == ChannelSourceType::Sampler)
                                                  ? getSamplerSourceBuffer(ch) : nullptr;
                                dispatchVoiceNote(ch, tp, note.velocity, noteLenSamples,
                                                  1.0f, 0.0f, mixerTrack,
                                                  sp, srcType, snap.samplerParams[(size_t)ch],
                                                  std::move(samplerBuf));
                            }
                            else
                            {
                                // Raw sample fallback -- no synth/sampler, play via sample trigger
                                auto sourceBuffer = getChannelSourceBufferShared(ch);
                                scheduleSampleTrigger(ch, 0, mixerTrack,
                                                      sourceBuffer.get(), sourceBuffer,
                                                      snap.channelVolume[(size_t)ch] * note.velocity,
                                                      snap.channelPan[(size_t)ch],
                                                      channelBasePitch[(size_t)ch].load(std::memory_order_relaxed) + (float)(note.pitch - 60),
                                                      1.0f);
                            }
                        }
                    }
                }
            }

            // Advance beat position, wrapping to loop start when loop recording is active
            const double nextBeatPos = patternBeatPos + numSamples / samplesPerBeat;
            if (loopRecordEnabled_
                && patternBeatPos < loopMarkers_.endBeat
                && nextBeatPos  >= loopMarkers_.endBeat)
            {
                commitHangingNotes();
                patternBeatPos = loopMarkers_.startBeat + (nextBeatPos - loopMarkers_.endBeat);
            }
            else
            {
                patternBeatPos = nextBeatPos;
            }
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

    if (runtime.playlistClips.empty())
        return;

    double currentBpm = runtime.projectBpm;
    float effectiveMasterVolume = runtime.masterVolume;
    std::array<float, 16> automatedChannelVolumes;
    automatedChannelVolumes.fill(-1.0f);
    std::array<float, 8> automatedMixerVolMul;
    automatedMixerVolMul.fill(1.0f); // 1.0 = no automation (pass-through)

    // Read beat-based position first -- this survives BPM changes correctly
    const double startBeat = songBeatPosition_.load(std::memory_order_relaxed);

    if (!runtime.automationLanes.empty())
    {
        for (const auto& lane : runtime.automationLanes)
        {
            if (lane.paramId != "bpm") continue;

            currentBpm = (double)lane.evaluate(startBeat);
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
    const double beatsThisBlock = numSamples / samplesPerBeat;
    const double endBeat   = startBeat + beatsThisBlock;
    const double startBar  = startBeat / 4.0;
    const double endBar    = endBeat / 4.0;
    const double automationBeat = startBeat + beatsThisBlock * 0.5;

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
            else if (lane.paramId.startsWith("mixVol"))
            {
                const int t = lane.paramId.substring(6).getIntValue();
                if (t >= 0 && t < 8)
                    automatedMixerVolMul[(size_t)t] = v;
            }
        }
    }

    // --- Audio clip playback (Stage 1 -- playhead-based loop mode) ----------
    // For each instance, compute the file sample position from the current
    // playhead beat.  No trigger state: recalculated fresh every block.
    // If file is shorter than the clip it loops (% fileTotalSamples).
    {
        const juce::SpinLock::ScopedTryLockType tryLock(audioClipLock_);
        if (tryLock.isLocked())
        {
            for (auto& inst : audioClipInstances_)
            {
                inst.active = false;  // reset; set true below if overlapping
                if (inst.buffer == nullptr) continue;

                // Find matching clip in runtime state
                const PlaylistClip* clip = nullptr;
                for (const auto& c : runtime.playlistClips)
                    if (c.id == inst.clipId && c.clipType == ClipType::Audio)
                        { clip = &c; break; }
                if (clip == nullptr) continue;
                if (clip->muted) continue;

                const double clipStartBeat = clip->startBar * 4.0;
                const double clipEndBeat   = (clip->startBar + clip->lengthBars) * 4.0;

                // Skip if this block doesn't overlap the clip
                if (endBeat <= clipStartBeat || startBeat >= clipEndBeat) continue;

                const int fileSamples = inst.buffer->getNumSamples();
                if (fileSamples <= 0) continue;

                // Output sample offset: how far into the block does the clip start?
                const int outOffset = (startBeat < clipStartBeat)
                    ? juce::jmin((int)juce::jmax(0.0,
                                 (clipStartBeat - startBeat) * samplesPerBeat),
                                 numSamples - 1)
                    : 0;

                // Clip-local beat at the first output sample for this clip
                const double localBeat = juce::jmax(0.0,
                    startBeat + outOffset / samplesPerBeat - clipStartBeat);

                // Choose playback buffer and pitch ratio based on mode:
                //   Resample: original buffer, pitchRatio != 1 (speed+pitch change)
                //   Stretch/Elastique: pitchedBuffer (pre-processed), pitchRatio = 1.0
                const AudioClipMode clipMode = clip->audioClipMode;
                const bool useStretched = (clipMode != AudioClipMode::Resample)
                                          && (inst.pitchedBuffer != nullptr);

                const juce::AudioBuffer<float>* playBuf = useStretched
                    ? inst.pitchedBuffer.get()
                    : inst.buffer.get();
                const int playLen = playBuf->getNumSamples();
                if (playLen <= 0) continue;

                const double pitchRatio = useStretched
                    ? 1.0
                    : std::pow(2.0, (double)clip->pitchSemitone / 12.0);

                // File position = (clip-local output samples) x pitchRatio + slip offset, looped
                const double localSample = localBeat * samplesPerBeat * pitchRatio
                                         + (double)clip->sourceOffsetSamples;

                inst.active             = true;
                inst.startOffsetInBlock = outOffset;
                inst.fileReadStart      = std::fmod(localSample, (double)playLen);
                inst.fileTotalSamples   = playLen;
                inst.pitchRatio         = pitchRatio;

                // Fade envelope fields (proportional clamp applied here)
                inst.localBeatAtBlockStart = localBeat;
                inst.beatsPerSample        = 1.0 / samplesPerBeat;
                inst.clipLengthBeats       = clip->lengthBars * 4.0f;
                {
                    float fi = clip->fadeInBars  * 4.0f;
                    float fo = clip->fadeOutBars * 4.0f;
                    const float tot = fi + fo;
                    if (tot > inst.clipLengthBeats && tot > 0.0f)
                    {
                        const float sc = inst.clipLengthBeats / tot;
                        fi *= sc; fo *= sc;
                    }
                    inst.fadeInBeats  = fi;
                    inst.fadeOutBeats = fo;
                }
            }
        }
    }

    // Pattern/NoteEvent step-trigger loop (unchanged) -- skip if no patterns loaded
    if (!runtime.patterns.empty())
    {
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
        const double stepBeat    = stepBarPos * 4.0;
        const int    offsetInBuf = (int)((stepBeat - startBeat) * samplesPerBeat);

        if (offsetInBuf < 0 || offsetInBuf >= numSamples)
            continue;

        for (const auto& clip : runtime.playlistClips)
        {
            if (clip.muted) continue;
            if (stepBarPos < clip.startBar || stepBarPos >= clip.startBar + clip.lengthBars)
                continue;

            const Pattern* pattern = runtime.findPatternById(clip.patternId);
            if (pattern == nullptr || pattern->stepCount <= 0)
                continue;

            const double patternBars    = (double)pattern->stepCount / 16.0;
            const double localBarInClip = stepBarPos - clip.startBar;
            const double shiftedLocal   = localBarInClip + (double)clip.patternStartOffsetBars;
            const double localInPat     = std::fmod(juce::jmax(0.0, shiftedLocal), patternBars);
            const double pos01          = localInPat / patternBars;
            const int localStep = juce::jlimit(0, pattern->stepCount - 1,
                                               (int)std::floor(pos01 * (double)pattern->stepCount));

            const float stepFadeGain = computeClipFadeGain(clip, (float)localBarInClip);

            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            {
                updateSongMixState(*pattern, ch);
                if (!pattern->variations[clip.variationIdx].steps[ch][localStep]) continue;

                // Melodic channels are driven exclusively by NoteEvents (piano roll).
                // Skip step trigger to prevent double-triggering -> robotic envelope retrigger.
                if (pattern->channelTypes[(size_t)ch] == ChannelType::Melodic) continue;

                // --- Per-step params ---
                const StepParams& songSP = pattern->variations[clip.variationIdx].stepParams[ch][localStep];

                // Probability gate
                if (songSP.probability < 1.0f && stepParamRng_.nextFloat() > songSP.probability)
                    continue;

                const float songStepVel = juce::jlimit(0.0f, 1.0f, 0.8f * songSP.velocity);

                const bool useSynth = pattern->synthParams[(size_t)ch].enabled
                                   || pattern->channelSourceTypes[(size_t)ch] == ChannelSourceType::Sampler;
                if (useSynth)
                {
                    const int midiPitch = juce::jlimit(0, 127,
                        60 + (int)std::round(pattern->channelPitch[ch]) + songSP.pitchOffset);
                    const int baseLen       = juce::jmax(1, (int)(0.25 * samplesPerBeat));
                    const int noteLenSamples = juce::jmax(1, (int)(baseLen * songSP.gate));

                    // Phase 2 -- per-step cutoff modulation on top of channel cutoff
                    SynthParams songModSP = pattern->synthParams[(size_t)ch];
                    if (songSP.cutoffMod != 0.0f)
                        songModSP.cutoff = juce::jlimit(40.0f, 20000.0f,
                                                        songModSP.cutoff * std::pow(2.0f, songSP.cutoffMod));

                    const auto noteParams = makeNoteSynthParams(songModSP, midiPitch, songStepVel, noteLenSamples);
                    const int mixerTrack = juce::jlimit(0, 7, pattern->channelMixerRouting[ch]);
                    const float outVol   = songChannelVolume_[(size_t)ch] * stepFadeGain;
                    const float outPan   = songChannelPan_[(size_t)ch];

                    if (pattern->channelSourceTypes[(size_t)ch] == ChannelSourceType::Sampler)
                    {
                        const auto& samplerCache = getSongSamplerCache();
                        auto scit = samplerCache.find(clip.patternId);
                        std::shared_ptr<const juce::AudioBuffer<float>> samplerBuf =
                            (scit != samplerCache.end()) ? scit->second[(size_t)ch] : nullptr;
                        if (!samplerBuf) samplerBuf = getSamplerSourceBuffer(ch);  // fallback
                        if (samplerBuf)
                        {
                            // Phase 2 -- per-step sample start offset
                            SamplerParams songSamplerP = pattern->samplerParams[(size_t)ch];
                            if (songSP.startOffsetFrac > 0.0f && samplerBuf->getNumSamples() > 0)
                                songSamplerP.startOffsetSamples = (int)(songSP.startOffsetFrac * (float)samplerBuf->getNumSamples());

                            polySynths[(size_t)ch].noteOnSampler(
                                midiPitch, songStepVel, sampleRate, noteParams,
                                songSamplerP,
                                std::move(samplerBuf), noteLenSamples,
                                outVol, outPan, mixerTrack);
                        }
                    }
                    else
                    {
                        polySynths[(size_t)ch].noteOn(midiPitch, songStepVel, sampleRate,
                                                      noteParams, noteLenSamples,
                                                      outVol, outPan, mixerTrack);
                    }
                }
                else
                {
                    auto cit = songSampleCache.find(clip.patternId);
                    std::shared_ptr<const juce::AudioBuffer<float>> songBuffer =
                        (cit != songSampleCache.end()) ? cit->second[(size_t)ch] : nullptr;

                    // Phase 2 -- per-step sample start offset for raw song samples
                    double songRawStartOff = 0.0;
                    if (songSP.startOffsetFrac > 0.0f && songBuffer && songBuffer->getNumSamples() > 0)
                        songRawStartOff = songSP.startOffsetFrac * (double)songBuffer->getNumSamples();

                    scheduleSampleTrigger(ch, offsetInBuf, juce::jlimit(0, 7, pattern->channelMixerRouting[ch]), songBuffer.get(), songBuffer,
                                          songChannelVolume_[(size_t)ch] * stepFadeGain * songSP.velocity,
                                          songChannelPan_[(size_t)ch],
                                          pattern->channelPitch[ch] + (float)songSP.pitchOffset,
                                          (float)(currentBpm / juce::jmax(1.0, runtime.projectBpm)),
                                          songRawStartOff);
                }
            }
        }
    }

    {
        const double startBeatSong = startBeat;
        const double endBeatSong   = endBeat;

        // Flush pending plugin note-offs (song mode) -- must happen before note-ons
        // Use endBeat < endBeatSong (not >= startBeatSong) so past-due note-offs
        // that were missed in a previous block are still sent immediately.
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            if (instrumentPlugins[(size_t)ch] == nullptr) continue;
            auto& notes = activePluginNotes[ch];
            for (int i = 0; i < notes.count; )
            {
                const auto& activeNote = notes.notes[(size_t)i];
                if (activeNote.endBeat < endBeatSong)
                {
                    // Clamp: if endBeat is in the past, fire at sample 0; otherwise at correct offset
                    const int noteOffOffset = (activeNote.endBeat <= startBeatSong) ? 0
                        : juce::jlimit(0, juce::jmax(0, numSamples - 1),
                                        (int)std::round((activeNote.endBeat - startBeatSong) * samplesPerBeat));
                    instrumentMidiBuffers[ch].addEvent(
                        juce::MidiMessage::noteOff(1, activeNote.pitch), noteOffOffset);
                    notes.removeAt(i);
                }
                else { ++i; }
            }
        }

        for (const auto& clip : runtime.playlistClips)
        {
            if (clip.muted) continue;
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

                const double patOffsetBeats = (double)clip.patternStartOffsetBars * 4.0;

                for (const auto& note : pat->variations[clip.variationIdx].notes[ch])
                {
                    const double notePhaseRaw  = std::fmod((double)note.startBeat, patternBeats);
                    // Shift note phase by -offset so the pattern effectively starts at offsetBeats
                    const double notePhase     = std::fmod(notePhaseRaw - patOffsetBeats + patternBeats * 512.0, patternBeats);
                    const double relStart  = startBeatSong - clipStartBeat;
                    const double loopIdxD  = (relStart - notePhase) / patternBeats;
                    const int kStart = (loopIdxD < 0.0) ? 0 : juce::jmax(0, (int)loopIdxD - 1);

                    for (int k = kStart; k <= kStart + 2; ++k)
                    {
                        const double fireBeat = clipStartBeat + k * patternBeats + notePhase;
                        if (fireBeat < clipStartBeat || fireBeat >= clipEndBeat) continue;
                        if (fireBeat < startBeatSong || fireBeat >= endBeatSong)  continue;

                        const float noteLocalBars = (float)((fireBeat - clipStartBeat) / 4.0);
                        const float noteFadeGain  = computeClipFadeGain(clip, noteLocalBars);

                        const ChannelSourceType srcType = pat->channelSourceTypes[(size_t)ch];
                        const SynthParams&      sp      = pat->synthParams[(size_t)ch];
                        const int tp2        = juce::jlimit(0, 127, note.pitch + (int)std::round(pat->channelPitch[ch]));
                        const int noteLenSamples = (int)(note.lengthBeats * samplesPerBeat);
                        const int mixerTrack = juce::jlimit(0, 7, pat->channelMixerRouting[ch]);

                        const int noteOnOffset = juce::jlimit(0, juce::jmax(0, numSamples - 1),
                                                               (int)((fireBeat - startBeatSong) * samplesPerBeat));

                        // M8 -- VST/AU plugin path (song mode)
                        if (instrumentPlugins[(size_t)ch] != nullptr)
                        {
                            instrumentMidiBuffers[ch].addEvent(
                                juce::MidiMessage::noteOn(1, tp2, note.velocity), noteOnOffset);
                            activePluginNotes[ch].push({ fireBeat + note.lengthBeats, tp2 });
                        }
                        else if (sp.enabled || srcType == ChannelSourceType::Sampler)
                        {
                            // Resolve sampler buffer: prefer per-pattern cache, fall back to live channel buffer
                            std::shared_ptr<const juce::AudioBuffer<float>> samplerBuf;
                            if (srcType == ChannelSourceType::Sampler)
                            {
                                const auto& samplerCache = getSongSamplerCache();
                                auto scit = samplerCache.find(clip.patternId);
                                samplerBuf = (scit != samplerCache.end()) ? scit->second[(size_t)ch] : nullptr;
                                if (!samplerBuf) samplerBuf = getSamplerSourceBuffer(ch);
                            }
                            // Bake channel vol/pan into the voice at dispatch time.
                            // Song mode synths render via renderNextBlockRouted() which has
                            // no post-render gain stage, so vol/pan must be in the voice itself.
                            dispatchVoiceNote(ch, tp2, note.velocity, noteLenSamples,
                                              songChannelVolume_[(size_t)ch] * noteFadeGain,
                                              songChannelPan_[(size_t)ch], mixerTrack,
                                              sp, srcType, pat->samplerParams[(size_t)ch],
                                              std::move(samplerBuf));
                        }
                        else
                        {
                            // Raw sample fallback -- no synth/sampler, play via sample trigger
                            auto cit = songSampleCache.find(clip.patternId);
                            std::shared_ptr<const juce::AudioBuffer<float>> songBuffer =
                                (cit != songSampleCache.end()) ? cit->second[(size_t)ch] : nullptr;
                            scheduleSampleTrigger(ch, noteOnOffset, mixerTrack, songBuffer.get(), songBuffer,
                                                  songChannelVolume_[(size_t)ch] * note.velocity * noteFadeGain,
                                                  songChannelPan_[(size_t)ch],
                                                  pat->channelPitch[ch] + (float)(note.pitch - 60),
                                                  (float)(currentBpm / juce::jmax(1.0, runtime.projectBpm)));
                        }
                    }
                }
            }
        }
    }
    } // end if (!runtime.patterns.empty())

    dispatchScheduledSampleTriggers();
    MixRuntimeOverrides overrides;
    overrides.hasMasterOverride = true;
    overrides.masterVolume  = effectiveMasterVolume;
    overrides.mixerVolMul   = automatedMixerVolMul;
    overrides.masterPan = runtime.masterPan;
    mixToOutput(buffer, numSamples, &overrides);

    // Advance beat position (BPM-aware -- survives tempo automation correctly)
    songBeatPosition_.store(endBeat, std::memory_order_relaxed);
    // Keep sample position in sync for UI playhead display
    songSamplePosition.store((long)(endBeat * samplesPerBeat), std::memory_order_relaxed);
}

// M5 -- route channels through mixer tracks and sum to output
void AudioEngine::mixToOutput(juce::AudioBuffer<float>& output, int numSamples,
                              const MixRuntimeOverrides* overrides)
{
    // Guard: stagingBuf and mixerTrackBufs are allocated to bufferSize in
    // audioDeviceAboutToStart. If the driver delivers a smaller block, clamp.
    const int safe = juce::jmin(numSamples, stagingBuf.getNumSamples());

    // -- CC Real-Time Parameter Smoothing -------------------------------------
    // One exponential step per block toward the target set by the message thread.
    // tau ~= 20 ms gives smooth transitions without audible lag at knob speed.
    {
        const float tau     = 0.020f;  // 20 ms smoothing time constant
        const float coeff   = (sampleRate > 0.0)
                            ? (1.0f - std::exp(-(float)safe / ((float)sampleRate * tau)))
                            : 1.0f;
        for (int ch = 0; ch < 16; ++ch)
        {
            ccVol_[ch].tick(coeff);
            ccPan_[ch].tick(coeff);
        }
    }
    // -------------------------------------------------------------------------

    // Clear mixer track buses
    for (auto& tb : mixerTrackBufs)
        tb.clear();

    const auto& runtime = getRuntimeState();

    // Use snapshot for routing and synth params -- lock-free audio thread access
    const PlaybackSnapshot& mixSnap = snapshots_[activeSnapshotIdx_.load(std::memory_order_acquire)];
    std::array<int, 8> routedSourcesPerTrack {};

    // Render each channel into staging, accumulate into its assigned mixer track
    for (int ch = 0; ch < 16; ++ch)
    {
        stagingBuf.clear();

        // In pattern mode, only use plugin if the current pattern has it enabled.
        // In song mode, plugin usage is determined by ensureSongPluginsLoaded().
        const bool usePlugin = (instrumentPlugins[(size_t)ch] != nullptr)
                            && (playMode == PlayMode::Song || mixSnap.pluginSlotEnabled[ch]);
        // Song mode synth voices are rendered exclusively via renderNextBlockRouted()
        // below, which uses per-voice mixer track routing.  Calling renderNextBlock()
        // here in Song mode would render the same voice objects a second time in the
        // same audio callback -- the first call advances voice state (ADSR, oscillator
        // phase), so the second call produces a broken, phasey, robotic signal.
        // Pattern mode still uses this staging path (renderNextBlockRouted is skipped).
        const bool useSynth = (playMode != PlayMode::Song)
                           && !usePlugin
                           && (polySynths[(size_t)ch].isAnyActive()
                               || (mixSnap.patternId >= 0
                                   && mixSnap.synthParams[(size_t)ch].enabled));

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

                // If the plugin is mono, duplicate L -> R
                const int plugOuts = instrumentPlugins[(size_t)ch]->getTotalNumOutputChannels();
                if (plugOuts == 1 && stagingBuf.getNumChannels() > 1)
                    stagingBuf.copyFrom(1, 0, stagingBuf, 0, 0, safe);
            }
        }
        if (useSynth)
        {
            float* L = stagingBuf.getWritePointer(0);
            float* R = stagingBuf.getWritePointer(1);
            polySynths[(size_t)ch].renderNextBlock(L, R, safe, sampleRate);
        }

        if (usePlugin || useSynth)
        {
            // Pattern / Song base volume & pan
            const float baseVolume = (playMode == PlayMode::Song)
                                   ? songChannelVolume_[(size_t)ch]
                                   : ((mixSnap.patternId >= 0) ? mixSnap.channelVolume[(size_t)ch] : 0.8f);
            const float basePan    = (playMode == PlayMode::Song)
                                   ? songChannelPan_[(size_t)ch]
                                   : ((mixSnap.patternId >= 0) ? mixSnap.channelPan[(size_t)ch] : 0.0f);

            // CC real-time multipliers (smoothed this block, range [0,1] and [0,1]->[-1,+1])
            const float ccVol      = ccVol_[(size_t)ch].get();
            const float ccPanNorm  = ccPan_[(size_t)ch].get();          // 0.0-1.0
            const float ccPanBip   = (ccPanNorm * 2.0f) - 1.0f;       // normalise -> [-1, +1]

            const float channelVolume = baseVolume * ccVol;
            const float channelPan    = juce::jlimit(-1.0f, 1.0f, basePan + ccPanBip * 0.5f);

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
        ++routedSourcesPerTrack[(size_t) trackIdx];

        for (int s = 0; s < safe; ++s)
        {
            mixerTrackBufs[(size_t)trackIdx].addSample(0, s, stagingBuf.getSample(0, s));
            mixerTrackBufs[(size_t)trackIdx].addSample(1, s, stagingBuf.getSample(1, s));
        }
    }

    // Render sample voice pools and synths into mixer track buses
    // (must happen before per-track FX/AT processing)
    for (auto& pool : sampleVoicePools)
        pool.renderNextBlockRouted(mixerTrackBufs, safe);

    if (playMode == PlayMode::Song)
        for (auto& synth : polySynths)
            synth.renderNextBlockRouted(mixerTrackBufs, safe, sampleRate);

    // --- Audio clip rendering (Stage 1 -- playhead-based loop mode) ------
    // inst.active / fileReadStart / fileTotalSamples are set each block
    // by processSongMode.  Uses try-lock: skip silently if lock unavailable.
    // Must run BEFORE per-track AT/FX so auto-tune can process audio clips.
    {
        const juce::SpinLock::ScopedTryLockType tryLock(audioClipLock_);
        if (tryLock.isLocked())
        {
            for (auto& inst : audioClipInstances_)
            {
                if (!inst.active || inst.buffer == nullptr || inst.fileTotalSamples <= 0) continue;

                // Use pitched buffer when available (Stretch/Elastique); else original
                const juce::AudioBuffer<float>* playBuf =
                    (inst.pitchedBuffer != nullptr) ? inst.pitchedBuffer.get() : inst.buffer.get();

                const int    srcCh   = playBuf->getNumChannels();
                const int    startS  = inst.startOffsetInBlock;
                const int    fileLen = inst.fileTotalSamples;
                const double pitch   = inst.pitchRatio;
                double       filePos = inst.fileReadStart;

                for (int s = startS; s < safe; ++s)
                {
                    // Stop at end of file — no looping for one-shot audio clips
                    if (filePos >= (double)fileLen || filePos < 0.0) break;

                    const double wp = filePos;

                    // Linear interpolation between adjacent samples
                    const int   s0   = (int)wp;
                    const int   s1   = juce::jmin(s0 + 1, fileLen - 1);
                    const float frac = (float)(wp - (double)s0);
                    const float invF = 1.0f - frac;

                    float sL = playBuf->getSample(0, s0) * invF
                             + playBuf->getSample(0, s1) * frac;
                    float sR = (srcCh > 1)
                             ? playBuf->getSample(1, s0) * invF
                             + playBuf->getSample(1, s1) * frac
                             : sL;

                    // Fade gain envelope
                    if (inst.fadeInBeats > 0.0f || inst.fadeOutBeats > 0.0f)
                    {
                        const float clipBeat = (float)(inst.localBeatAtBlockStart
                                              + (double)(s - startS) * inst.beatsPerSample);
                        float gain = 1.0f;
                        if (inst.fadeInBeats > 0.0f && clipBeat < inst.fadeInBeats)
                            gain = juce::jlimit(0.0f, 1.0f, clipBeat / inst.fadeInBeats);
                        if (inst.fadeOutBeats > 0.0f)
                        {
                            const float remain = inst.clipLengthBeats - clipBeat;
                            if (remain < inst.fadeOutBeats)
                                gain = juce::jmin(gain, juce::jlimit(0.0f, 1.0f, remain / inst.fadeOutBeats));
                        }
                        sL *= gain;
                        sR *= gain;
                    }

                    mixerTrackBufs[0].addSample(0, s, sL);
                    mixerTrackBufs[0].addSample(1, s, sR);

                    filePos += pitch;
                }
            }
        }
    }

    // M14 -- apply FX chains per mixer track bus
    // (now runs AFTER all audio sources are mixed into track buses)
    for (int t = 0; t < 8; ++t)
    {
        float trackPeak = 0.0f;
        for (int s = 0; s < safe; ++s)
        {
            trackPeak = juce::jmax(trackPeak, std::abs(mixerTrackBufs[(size_t)t].getSample(0, s)));
            trackPeak = juce::jmax(trackPeak, std::abs(mixerTrackBufs[(size_t)t].getSample(1, s)));
        }

        const float targetTrim = computeAdaptiveTrim(routedSourcesPerTrack[(size_t)t], trackPeak);
        trackInputTrim_[(size_t)t] = smoothingTowards(trackInputTrim_[(size_t)t], targetTrim, 0.18f);
        mixerTrackBufs[(size_t)t].applyGain(trackInputTrim_[(size_t)t]);

        // Auto-Tune insert (pre-FX for cleanest pitch detection)
        {
            const AutoTuneParams& atp = runtime.autoTuneParams[(size_t)t];
            if (atp.enabled)
            {
                float* atL = mixerTrackBufs[(size_t)t].getWritePointer(0);
                float* atR = mixerTrackBufs[(size_t)t].getWritePointer(1);
                autoTuneProcessors_[(size_t)t].processBlock(atL, atR, safe, atp);
            }
        }

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

    float masterPeak = 0.0f;
    for (int t = 0; t < 8; ++t)
    {
        const auto& mt = runtime.mixerTracks[(size_t)t];
        if (mt.muted || (anySoloed && !mt.soloed))
            continue;

        const float autoMul = (overrides != nullptr) ? overrides->mixerVolMul[(size_t)t] : 1.0f;
        const float ang = (mt.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float trackL = mt.volume * autoMul * std::cos(ang);
        const float trackR = mt.volume * autoMul * std::sin(ang);

        for (int s = 0; s < safe; ++s)
        {
            masterPeak = juce::jmax(masterPeak, std::abs(mixerTrackBufs[(size_t)t].getSample(0, s) * trackL * mL));
            masterPeak = juce::jmax(masterPeak, std::abs(mixerTrackBufs[(size_t)t].getSample(1, s) * trackR * mR));
        }
    }

    const float targetMasterTrim = masterPeak > 0.86f ? 0.86f / masterPeak : 1.0f;
    masterInputTrim_ = smoothingTowards(masterInputTrim_,
                                        juce::jlimit(0.6f, 1.0f, targetMasterTrim),
                                        0.12f);

    // Mix each track bus into output with track volume/pan
    for (int t = 0; t < 8; ++t)
    {
        const auto& mt = runtime.mixerTracks[(size_t)t];
        if (mt.muted || (anySoloed && !mt.soloed))
            continue;

        const float autoMul2 = (overrides != nullptr) ? overrides->mixerVolMul[(size_t)t] : 1.0f;
        const float ang = (mt.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float L   = mt.volume * autoMul2 * std::cos(ang);
        const float R   = mt.volume * autoMul2 * std::sin(ang);

        for (int s = 0; s < safe; ++s)
        {
            if (output.getNumChannels() > 0)
                output.addSample(0, s, mixerTrackBufs[(size_t)t].getSample(0, s) * L * mL * masterInputTrim_);
            if (output.getNumChannels() > 1)
                output.addSample(1, s, mixerTrackBufs[(size_t)t].getSample(1, s) * R * mR * masterInputTrim_);
        }
    }

    // Master bus Dynamic EQ (post-sum, pre-output)
    if (masterDynEQ_.isEnabled() && output.getNumChannels() >= 2)
    {
        float* L = output.getWritePointer(0);
        float* R = output.getWritePointer(1);
        masterDynEQ_.processBlock(L, R, safe);
    }

    if (output.getNumChannels() >= 2)
    {
        float* left = output.getWritePointer(0);
        float* right = output.getWritePointer(1);
        const float attackCoeff = std::exp(-1.0f / (float) juce::jmax(1.0, sampleRate * 0.0018));
        const float releaseCoeff = std::exp(-1.0f / (float) juce::jmax(1.0, sampleRate * 0.120));
        constexpr float threshold = 0.78f;
        constexpr float engageRange = 0.08f;
        constexpr float compression = 0.95f;
        constexpr float makeup = 1.015f;
        constexpr float wet = 0.22f;

        for (int s = 0; s < safe; ++s)
        {
            const float dryL = left[s];
            const float dryR = right[s];
            const float detector = juce::jmax(std::abs(dryL), std::abs(dryR));
            const float coeff = detector > masterGlueEnvelope_ ? attackCoeff : releaseCoeff;
            masterGlueEnvelope_ = detector + coeff * (masterGlueEnvelope_ - detector);

            const float over = juce::jmax(0.0f, masterGlueEnvelope_ - threshold);
            const float engage = juce::jlimit(0.0f, 1.0f, over / engageRange);
            const float gainReduction = 1.0f / (1.0f + over * compression * engage);
            const float shapedL = softSaturate(dryL * gainReduction * makeup);
            const float shapedR = softSaturate(dryR * gainReduction * makeup);

            left[s] = dryL + (shapedL - dryL) * wet;
            right[s] = dryR + (shapedR - dryR) * wet;
        }
    }

    // -- Metronome click (mixed directly after limiter so it's always audible) --
    if (metronomeEnabled_.load(std::memory_order_relaxed) && sampleRate > 0.0 && output.getNumChannels() >= 1)
    {
        const double bpmVal  = bpm.load(std::memory_order_relaxed);
        const float  gain    = metronomeGain_.load(std::memory_order_relaxed);
        const double globalB = liveLoopEngine_.getGlobalBeat();

        float* left  = output.getWritePointer(0);
        float* right = (output.getNumChannels() > 1) ? output.getWritePointer(1) : left;

        for (int s = 0; s < safe; ++s)
        {
            // Compute exact beat position for this sample
            const double beatHere = globalB - (double)(safe - s) * (bpmVal / (60.0 * sampleRate));

            // Detect integer beat crossing
            const double prevB = metronomePrevBeat_;
            metronomePrevBeat_ = beatHere;
            if (beatHere < 0.0) continue;

            const double curFloor  = std::floor(beatHere);
            const double prevFloor = std::floor(prevB);
            if (curFloor > prevFloor && prevFloor >= 0.0)
            {
                // Beat boundary: kick off a click
                const long beatIdx = (long)curFloor;
                const bool isBeat1 = (beatIdx % 4 == 0);
                clickFreq_       = isBeat1 ? 1050.0f : 700.0f;
                clickPhase_      = 0.0f;
                clickEnv_        = 1.0f;
                clickSamplesLeft_ = (int)(sampleRate * 0.022);  // 22 ms click
            }

            if (clickSamplesLeft_ > 0)
            {
                const float decayRate = 1.0f - 1.0f / (float)(sampleRate * 0.018f);
                clickEnv_ *= decayRate;
                const float click = std::sin(clickPhase_) * clickEnv_ * gain;
                clickPhase_ += juce::MathConstants<float>::twoPi * clickFreq_ / (float)sampleRate;
                left[s]  += click;
                right[s] += click;
                --clickSamplesLeft_;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// M10 -- Offline render
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
    for (auto& at : autoTuneProcessors_) at.reset();

    // Set up playback state for offline pass
    const double samplesPerBar = (sampleRate * 60.0 / bpm.load(std::memory_order_relaxed)) * 4.0;
    const long   totalSamples  = (long)std::ceil((double)numBars * samplesPerBar);

    if (mode == PlayMode::Pattern)
    {
        patternBeatPos = 0.0;   // must reset before offline render
        sequencer.start(0);
    }
    else
    {
        buildSongSampleCache();
        songSamplePosition.store(0, std::memory_order_relaxed);
        songBeatPosition_.store(0.0, std::memory_order_relaxed);
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
// Stem Export -- renders each mixer track to a separate WAV in the given folder
// ---------------------------------------------------------------------------

int AudioEngine::renderStemsToFolder(const juce::File& folder, PlayMode mode, int numBars)
{
    if (!folder.isDirectory())
        folder.createDirectory();

    if (!folder.isDirectory() || project == nullptr)
        return 0;

    // Save original mute/solo state
    struct TrackState { bool muted; bool soloed; };
    std::array<TrackState, 8> savedState;
    for (int t = 0; t < 8; ++t)
    {
        savedState[(size_t)t].muted  = project->mixerTracks[(size_t)t].muted;
        savedState[(size_t)t].soloed = project->mixerTracks[(size_t)t].soloed;
    }

    int exported = 0;

    // --- Render each track individually ---
    for (int t = 0; t < 8; ++t)
    {
        // Check if any channel routes to this track
        bool hasContent = false;
        for (const auto& pat : project->patterns)
            for (int ch = 0; ch < 16; ++ch)
                if (pat.channelMixerRouting[ch] == t)
                    hasContent = true;

        if (!hasContent) continue; // skip empty tracks

        // Solo this track, mute all others
        for (int i = 0; i < 8; ++i)
        {
            project->mixerTracks[(size_t)i].muted = false;
            project->mixerTracks[(size_t)i].soloed = (i == t);
        }

        // Build filename: "Track_1_Name.wav"
        juce::String trackName = project->mixerTracks[(size_t)t].name
                                    .replaceCharacters(" /\\:", "____");
        juce::File outFile = folder.getChildFile(
            "Track_" + juce::String(t + 1) + "_" + trackName + ".wav");

        if (renderToFile(outFile, mode, numBars))
            ++exported;
    }

    // --- Render master (all tracks) ---
    for (int i = 0; i < 8; ++i)
    {
        project->mixerTracks[(size_t)i].muted = false;
        project->mixerTracks[(size_t)i].soloed = false;
    }

    juce::File masterFile = folder.getChildFile("Master.wav");
    if (renderToFile(masterFile, mode, numBars))
        ++exported;

    // --- Restore original mute/solo state ---
    for (int t = 0; t < 8; ++t)
    {
        project->mixerTracks[(size_t)t].muted  = savedState[(size_t)t].muted;
        project->mixerTracks[(size_t)t].soloed = savedState[(size_t)t].soloed;
    }

    // Rebuild runtime state with restored mute/solo
    rebuildRuntimeStateFromProject();

    return exported;
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
