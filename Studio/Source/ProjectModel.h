/*
  ==============================================================================

    ProjectModel.h
    Created: 6 Mar 2026
    Author:  홍준영

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

enum class PlayMode    { Pattern, Song };
enum class ChannelType { Drum, Melodic };   // M3

// M3 — one note in a melodic channel's piano roll
struct NoteEvent
{
    int   pitch       = 60;    // MIDI note number (C4 = 60)
    float startBeat   = 0.0f;  // beat position within the pattern (0 = start)
    float lengthBeats = 0.25f; // duration in beats (0.25 = 16th note)
    float velocity    = 0.8f;  // 0.0 – 1.0
};

// M5 — one strip in the mixer
struct MixerTrack
{
    juce::String name   = "Track";
    float        volume = 1.0f;
    float        pan    = 0.0f;
    bool         muted  = false;
    bool         soloed = false;
};

// M11 — one row in the playlist timeline
struct PlaylistTrack
{
    juce::String name   = "Track";
    juce::Colour colour = juce::Colour(0xff3498db);
};

struct Pattern
{
    int          id         = 0;
    juce::String name       = "Pattern 1";
    int          lengthBars = 1;
    int          stepCount  = 16;

    static constexpr int kMaxChannels = 16;
    static constexpr int kMaxSteps    = 64;

    bool steps[kMaxChannels][kMaxSteps] {};

    // M3 — per-channel note lists (for Melodic channels)
    std::vector<NoteEvent> notes[kMaxChannels];
};

struct PlaylistClip
{
    int          id         = 0;
    int          patternId  = 0;
    int          trackIndex = 0;
    float        startBar   = 0.0f;
    float        lengthBars = 1.0f;

    juce::String name;
};

struct Project
{
    double bpm = 140.0;

    std::vector<Pattern>      patterns;
    std::vector<PlaylistClip>  playlistClips;
    std::vector<PlaylistTrack> playlistTracks;   // M11 — dynamic track rows

    // M3 — channel types (global, not per-pattern)
    std::array<ChannelType, 16> channelTypes        = {};   // all Drum by default

    // M5 — mixer
    std::array<int, 16>       channelMixerRouting  = {};   // channel → mixer track (0-7)
    std::vector<MixerTrack>   mixerTracks;                  // 8 tracks initialised in MainComponent
    float                     masterVolume         = 1.0f;
    float                     masterPan            = 0.0f;
};
