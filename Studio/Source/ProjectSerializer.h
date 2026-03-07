/*
  ==============================================================================

    ProjectSerializer.h
    Author:  홍준영

    XML save / load for the Studio project format (.studioproj).

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "ProjectModel.h"

class ProjectSerializer
{
public:
    // Returns true on success.
    static bool save(const Project& project, const juce::File& file);
    static bool load(juce::File& file, Project& projectOut);

    // File chooser helpers (async — keep the chooser alive via shared_ptr)
    static constexpr const char* fileExtension    = "studioproj";
    static constexpr const char* fileWildcard     = "*.studioproj";
    static constexpr const char* fileTypeName     = "Studio Project";

private:
    ProjectSerializer() = delete;
};
