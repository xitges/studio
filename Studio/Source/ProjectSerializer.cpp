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

    projectOut = std::move(loaded);
    return true;
}
