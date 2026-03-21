/*
  ==============================================================================

    TrackpadController.h
    Created: 21 Mar 2026
    Author:  홍준영

    MacBook trackpad → 4×4 launchpad grid via MultitouchSupport.framework.
    Pure C++ header — Objective-C++ details hidden behind pimpl.

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <functional>
#include <memory>

class TrackpadController
{
public:
    TrackpadController();
    ~TrackpadController();

    void start();
    void stop();
    bool isRunning() const;

    // padIdx 0..15 (4×4 grid), velocity 0..1 (from finger pressure),
    // isNoteOn: true = finger down, false = finger lifted
    std::function<void(int padIdx, float velocity, bool isNoteOn)> onPadEvent;

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackpadController)
};
