/*
  ==============================================================================

    ProjectSerializer.cpp
    Author:  홍준영

  ==============================================================================
*/

#include "ProjectSerializer.h"

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

bool ProjectSerializer::save(const Project& project, const juce::File& file)
{
    juce::XmlElement root("StudioProject");
    root.setAttribute("version", 1);
    root.setAttribute("bpm",     project.bpm);
    root.setAttribute("playMode", (int)project.playMode);
    root.setAttribute("activePatternId", project.activePatternId);
    root.setAttribute("keyTonic", project.keySignature.tonic);
    root.setAttribute("keyScale", project.keySignature.scale == ScaleType::Minor ? "minor" : "major");

    // ---- Patterns
    auto* patternsEl = root.createNewChildElement("Patterns");
    for (const auto& pat : project.patterns)
    {
        auto* patEl = patternsEl->createNewChildElement("Pattern");
        patEl->setAttribute("id",         pat.id);
        patEl->setAttribute("name",       pat.name);
        patEl->setAttribute("stepCount",  pat.stepCount);
        patEl->setAttribute("lengthBars", pat.lengthBars);

        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            juce::String bits;
            bits.preallocateBytes(Pattern::kMaxSteps + 1);
            for (int s = 0; s < Pattern::kMaxSteps; ++s)
                bits += pat.steps[ch][s] ? "1" : "0";

            auto* chEl = patEl->createNewChildElement("Ch");
            chEl->setAttribute("i",          ch);
            chEl->setAttribute("steps",      bits);
            chEl->setAttribute("samplePath", pat.samplePaths[ch]);
            chEl->setAttribute("volume",     (double)pat.channelVolume[ch]);
            chEl->setAttribute("pan",        (double)pat.channelPan[ch]);
            chEl->setAttribute("pitch",      (double)pat.channelPitch[ch]);
            chEl->setAttribute("type",       (int)pat.channelTypes[ch]);
            chEl->setAttribute("name",       pat.channelNames[ch]);

            // SynthParams per channel
            const auto& sp = pat.synthParams[ch];
            chEl->setAttribute("spEnabled",   sp.enabled   ? 1 : 0);
            chEl->setAttribute("spWaveform",  sp.waveform);
            chEl->setAttribute("spAttack",    (double)sp.attack);
            chEl->setAttribute("spDecay",     (double)sp.decay);
            chEl->setAttribute("spSustain",   (double)sp.sustain);
            chEl->setAttribute("spRelease",   (double)sp.release);
            chEl->setAttribute("spCutoff",    (double)sp.cutoff);
            chEl->setAttribute("spResonance", (double)sp.resonance);
            chEl->setAttribute("spLfoRate",   (double)sp.lfoRate);
            chEl->setAttribute("spLfoDepth",  (double)sp.lfoDepth);
            chEl->setAttribute("spLfoTarget", sp.lfoTarget);
            chEl->setAttribute("spDdspEnabled",    sp.ddspAuto.enabled ? 1 : 0);
            chEl->setAttribute("spDdspAmount",     (double)sp.ddspAuto.amount);
            chEl->setAttribute("spDdspBrightness", (double)sp.ddspAuto.brightness);
            chEl->setAttribute("spDdspMotion",     (double)sp.ddspAuto.motion);
            chEl->setAttribute("mixRoute",    pat.channelMixerRouting[ch]);
        }

        // NoteEvents (M3)
        auto* notesEl = patEl->createNewChildElement("Notes");
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            for (const auto& note : pat.notes[ch])
            {
                auto* nEl = notesEl->createNewChildElement("Note");
                nEl->setAttribute("ch",    ch);
                nEl->setAttribute("pitch", note.pitch);
                nEl->setAttribute("start", (double)note.startBeat);
                nEl->setAttribute("len",   (double)note.lengthBeats);
                nEl->setAttribute("vel",   (double)note.velocity);
            }
        }
    }

    // ---- Channel rack (count only; names/types/synthParams now stored per-Pattern/Ch)
    auto* crEl = root.createNewChildElement("ChannelRack");
    crEl->setAttribute("count", project.channelCount);

    // ---- Playlist clips
    auto* clipsEl = root.createNewChildElement("PlaylistClips");
    for (const auto& clip : project.playlistClips)
    {
        auto* clipEl = clipsEl->createNewChildElement("Clip");
        clipEl->setAttribute("id",         clip.id);
        clipEl->setAttribute("patternId",  clip.patternId);
        clipEl->setAttribute("name",       clip.name);
        clipEl->setAttribute("trackIndex", clip.trackIndex);
        clipEl->setAttribute("startBar",   clip.startBar);
        clipEl->setAttribute("lengthBars", clip.lengthBars);
    }

    // ---- MixerTracks (M5)
    auto* mixEl = root.createNewChildElement("MixerTracks");
    mixEl->setAttribute("masterVol", (double)project.masterVolume);
    mixEl->setAttribute("masterPan", (double)project.masterPan);
    for (const auto& mt : project.mixerTracks)
    {
        auto* mEl = mixEl->createNewChildElement("Track");
        mEl->setAttribute("name",   mt.name);
        mEl->setAttribute("volume", (double)mt.volume);
        mEl->setAttribute("pan",    (double)mt.pan);
        mEl->setAttribute("muted",  mt.muted  ? 1 : 0);
        mEl->setAttribute("soloed", mt.soloed ? 1 : 0);
    }

    // ---- Channel config placeholder (routing now stored per-Pattern/Ch)
    root.createNewChildElement("ChannelConfig");

    // ---- Playlist tracks (M11)
    auto* plTracksEl = root.createNewChildElement("PlaylistTracks");
    for (const auto& t : project.playlistTracks)
    {
        auto* tEl = plTracksEl->createNewChildElement("Track");
        tEl->setAttribute("name",   t.name);
        tEl->setAttribute("colour", (int)t.colour.getARGB());
    }

    // SynthParams are now stored per-Pattern/Ch — no separate section needed

    // ---- FXParams (M14)
    auto* fxEl = root.createNewChildElement("FXParams");
    for (int t = 0; t < 8; ++t)
    {
        const auto& fp = project.fxParams[(size_t)t];
        auto* fEl = fxEl->createNewChildElement("Track");
        fEl->setAttribute("i",              t);
        fEl->setAttribute("compEnabled",    fp.compEnabled   ? 1 : 0);
        fEl->setAttribute("compThreshDB",   (double)fp.compThreshDB);
        fEl->setAttribute("compRatio",      (double)fp.compRatio);
        fEl->setAttribute("compAttackMs",   (double)fp.compAttackMs);
        fEl->setAttribute("compReleaseMs",  (double)fp.compReleaseMs);
        fEl->setAttribute("delayEnabled",   fp.delayEnabled  ? 1 : 0);
        fEl->setAttribute("delayBeats",     (double)fp.delayBeats);
        fEl->setAttribute("delayFeedback",  (double)fp.delayFeedback);
        fEl->setAttribute("delayMix",       (double)fp.delayMix);
        fEl->setAttribute("reverbEnabled",  fp.reverbEnabled ? 1 : 0);
        fEl->setAttribute("reverbRoom",     (double)fp.reverbRoom);
        fEl->setAttribute("reverbDamp",     (double)fp.reverbDamp);
        fEl->setAttribute("reverbWet",      (double)fp.reverbWet);
        fEl->setAttribute("reverbWidth",    (double)fp.reverbWidth);
    }

    // ---- Launchpad pad assignments
    auto* lpEl = root.createNewChildElement("Launchpad");
    for (int i = 0; i < 64; ++i)
    {
        const auto& pad = project.launchpadPads[(size_t)i];
        if (pad.filePath.isNotEmpty())
        {
            auto* padEl = lpEl->createNewChildElement("Pad");
            padEl->setAttribute("i",      i);
            padEl->setAttribute("file",   pad.filePath);
            padEl->setAttribute("volume", (double)pad.volume);
            padEl->setAttribute("pitch",  (double)pad.pitch);
        }
    }

    // ---- M8: Instrument plugins
    auto* instrPlugEl = root.createNewChildElement("InstrumentPlugins");
    for (int ch = 0; ch < 16; ++ch)
    {
        const auto& slot = project.channelInstrumentPlugins[(size_t)ch];
        if (slot.pluginId.isEmpty()) continue;
        auto* pEl = instrPlugEl->createNewChildElement("Ch");
        pEl->setAttribute("i",       ch);
        pEl->setAttribute("id",      slot.pluginId);
        pEl->setAttribute("enabled", slot.enabled ? 1 : 0);
        pEl->setAttribute("state",   slot.pluginStateBase64);
    }

    // ---- M9: Automation lanes
    auto* autoEl = root.createNewChildElement("Automation");
    for (const auto& lane : project.automationLanes)
    {
        auto* laneEl = autoEl->createNewChildElement("Lane");
        laneEl->setAttribute("paramId", lane.paramId);
        laneEl->setAttribute("minVal",  (double)lane.minVal);
        laneEl->setAttribute("maxVal",  (double)lane.maxVal);
        for (const auto& pt : lane.points)
        {
            auto* ptEl = laneEl->createNewChildElement("Pt");
            ptEl->setAttribute("beat",  pt.beat);
            ptEl->setAttribute("value", (double)pt.value);
        }
    }

    auto* synthPresetEl = root.createNewChildElement("CustomSynthPresets");
    for (const auto& preset : project.customSynthPresets)
    {
        auto* pEl = synthPresetEl->createNewChildElement("Preset");
        pEl->setAttribute("name", preset.name);
        pEl->setAttribute("enabled", preset.params.enabled ? 1 : 0);
        pEl->setAttribute("waveform", preset.params.waveform);
        pEl->setAttribute("attack", (double)preset.params.attack);
        pEl->setAttribute("decay", (double)preset.params.decay);
        pEl->setAttribute("sustain", (double)preset.params.sustain);
        pEl->setAttribute("release", (double)preset.params.release);
        pEl->setAttribute("cutoff", (double)preset.params.cutoff);
        pEl->setAttribute("resonance", (double)preset.params.resonance);
        pEl->setAttribute("lfoRate", (double)preset.params.lfoRate);
        pEl->setAttribute("lfoDepth", (double)preset.params.lfoDepth);
        pEl->setAttribute("lfoTarget", preset.params.lfoTarget);
        pEl->setAttribute("ddspEnabled", preset.params.ddspAuto.enabled ? 1 : 0);
        pEl->setAttribute("ddspAmount", (double)preset.params.ddspAuto.amount);
        pEl->setAttribute("ddspBrightness", (double)preset.params.ddspAuto.brightness);
        pEl->setAttribute("ddspMotion", (double)preset.params.ddspAuto.motion);
    }

    return root.writeTo(file);
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

bool ProjectSerializer::load(juce::File& file, Project& projectOut)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || xml->getTagName() != "StudioProject")
        return false;

    Project loaded;
    loaded.bpm = xml->getDoubleAttribute("bpm", 70.0);
    loaded.playMode = (PlayMode)juce::jlimit(0, 1, xml->getIntAttribute("playMode", (int)PlayMode::Pattern));
    loaded.activePatternId = xml->getIntAttribute("activePatternId", 1);
    loaded.keySignature.tonic = juce::jlimit(0, 11, xml->getIntAttribute("keyTonic", 0));
    loaded.keySignature.scale = xml->getStringAttribute("keyScale", "major").equalsIgnoreCase("minor")
        ? ScaleType::Minor
        : ScaleType::Major;

    // ---- Patterns
    if (auto* patternsEl = xml->getChildByName("Patterns"))
    {
        for (auto* patEl : patternsEl->getChildIterator())
        {
            Pattern pat;
            pat.id        = patEl->getIntAttribute("id",        0);
            pat.name      = patEl->getStringAttribute("name",   "Pattern");
            pat.stepCount = juce::jlimit(1, Pattern::kMaxSteps,
                                         patEl->getIntAttribute("stepCount", 16));
            pat.lengthBars= patEl->getIntAttribute("lengthBars",1);

            for (auto* chEl : patEl->getChildIterator())
            {
                const int ch = chEl->getIntAttribute("i", -1);
                if (ch < 0 || ch >= Pattern::kMaxChannels) continue;

                const juce::String bits = chEl->getStringAttribute("steps");
                for (int s = 0; s < juce::jmin(bits.length(), Pattern::kMaxSteps); ++s)
                    pat.steps[ch][s] = (bits[s] == '1');

                pat.samplePaths[ch]   = chEl->getStringAttribute("samplePath");
                pat.channelVolume[ch] = (float)chEl->getDoubleAttribute("volume", 0.8);
                pat.channelPan[ch]    = (float)chEl->getDoubleAttribute("pan",    0.0);
                pat.channelPitch[ch]  = (float)chEl->getDoubleAttribute("pitch",  0.0);
                pat.channelTypes[ch]  = (ChannelType)chEl->getIntAttribute("type", 0);
                pat.channelNames[ch]  = chEl->getStringAttribute(
                    "name", "Channel " + juce::String(ch + 1));

                pat.channelMixerRouting[ch] = chEl->getIntAttribute("mixRoute", ch % 8);

                // SynthParams per channel (new format; fall back to defaults if absent)
                auto& sp       = pat.synthParams[ch];
                sp.enabled     = chEl->getIntAttribute("spEnabled",   0) != 0;
                sp.waveform    = chEl->getIntAttribute("spWaveform",  1);
                sp.attack      = (float)chEl->getDoubleAttribute("spAttack",    5.0);
                sp.decay       = (float)chEl->getDoubleAttribute("spDecay",     80.0);
                sp.sustain     = (float)chEl->getDoubleAttribute("spSustain",   0.6);
                sp.release     = (float)chEl->getDoubleAttribute("spRelease",   200.0);
                sp.cutoff      = (float)chEl->getDoubleAttribute("spCutoff",    4000.0);
                sp.resonance   = (float)chEl->getDoubleAttribute("spResonance", 0.3);
                sp.lfoRate     = (float)chEl->getDoubleAttribute("spLfoRate",   2.0);
                sp.lfoDepth    = (float)chEl->getDoubleAttribute("spLfoDepth",  0.0);
                sp.lfoTarget   = chEl->getIntAttribute("spLfoTarget", 0);
                sp.ddspAuto.enabled    = chEl->getIntAttribute("spDdspEnabled", 0) != 0;
                sp.ddspAuto.amount     = (float)chEl->getDoubleAttribute("spDdspAmount", 0.0);
                sp.ddspAuto.brightness = (float)chEl->getDoubleAttribute("spDdspBrightness", 0.5);
                sp.ddspAuto.motion     = (float)chEl->getDoubleAttribute("spDdspMotion", 0.25);
            }

            // NoteEvents (M3)
            if (auto* notesEl = patEl->getChildByName("Notes"))
            {
                for (auto* nEl : notesEl->getChildIterator())
                {
                    const int ch = nEl->getIntAttribute("ch", -1);
                    if (ch < 0 || ch >= Pattern::kMaxChannels) continue;

                    NoteEvent note;
                    note.pitch       = nEl->getIntAttribute("pitch",  60);
                    note.startBeat   = (float)nEl->getDoubleAttribute("start", 0.0);
                    note.lengthBeats = (float)nEl->getDoubleAttribute("len",   0.25);
                    note.velocity    = (float)nEl->getDoubleAttribute("vel",   0.8);
                    pat.notes[ch].push_back(note);
                }
            }

            loaded.patterns.push_back(pat);
        }
    }

    // ---- Channel rack (count only — names/types live in Pattern since refactor)
    if (auto* crEl = xml->getChildByName("ChannelRack"))
    {
        loaded.channelCount = juce::jlimit(1, 16, crEl->getIntAttribute("count", 3));

        // Legacy: old files stored names here; migrate them into every pattern
        for (int ch = 0; ch < loaded.channelCount; ++ch)
        {
            const juce::String legacyName = crEl->getStringAttribute("name" + juce::String(ch));
            if (legacyName.isNotEmpty())
                for (auto& p : loaded.patterns)
                    p.channelNames[ch] = legacyName;
        }
    }
    else
    {
        // Old file: infer channel count from pattern data (highest channel with a sample
        // or any active step)
        int maxCh = 2; // minimum 3 channels
        for (const auto& pat : loaded.patterns)
        {
            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            {
                if (pat.samplePaths[ch].isNotEmpty())
                {
                    maxCh = juce::jmax(maxCh, ch);
                    continue;
                }
                for (int s = 0; s < pat.stepCount; ++s)
                {
                    if (pat.steps[ch][s]) { maxCh = juce::jmax(maxCh, ch); break; }
                }
            }
        }
        loaded.channelCount = maxCh + 1;
    }

    // Ensure at least one pattern exists
    if (loaded.patterns.empty())
    {
        Pattern def;
        def.id = 1; def.name = "Pattern 1"; def.stepCount = 16;
        loaded.patterns.push_back(def);
    }

    // ---- Playlist clips
    if (auto* clipsEl = xml->getChildByName("PlaylistClips"))
    {
        for (auto* clipEl : clipsEl->getChildIterator())
        {
            PlaylistClip clip;
            clip.id         = clipEl->getIntAttribute("id",         0);
            clip.patternId  = clipEl->getIntAttribute("patternId", -1);
            clip.name       = clipEl->getStringAttribute("name",    "Clip");
            clip.trackIndex = clipEl->getIntAttribute("trackIndex", 0);
            clip.startBar   = (float)clipEl->getDoubleAttribute("startBar",   0.0);
            clip.lengthBars = (float)clipEl->getDoubleAttribute("lengthBars", 4.0);
            loaded.playlistClips.push_back(clip);
        }
    }

    // ---- MixerTracks (M5)
    if (auto* mixEl = xml->getChildByName("MixerTracks"))
    {
        loaded.masterVolume = (float)mixEl->getDoubleAttribute("masterVol", 1.0);
        loaded.masterPan    = (float)mixEl->getDoubleAttribute("masterPan", 0.0);
        for (auto* mEl : mixEl->getChildIterator())
        {
            MixerTrack mt;
            mt.name   = mEl->getStringAttribute("name",   "Track");
            mt.volume = (float)mEl->getDoubleAttribute("volume", 1.0);
            mt.pan    = (float)mEl->getDoubleAttribute("pan",    0.0);
            mt.muted  = mEl->getIntAttribute("muted",  0) != 0;
            mt.soloed = mEl->getIntAttribute("soloed", 0) != 0;
            loaded.mixerTracks.push_back(mt);
        }
    }

    // ---- Channel config (legacy migration; routing now stored per-Pattern/Ch)
    if (auto* chCfgEl = xml->getChildByName("ChannelConfig"))
    {
        for (auto* ctEl : chCfgEl->getChildIterator())
        {
            const int ch = ctEl->getIntAttribute("i", -1);
            if (ch < 0 || ch >= 16) continue;

            // Legacy: old files stored mixTrack here; migrate to all patterns
            if (ctEl->hasAttribute("mixTrack"))
            {
                const int route = ctEl->getIntAttribute("mixTrack", ch % 8);
                for (auto& p : loaded.patterns) p.channelMixerRouting[ch] = route;
            }

            // Legacy: old files stored type here; migrate to all patterns
            if (ctEl->hasAttribute("type"))
            {
                const auto t = (ChannelType)ctEl->getIntAttribute("type", 0);
                for (auto& p : loaded.patterns) p.channelTypes[ch] = t;
            }
        }
    }

    // ---- Playlist tracks (M11)
    if (auto* plTracksEl = xml->getChildByName("PlaylistTracks"))
    {
        for (auto* tEl : plTracksEl->getChildIterator())
        {
            PlaylistTrack t;
            t.name   = tEl->getStringAttribute("name", "Track");
            t.colour = juce::Colour((juce::uint32)(unsigned int)
                           tEl->getIntAttribute("colour",
                               (int)juce::Colour(0xff3498db).getARGB()));
            loaded.playlistTracks.push_back(t);
        }
    }

    // Legacy SynthParams section: migrate to all patterns (new files store sp per-Ch)
    if (auto* synthEl = xml->getChildByName("SynthParams"))
    {
        for (auto* sEl : synthEl->getChildIterator())
        {
            const int ch = sEl->getIntAttribute("i", -1);
            if (ch < 0 || ch >= 16) continue;
            SynthParams sp;
            sp.enabled     = sEl->getIntAttribute("enabled",   0) != 0;
            sp.waveform    = sEl->getIntAttribute("waveform",  1);
            sp.attack      = (float)sEl->getDoubleAttribute("attack",    5.0);
            sp.decay       = (float)sEl->getDoubleAttribute("decay",     80.0);
            sp.sustain     = (float)sEl->getDoubleAttribute("sustain",   0.6);
            sp.release     = (float)sEl->getDoubleAttribute("release",   200.0);
            sp.cutoff      = (float)sEl->getDoubleAttribute("cutoff",    4000.0);
            sp.resonance   = (float)sEl->getDoubleAttribute("resonance", 0.3);
            sp.lfoRate     = (float)sEl->getDoubleAttribute("lfoRate",   2.0);
            sp.lfoDepth    = (float)sEl->getDoubleAttribute("lfoDepth",  0.0);
            sp.lfoTarget   = sEl->getIntAttribute("lfoTarget", 0);
            for (auto& p : loaded.patterns) p.synthParams[ch] = sp;
        }
    }

    // ---- FXParams (M14)
    if (auto* fxEl = xml->getChildByName("FXParams"))
    {
        for (auto* fEl : fxEl->getChildIterator())
        {
            const int t = fEl->getIntAttribute("i", -1);
            if (t < 0 || t >= 8) continue;
            auto& fp          = loaded.fxParams[(size_t)t];
            fp.compEnabled    = fEl->getIntAttribute("compEnabled",   0) != 0;
            fp.compThreshDB   = (float)fEl->getDoubleAttribute("compThreshDB",  -12.0);
            fp.compRatio      = (float)fEl->getDoubleAttribute("compRatio",       4.0);
            fp.compAttackMs   = (float)fEl->getDoubleAttribute("compAttackMs",   10.0);
            fp.compReleaseMs  = (float)fEl->getDoubleAttribute("compReleaseMs", 100.0);
            fp.delayEnabled   = fEl->getIntAttribute("delayEnabled",  0) != 0;
            fp.delayBeats     = (float)fEl->getDoubleAttribute("delayBeats",    0.5);
            fp.delayFeedback  = (float)fEl->getDoubleAttribute("delayFeedback", 0.3);
            fp.delayMix       = (float)fEl->getDoubleAttribute("delayMix",      0.25);
            fp.reverbEnabled  = fEl->getIntAttribute("reverbEnabled", 0) != 0;
            fp.reverbRoom     = (float)fEl->getDoubleAttribute("reverbRoom",    0.5);
            fp.reverbDamp     = (float)fEl->getDoubleAttribute("reverbDamp",    0.5);
            fp.reverbWet      = (float)fEl->getDoubleAttribute("reverbWet",     0.25);
            fp.reverbWidth    = (float)fEl->getDoubleAttribute("reverbWidth",   1.0);
        }
    }

    // ---- Launchpad pad assignments
    if (auto* lpEl = xml->getChildByName("Launchpad"))
    {
        for (auto* padEl : lpEl->getChildIterator())
        {
            const int i = padEl->getIntAttribute("i", -1);
            if (i < 0 || i >= 64) continue;
            loaded.launchpadPads[(size_t)i].filePath =
                padEl->getStringAttribute("file");
            loaded.launchpadPads[(size_t)i].volume =
                (float)padEl->getDoubleAttribute("volume", 0.8);
            loaded.launchpadPads[(size_t)i].pitch =
                (float)padEl->getDoubleAttribute("pitch",  0.0);
        }
    }

    // ---- M8: Instrument plugins
    if (auto* instrPlugEl = xml->getChildByName("InstrumentPlugins"))
    {
        for (auto* pEl : instrPlugEl->getChildIterator())
        {
            const int ch = pEl->getIntAttribute("i", -1);
            if (ch < 0 || ch >= 16) continue;
            loaded.channelInstrumentPlugins[(size_t)ch].pluginId          =
                pEl->getStringAttribute("id");
            loaded.channelInstrumentPlugins[(size_t)ch].enabled           =
                (pEl->getIntAttribute("enabled", 0) != 0);
            loaded.channelInstrumentPlugins[(size_t)ch].pluginStateBase64 =
                pEl->getStringAttribute("state");
        }
    }

    // ---- M9: Automation lanes
    if (auto* autoEl = xml->getChildByName("Automation"))
    {
        for (auto* laneEl : autoEl->getChildIterator())
        {
            AutomationLane lane;
            lane.paramId = laneEl->getStringAttribute("paramId");
            lane.minVal  = (float)laneEl->getDoubleAttribute("minVal", 0.0);
            lane.maxVal  = (float)laneEl->getDoubleAttribute("maxVal", 1.0);
            for (auto* ptEl : laneEl->getChildIterator())
            {
                AutomationPoint pt;
                pt.beat  = ptEl->getDoubleAttribute("beat",  0.0);
                pt.value = (float)ptEl->getDoubleAttribute("value", 0.0);
                lane.points.push_back(pt);
            }
            loaded.automationLanes.push_back(lane);
        }
    }

    if (auto* synthPresetEl = xml->getChildByName("CustomSynthPresets"))
    {
        for (auto* pEl : synthPresetEl->getChildIterator())
        {
            SynthPresets::Preset preset;
            preset.name = pEl->getStringAttribute("name");
            preset.params.enabled = pEl->getIntAttribute("enabled", 1) != 0;
            preset.params.waveform = pEl->getIntAttribute("waveform", 1);
            preset.params.attack = (float)pEl->getDoubleAttribute("attack", 5.0);
            preset.params.decay = (float)pEl->getDoubleAttribute("decay", 80.0);
            preset.params.sustain = (float)pEl->getDoubleAttribute("sustain", 0.6);
            preset.params.release = (float)pEl->getDoubleAttribute("release", 200.0);
            preset.params.cutoff = (float)pEl->getDoubleAttribute("cutoff", 4000.0);
            preset.params.resonance = (float)pEl->getDoubleAttribute("resonance", 0.3);
            preset.params.lfoRate = (float)pEl->getDoubleAttribute("lfoRate", 2.0);
            preset.params.lfoDepth = (float)pEl->getDoubleAttribute("lfoDepth", 0.0);
            preset.params.lfoTarget = pEl->getIntAttribute("lfoTarget", 0);
            preset.params.ddspAuto.enabled = pEl->getIntAttribute("ddspEnabled", 0) != 0;
            preset.params.ddspAuto.amount = (float)pEl->getDoubleAttribute("ddspAmount", 0.0);
            preset.params.ddspAuto.brightness = (float)pEl->getDoubleAttribute("ddspBrightness", 0.5);
            preset.params.ddspAuto.motion = (float)pEl->getDoubleAttribute("ddspMotion", 0.25);

            if (preset.name.isNotEmpty())
                loaded.customSynthPresets.push_back(std::move(preset));
        }
    }

    projectOut = std::move(loaded);
    return true;
}
