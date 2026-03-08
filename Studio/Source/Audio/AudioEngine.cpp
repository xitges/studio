/*
  ==============================================================================

    AudioEngine.cpp
    Created: 6 Mar 2026 12:25:31pm
    Author:  홍준영

  ==============================================================================
*/

#include "AudioEngine.h"

AudioEngine::AudioEngine()
    : sequencer([this](int ch) { triggerChannel(ch); })
{
}

AudioEngine::~AudioEngine()
{
    shutdown();
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
        buildSongSampleCache();
        songSamplePosition = 0;
        songPlaying        = true;
    }
}

void AudioEngine::stop()
{
    if (playMode == PlayMode::Pattern)
        sequencer.stop();
    else
        songPlaying = false;
}

// ---- Mute / Solo -------------------------------------------------------

void AudioEngine::applyChannelMuteLogic()
{
    // Check if any channel is soloed
    bool anySoloed = false;
    for (int i = 0; i < 16; ++i)
        if (channelSoloed[i]) { anySoloed = true; break; }

    for (int i = 0; i < 16; ++i)
    {
        bool silenced = channelMuted[i] || (anySoloed && !channelSoloed[i]);
        players[i].setMuted(silenced);
    }
}

void AudioEngine::setChannelMuted(int ch, bool muted)
{
    if (ch < 0 || ch >= 16) return;
    channelMuted[ch] = muted;
    applyChannelMuteLogic();
}

void AudioEngine::setChannelSolo(int ch, bool soloed)
{
    if (ch < 0 || ch >= 16) return;
    channelSoloed[ch] = soloed;
    applyChannelMuteLogic();
}

// ---- M1.1 Volume / Pan -------------------------------------------------

void AudioEngine::setChannelVolume(int ch, float volume)
{
    if (ch >= 0 && ch < 16)
        players[ch].setVolume(volume);
}

void AudioEngine::setChannelPan(int ch, float pan)
{
    if (ch >= 0 && ch < 16)
        players[ch].setPan(pan);
}

// ---- M1.2 Pitch --------------------------------------------------------

void AudioEngine::setChannelPitch(int ch, float semitones)
{
    if (ch >= 0 && ch < 16)
    {
        channelBasePitch[ch] = semitones;
        players[ch].setPitch(semitones);
    }
}

// ---- M1.3 Envelope -----------------------------------------------------

void AudioEngine::setChannelAttack(int ch, float ms)
{
    if (ch >= 0 && ch < 16)
        players[ch].setAttack(ms);
}

void AudioEngine::setChannelRelease(int ch, float ms)
{
    if (ch >= 0 && ch < 16)
        players[ch].setRelease(ms);
}

// ---- M3 — Active pattern / pitch ----------------------------------------

void AudioEngine::setActivePattern(int patternId)
{
    activePatternId = patternId;
    patternBeatPos  = 0.0;
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

void AudioEngine::allSynthNotesOff()
{
    for (auto& ps : polySynths)
        ps.allNotesOff();
    for (auto& fx : fxChains)
        fx.reset();
}

void AudioEngine::previewNote(int ch, int midiPitch)
{
    if (ch < 0 || ch >= 16) return;
    // If channel has synth enabled, use PolySynth; otherwise use sample player
    if (project && project->synthParams[(size_t)ch].enabled)
    {
        const double samplesPerBeat = sampleRate * 60.0 / bpm;
        const int noteLenSamples = (int)(0.25 * samplesPerBeat);  // 16th note preview
        polySynths[(size_t)ch].noteOn(midiPitch, 0.8f, sampleRate,
                                       project->synthParams[(size_t)ch], noteLenSamples);
    }
    else
    {
        players[ch].setPitch(channelBasePitch[ch] + (float)(midiPitch - 60));
        players[ch].trigger();
    }
}

void AudioEngine::previewBrowserFile(const juce::File& f)
{
    browserPreviewPlayer.loadFile(f);
    browserPreviewPlayer.setVolume(0.8f);
    browserPreviewPlayer.setPitch(0.0f);
    browserPreviewPlayer.trigger();
}

void AudioEngine::previewSynthNote(int ch, int midiPitch, const SynthParams& p)
{
    if (ch < 0 || ch >= 16) return;
    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    const int noteLenSamples = (int)(0.5 * samplesPerBeat);  // 8th note preview
    polySynths[(size_t)ch].noteOn(midiPitch, 0.8f, sampleRate, p, noteLenSamples);
}

// ---- M5 — Mixer track controls ------------------------------------------

void AudioEngine::setMixerTrackVolume(int track, float vol)
{
    if (project && track >= 0 && track < (int)project->mixerTracks.size())
        project->mixerTracks[(size_t)track].volume = vol;
}
void AudioEngine::setMixerTrackPan(int track, float pan)
{
    if (project && track >= 0 && track < (int)project->mixerTracks.size())
        project->mixerTracks[(size_t)track].pan = pan;
}
void AudioEngine::setMixerTrackMuted(int track, bool muted)
{
    if (project && track >= 0 && track < (int)project->mixerTracks.size())
        project->mixerTracks[(size_t)track].muted = muted;
}
void AudioEngine::setMixerTrackSoloed(int track, bool soloed)
{
    if (project && track >= 0 && track < (int)project->mixerTracks.size())
        project->mixerTracks[(size_t)track].soloed = soloed;
}
void AudioEngine::setMasterVolume(float vol)
{
    if (project) project->masterVolume = vol;
}
void AudioEngine::setMasterPan(float pan)
{
    if (project) project->masterPan = pan;
}

// ---- BPM / Mode / Project ----------------------------------------------

void AudioEngine::setBPM(double newBpm)
{
    bpm = newBpm;
    sequencer.setBPM(newBpm);
}

bool AudioEngine::isPlaying() const
{
    if (playMode == PlayMode::Pattern)
        return sequencer.isPlaying();
    return songPlaying;
}

void AudioEngine::setPlayMode(PlayMode mode)
{
    playMode = mode;
}

void AudioEngine::setProject(Project* projectPtr)
{
    project = projectPtr;
}

void AudioEngine::setPatternStepCount(int stepCount)
{
    sequencer.setStepCount(stepCount);
}

void AudioEngine::loadSample(int channelIndex, const juce::File& file)
{
    if (channelIndex >= 0 && channelIndex < 16)
        players[channelIndex].loadFile(file);
}

void AudioEngine::unloadSample(int channelIndex)
{
    if (channelIndex >= 0 && channelIndex < 16)
        players[channelIndex].clear();
}

void AudioEngine::buildSongSampleCache()
{
    songSampleCache.clear();
    std::fill_n(songPlayerPatternId, 16, -1);

    if (project == nullptr) return;

    for (const auto& pat : project->patterns)
    {
        auto& cache = songSampleCache[pat.id];   // default-constructs 16 empty AudioBuffers

        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            if (pat.samplePaths[ch].isEmpty()) continue;

            // Decode the file on the message thread into a temporary player,
            // then copy the decoded buffer into the cache (no disk I/O at runtime)
            SamplePlayer helper;
            helper.prepare(sampleRate, bufferSize);
            helper.loadFile(juce::File(pat.samplePaths[ch]));
            if (helper.isLoaded())
                cache[(size_t)ch].makeCopyOf(helper.getBuffer());
        }
    }
}

void AudioEngine::triggerChannel(int channelIndex)
{
    if (channelIndex >= 0 && channelIndex < 16)
        players[channelIndex].trigger();
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

    // M12 — process incoming MIDI messages
    {
        juce::MidiBuffer midiBuf;
        midiCollector.removeNextBlockOfMessages(midiBuf, numSamples);

        const int ch = juce::jlimit(0, 15, midiTargetChannel);

        for (const auto meta : midiBuf)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                const int   pitch = msg.getNoteNumber();
                const float vel   = msg.getFloatVelocity();

                const bool useSynth = (project != nullptr) &&
                                       project->synthParams[(size_t)ch].enabled;
                if (useSynth)
                {
                    // noteLenSamples=0 → sustain until noteOff
                    polySynths[(size_t)ch].noteOn(pitch, vel, sampleRate,
                                                   project->synthParams[(size_t)ch], 0);
                }
                else
                {
                    players[ch].setPitch(channelBasePitch[ch] + (float)(pitch - 60));
                    players[ch].trigger();
                }
            }
            else if (msg.isNoteOff())
            {
                polySynths[(size_t)ch].noteOff(msg.getNoteNumber());
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                polySynths[(size_t)ch].allNotesOff();
            }
        }
    }

    if (playMode == PlayMode::Pattern)
        processPatternMode(buffer, numSamples, numOutputChannels);
    else
        processSongMode(buffer, numSamples, numOutputChannels);

    // Launchpad one-shot players — render directly into output (bypass mixer)
    const float mv = project ? project->masterVolume : 1.0f;
    for (auto& lp : launchpadPlayers)
    {
        if (!lp.isLoaded()) continue;
        stagingBuf.clear();
        lp.renderNextBlock(stagingBuf, numSamples);
        for (int s = 0; s < numSamples; ++s)
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
        browserPreviewPlayer.renderNextBlock(stagingBuf, numSamples);
        for (int s = 0; s < numSamples; ++s)
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

    for (auto& player : players)
        player.prepare(sampleRate, bufferSize);

    // M12 — reset MIDI collector to current sample rate
    midiCollector.reset(sampleRate);

    // M13/M14 — prepare synths and FX chains
    for (auto& s : polySynths) s.prepare(sampleRate, bufferSize);
    for (auto& fx : fxChains)  fx.prepare(sampleRate, bufferSize);

    // Launchpad players
    for (auto& lp : launchpadPlayers) lp.prepare(sampleRate, bufferSize);

    // M15 — browser preview player
    browserPreviewPlayer.prepare(sampleRate, bufferSize);

    // M5 — allocate mixer staging buffers
    stagingBuf.setSize(2, bufferSize);
    for (auto& tb : mixerTrackBufs)
        tb.setSize(2, bufferSize);
}

void AudioEngine::audioDeviceStopped()
{
    for (auto& player : players)       player.reset();
    for (auto& s : polySynths)         s.reset();
    for (auto& fx : fxChains)          fx.reset();
    for (auto& lp : launchpadPlayers)  lp.reset();
}

// ---- Render helpers ----------------------------------------------------

void AudioEngine::processPatternMode(juce::AudioBuffer<float>& buffer,
                                     int numSamples,
                                     int /*numOutputChannels*/)
{
    sequencer.processBlock(numSamples);

    // M3 — trigger NoteEvents for Melodic channels (only while sequencer is playing)
    if (sequencer.isPlaying() && project != nullptr)
    {
        const Pattern* pat = findPatternById(activePatternId);
        if (pat != nullptr && sampleRate > 0.0)
        {
            const double samplesPerBeat = sampleRate * 60.0 / bpm;
            const double patternBeats   = pat->stepCount * 0.25;  // 1 step = 1/16 note

            if (patternBeats > 0.0)
            {
                const double startBeat = patternBeatPos;
                const double endBeat   = startBeat + numSamples / samplesPerBeat;

                const double loopStart = std::fmod(startBeat, patternBeats);
                const double loopEnd   = std::fmod(endBeat,   patternBeats);

                for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
                {
                    if (project->channelTypes[ch] != ChannelType::Melodic) continue;
                    for (const auto& note : pat->notes[ch])
                    {
                        const double ns = std::fmod((double)note.startBeat, patternBeats);
                        const bool fires = (loopEnd < loopStart)
                                           ? (ns >= loopStart || ns < loopEnd)
                                           : (ns >= loopStart && ns < loopEnd);
                        if (fires)
                        {
                            const SynthParams& sp = project->synthParams[(size_t)ch];
                            if (sp.enabled)
                            {
                                // Synth path — noteOn with auto-release length
                                const int noteLenSamples = (int)(note.lengthBeats * samplesPerBeat);
                                polySynths[(size_t)ch].noteOn(note.pitch, note.velocity,
                                                               sampleRate, sp, noteLenSamples);
                            }
                            else
                            {
                                // Sample player path (original behaviour)
                                players[ch].setPitch(channelBasePitch[ch] + (float)(note.pitch - 60));
                                players[ch].trigger();
                            }
                        }
                    }
                }

                patternBeatPos += numSamples / samplesPerBeat;
            }
        }
    }

    mixToOutput(buffer, numSamples);
}

void AudioEngine::processSongMode(juce::AudioBuffer<float>& buffer,
                                  int numSamples,
                                  int /*numOutputChannels*/)
{
    if (!songPlaying || project == nullptr || sampleRate <= 0.0)
        return;

    if (project->playlistClips.empty() || project->patterns.empty())
        return;

    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    const double samplesPerBar  = samplesPerBeat * 4.0;

    auto barFromSample = [samplesPerBar](long s)
    {
        return (double)s / samplesPerBar;
    };

    const long startSample = songSamplePosition;
    const long endSample   = songSamplePosition + numSamples;

    const double startBar = barFromSample(startSample);
    const double endBar   = barFromSample(endSample);

    const int timelineStepsPerBar = 16;
    const int startStep = (int)std::floor(startBar * timelineStepsPerBar);
    const int endStep   = (int)std::floor(endBar   * timelineStepsPerBar);

    for (int stepIndex = startStep; stepIndex <= endStep; ++stepIndex)
    {
        const double stepBarPos  = (double)stepIndex / (double)timelineStepsPerBar;
        const long   stepSample  = (long)(stepBarPos * samplesPerBar);
        const int    offsetInBuf = (int)(stepSample - startSample);

        if (offsetInBuf < 0 || offsetInBuf >= numSamples)
            continue;

        for (const auto& clip : project->playlistClips)
        {
            if (stepBarPos < clip.startBar ||
                stepBarPos >= clip.startBar + clip.lengthBars)
                continue;

            const Pattern* pattern = findPatternById(clip.patternId);
            if (pattern == nullptr || pattern->stepCount <= 0)
                continue;

            const double patternBars    = (double)pattern->stepCount / (double)timelineStepsPerBar;
            const double localBarInClip = stepBarPos - clip.startBar;
            const double localInPat     = std::fmod(juce::jmax(0.0, localBarInClip), patternBars);
            const double pos01          = localInPat / patternBars;
            const int    localStep      = juce::jlimit(0, pattern->stepCount - 1,
                                                       (int)std::floor(pos01 * (double)pattern->stepCount));

            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            {
                if (!pattern->steps[ch][localStep]) continue;

                // Swap to this pattern's sample + settings if not already loaded
                if (songPlayerPatternId[ch] != clip.patternId)
                {
                    auto cit = songSampleCache.find(clip.patternId);
                    if (cit != songSampleCache.end()
                        && cit->second[(size_t)ch].getNumSamples() > 0)
                        players[ch].loadBuffer(cit->second[(size_t)ch]);
                    else
                        players[ch].clear();

                    // Apply per-pattern channel settings so each clip sounds right
                    players[ch].setVolume(pattern->channelVolume[ch]);
                    players[ch].setPan   (pattern->channelPan   [ch]);
                    players[ch].setPitch (channelBasePitch[ch] + pattern->channelPitch[ch]);

                    songPlayerPatternId[ch] = clip.patternId;
                }

                players[ch].trigger();
            }
        }
    }

    // ---- Melodic NoteEvent pass (beat-accurate, per-clip) ------------------
    {
        const double startBeatSong = (double)startSample / samplesPerBeat;
        const double endBeatSong   = (double)endSample   / samplesPerBeat;

        for (const auto& clip : project->playlistClips)
        {
            const Pattern* pat = findPatternById(clip.patternId);
            if (pat == nullptr || pat->stepCount <= 0) continue;

            const double patternBeats  = pat->stepCount * 0.25;
            const double clipStartBeat = clip.startBar * 4.0;
            const double clipEndBeat   = (clip.startBar + clip.lengthBars) * 4.0;

            if (endBeatSong <= clipStartBeat || startBeatSong >= clipEndBeat) continue;

            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            {
                if (project->channelTypes[ch] != ChannelType::Melodic) continue;

                for (const auto& note : pat->notes[ch])
                {
                    const double notePhase = std::fmod((double)note.startBeat, patternBeats);
                    const double relStart  = startBeatSong - clipStartBeat;
                    const double loopIdxD  = (relStart - notePhase) / patternBeats;
                    const int    kStart    = (loopIdxD < 0.0) ? 0 : (int)loopIdxD;

                    for (int k = kStart; k <= kStart + 1; ++k)
                    {
                        const double fireBeat = clipStartBeat + k * patternBeats + notePhase;
                        if (fireBeat < clipStartBeat || fireBeat >= clipEndBeat) continue;
                        if (fireBeat < startBeatSong  || fireBeat >= endBeatSong)  continue;

                        const SynthParams& sp = project->synthParams[(size_t)ch];
                        if (sp.enabled)
                        {
                            const int noteLenSamples = (int)(note.lengthBeats * samplesPerBeat);
                            polySynths[(size_t)ch].noteOn(note.pitch, note.velocity,
                                                          sampleRate, sp, noteLenSamples);
                        }
                        else
                        {
                            players[ch].setPitch(channelBasePitch[ch] + (float)(note.pitch - 60));
                            players[ch].trigger();
                        }
                    }
                }
            }
        }
    }

    // M9 — apply automation lanes at the midpoint of this buffer
    if (!project->automationLanes.empty())
    {
        const double midBeat = ((double)startSample + numSamples * 0.5) / samplesPerBeat;

        for (const auto& lane : project->automationLanes)
        {
            const float v = lane.evaluate(midBeat);

            if (lane.paramId == "masterVolume")
            {
                project->masterVolume = v;
            }
            else if (lane.paramId == "bpm")
            {
                bpm = (double)v;
            }
            else if (lane.paramId.startsWith("ch") && lane.paramId.endsWith("vol"))
            {
                const int ch = lane.paramId.substring(2, lane.paramId.length() - 3).getIntValue();
                if (ch >= 0 && ch < 16)
                    players[ch].setVolume(v);
            }
        }
    }

    mixToOutput(buffer, numSamples);

    songSamplePosition += numSamples;
}

// M5 — route channels through mixer tracks and sum to output
void AudioEngine::mixToOutput(juce::AudioBuffer<float>& output, int numSamples)
{
    // Clear mixer track buses
    for (auto& tb : mixerTrackBufs)
        tb.clear();

    // Render each channel into staging, accumulate into its assigned mixer track
    for (int ch = 0; ch < 16; ++ch)
    {
        stagingBuf.clear();

        // M13 — route to PolySynth if synth is enabled for this channel
        const bool useSynth = (project != nullptr) &&
                               project->synthParams[(size_t)ch].enabled;

        if (useSynth)
        {
            float* L = stagingBuf.getWritePointer(0);
            float* R = stagingBuf.getWritePointer(1);
            polySynths[(size_t)ch].renderNextBlock(L, R, numSamples, sampleRate,
                                                    project->synthParams[(size_t)ch]);
        }
        else
        {
            players[ch].renderNextBlock(stagingBuf, numSamples);
        }

        const int trackIdx = (project != nullptr)
                             ? juce::jlimit(0, 7, project->channelMixerRouting[ch])
                             : 0;

        for (int s = 0; s < numSamples; ++s)
        {
            mixerTrackBufs[(size_t)trackIdx].addSample(0, s, stagingBuf.getSample(0, s));
            mixerTrackBufs[(size_t)trackIdx].addSample(1, s, stagingBuf.getSample(1, s));
        }
    }

    // M14 — apply FX chains per mixer track bus
    for (int t = 0; t < 8; ++t)
    {
        const FXParams& fp = (project != nullptr)
                              ? project->fxParams[(size_t)t]
                              : FXParams{};
        fxChains[(size_t)t].processBlock(mixerTrackBufs[(size_t)t], numSamples, fp, bpm);
    }

    // Check if any track is soloed
    bool anySoloed = false;
    if (project != nullptr)
        for (const auto& mt : project->mixerTracks)
            if (mt.soloed) { anySoloed = true; break; }

    const float mv  = project ? project->masterVolume : 1.0f;
    const float mp  = project ? project->masterPan    : 0.0f;
    const float mAng = (mp + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    const float mL  = mv * std::cos(mAng);
    const float mR  = mv * std::sin(mAng);

    // Mix each track bus into output with track volume/pan
    for (int t = 0; t < 8; ++t)
    {
        if (project != nullptr && t < (int)project->mixerTracks.size())
        {
            const auto& mt = project->mixerTracks[(size_t)t];
            if (mt.muted || (anySoloed && !mt.soloed)) continue;

            const float ang = (mt.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float L   = mt.volume * std::cos(ang);
            const float R   = mt.volume * std::sin(ang);

            for (int s = 0; s < numSamples; ++s)
            {
                if (output.getNumChannels() > 0)
                    output.addSample(0, s, mixerTrackBufs[(size_t)t].getSample(0, s) * L * mL);
                if (output.getNumChannels() > 1)
                    output.addSample(1, s, mixerTrackBufs[(size_t)t].getSample(1, s) * R * mR);
            }
        }
        else
        {
            // No mixer track data: pass through at unity
            for (int s = 0; s < numSamples; ++s)
            {
                if (output.getNumChannels() > 0)
                    output.addSample(0, s, mixerTrackBufs[(size_t)t].getSample(0, s) * mL);
                if (output.getNumChannels() > 1)
                    output.addSample(1, s, mixerTrackBufs[(size_t)t].getSample(1, s) * mR);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// M10 — Offline render
// ---------------------------------------------------------------------------

bool AudioEngine::renderToFile(const juce::File& outputFile, PlayMode mode, int numBars)
{
    if (sampleRate <= 0.0 || numBars <= 0)
        return false;

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
    for (auto& p : players)   p.reset();
    for (auto& s : polySynths) s.reset();
    for (auto& fx : fxChains)  fx.reset();

    // Set up playback state for offline pass
    const double samplesPerBar = (sampleRate * 60.0 / bpm) * 4.0;
    const long   totalSamples  = (long)std::ceil((double)numBars * samplesPerBar);

    if (mode == PlayMode::Pattern)
    {
        patternBeatPos = 0.0;   // must reset before offline render
        sequencer.start();
    }
    else
    {
        buildSongSampleCache();
        songSamplePosition = 0;
        songPlaying        = true;
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
        songPlaying = false;

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
