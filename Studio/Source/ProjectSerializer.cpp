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

    // ---- Channel rack (count + names)
    auto* crEl = root.createNewChildElement("ChannelRack");
    crEl->setAttribute("count", project.channelCount);
    for (int ch = 0; ch < project.channelCount && ch < 16; ++ch)
        crEl->setAttribute("name" + juce::String(ch), project.channelNames[ch]);

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

    // ---- Channel types + routing (M3/M5)
    auto* chCfgEl = root.createNewChildElement("ChannelConfig");
    for (int ch = 0; ch < 16; ++ch)
    {
        auto* ctEl = chCfgEl->createNewChildElement("Ch");
        ctEl->setAttribute("i",        ch);
        ctEl->setAttribute("type",     (int)project.channelTypes[ch]);
        ctEl->setAttribute("mixTrack", project.channelMixerRouting[ch]);
    }

    // ---- Playlist tracks (M11)
    auto* plTracksEl = root.createNewChildElement("PlaylistTracks");
    for (const auto& t : project.playlistTracks)
    {
        auto* tEl = plTracksEl->createNewChildElement("Track");
        tEl->setAttribute("name",   t.name);
        tEl->setAttribute("colour", (int)t.colour.getARGB());
    }

    // ---- SynthParams (M13)
    auto* synthEl = root.createNewChildElement("SynthParams");
    for (int ch = 0; ch < 16; ++ch)
    {
        const auto& sp = project.synthParams[(size_t)ch];
        auto* sEl = synthEl->createNewChildElement("Ch");
        sEl->setAttribute("i",          ch);
        sEl->setAttribute("enabled",    sp.enabled   ? 1 : 0);
        sEl->setAttribute("waveform",   sp.waveform);
        sEl->setAttribute("attack",     (double)sp.attack);
        sEl->setAttribute("decay",      (double)sp.decay);
        sEl->setAttribute("sustain",    (double)sp.sustain);
        sEl->setAttribute("release",    (double)sp.release);
        sEl->setAttribute("cutoff",     (double)sp.cutoff);
        sEl->setAttribute("resonance",  (double)sp.resonance);
        sEl->setAttribute("lfoRate",    (double)sp.lfoRate);
        sEl->setAttribute("lfoDepth",   (double)sp.lfoDepth);
        sEl->setAttribute("lfoTarget",  sp.lfoTarget);
    }

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

    // ---- Patterns
    if (auto* patternsEl = xml->getChildByName("Patterns"))
    {
        for (auto* patEl : patternsEl->getChildIterator())
        {
            Pattern pat;
            pat.id        = patEl->getIntAttribute("id",        0);
            pat.name      = patEl->getStringAttribute("name",   "Pattern");
            pat.stepCount = patEl->getIntAttribute("stepCount", 16);
            pat.lengthBars= patEl->getIntAttribute("lengthBars",1);

            for (auto* chEl : patEl->getChildIterator())
            {
                const int ch = chEl->getIntAttribute("i", -1);
                if (ch < 0 || ch >= Pattern::kMaxChannels) continue;

                const juce::String bits = chEl->getStringAttribute("steps");
                for (int s = 0; s < juce::jmin(bits.length(), Pattern::kMaxSteps); ++s)
                    pat.steps[ch][s] = (bits[s] == '1');

                pat.samplePaths[ch]    = chEl->getStringAttribute("samplePath");
                pat.channelVolume[ch]  = (float)chEl->getDoubleAttribute("volume", 0.8);
                pat.channelPan[ch]     = (float)chEl->getDoubleAttribute("pan",    0.0);
                pat.channelPitch[ch]   = (float)chEl->getDoubleAttribute("pitch",  0.0);
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

    // ---- Channel rack (count + names)
    if (auto* crEl = xml->getChildByName("ChannelRack"))
    {
        loaded.channelCount = juce::jlimit(1, 16, crEl->getIntAttribute("count", 3));
        for (int ch = 0; ch < loaded.channelCount; ++ch)
            loaded.channelNames[ch] = crEl->getStringAttribute("name" + juce::String(ch));
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
            clip.patternId  = clipEl->getIntAttribute("patternId",  1);
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

    // ---- Channel types + routing (M3/M5)
    if (auto* chCfgEl = xml->getChildByName("ChannelConfig"))
    {
        for (auto* ctEl : chCfgEl->getChildIterator())
        {
            const int ch = ctEl->getIntAttribute("i", -1);
            if (ch < 0 || ch >= 16) continue;
            loaded.channelTypes[ch]        = (ChannelType)ctEl->getIntAttribute("type",     0);
            loaded.channelMixerRouting[ch] = ctEl->getIntAttribute("mixTrack", ch % 8);
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

    // ---- SynthParams (M13)
    if (auto* synthEl = xml->getChildByName("SynthParams"))
    {
        for (auto* sEl : synthEl->getChildIterator())
        {
            const int ch = sEl->getIntAttribute("i", -1);
            if (ch < 0 || ch >= 16) continue;
            auto& sp       = loaded.synthParams[(size_t)ch];
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

    projectOut = std::move(loaded);
    return true;
}
