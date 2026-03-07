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
            chEl->setAttribute("i",     ch);
            chEl->setAttribute("steps", bits);
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

    projectOut = std::move(loaded);
    return true;
}
