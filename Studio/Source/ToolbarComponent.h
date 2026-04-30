/*
  ==============================================================================

    ToolbarComponent.h
    Created: 6 Mar 2026 11:31:51am
    Author:  홍준영

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "ProjectModel.h"


class ToolbarComponent : public juce::Component,
                         public juce::Button::Listener,
                         public juce::Slider::Listener,
                         public juce::ComboBox::Listener,
                         public juce::Timer
{
public:
    ToolbarComponent();
    ~ToolbarComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void buttonClicked(juce::Button*) override;
    void sliderValueChanged(juce::Slider*) override;
    void comboBoxChanged(juce::ComboBox*) override;
    void timerCallback() override;

    bool     isPlaying()   const { return playing; }
    PlayMode getPlayMode() const { return playMode; }
    double   getBPM()      const { return bpmSlider.getValue(); }
    
    
    void setBPM(double bpm)
    {
        bpmSlider.setValue(bpm, juce::dontSendNotification);
        bpmLabel.setText(juce::String(bpm, 1), juce::dontSendNotification);
    }
    void setPlayMode(PlayMode mode)
    {
        playMode = mode;
        patModeBtn_ .setToggleState(mode == PlayMode::Pattern, juce::dontSendNotification);
        songModeBtn_.setToggleState(mode == PlayMode::Song,    juce::dontSendNotification);
    }

    // Toggle Song ↔ Pattern mode and fire onPlayModeChanged
    void togglePlayMode()
    {
        playMode = (playMode == PlayMode::Pattern) ? PlayMode::Song : PlayMode::Pattern;
        patModeBtn_ .setToggleState(playMode == PlayMode::Pattern, juce::dontSendNotification);
        songModeBtn_.setToggleState(playMode == PlayMode::Song,    juce::dontSendNotification);
        if (onPlayModeChanged) onPlayModeChanged(playMode);
    }

    // M2.1 — refresh pattern list in the combo; call whenever patterns change
    void updatePatternList(const std::vector<Pattern>& patterns, int selectedId);

    // M4 — update title bar (filename + dirty marker)
    void setProjectTitle(const juce::String& filename, bool dirty);

    // Playback callbacks
    std::function<void()>           onPlay;
    std::function<void()>           onStop;
    std::function<void(double)>     onBPMChanged;
    std::function<void(PlayMode)>   onPlayModeChanged;
    std::function<void()>           onRewind;
    std::function<void()>           onFastForward;
    std::function<void()>           onToggleLoop;
    std::function<void(double)>     onMasterVolChanged;

    // M2.1 — Pattern management callbacks
    std::function<void(int patternId)> onPatternSelected;
    std::function<void()>              onNewPattern;
    std::function<void()>              onDuplicatePattern;
    std::function<void()>              onDeletePattern;
    std::function<void()>              onRenamePattern;

    // M4 — File callbacks
    std::function<void()>  onNewFile;
    std::function<void()>  onOpenFile;
    std::function<void()>  onSaveFile;
    std::function<void()>  onSaveFileAs;

    // M10 — Export callback
    std::function<void()>  onExport;
    std::function<void()>  onExportStems;

    // M5 — Mixer toggle
    std::function<void()>  onToggleMixer;

    // M12 — MIDI device selector button
    std::function<void()>  onMidiButton;
    std::function<void()>  onAudioButton;

    // Launchpad toggle
    std::function<void()>  onToggleLaunchpad;

    // Trackpad multitouch controller toggle
    std::function<void()>  onToggleTrackpad;

    // Live Performance mode toggle
    std::function<void()>  onToggleLiveMode;

    // Update LIVE button appearance to reflect current mode state
    void setLiveModeActive(bool active);

    // Loop state for visual feedback
    void setLoopEnabled(bool b)
    {
        loopEnabled_ = b;
        loopBtn_.setToggleState(b, juce::dontSendNotification);
    }

    // Recording — single toggle: start/stop recording
    std::function<void()>  onRecordStart;
    std::function<void()>  onRecordStop;

    // Set recording state (for visual feedback — called by MainComponent)
    void setRecordingActive(bool active);

    // Input level metering — call from MainComponent timer
    void setInputLevels(float levelL, float levelR);

    // Recording elapsed time — call from MainComponent timer
    void setRecordingElapsed(double seconds);

    // Playback position for segment display (bars, 0-based)
    void setPlaybackBar(double bars) { barPos_ = bars; }

private:
    // Row 1 — transport buttons (icon-based)
    juce::TextButton rewBtn_      { "REW"  };
    juce::TextButton playButton   { "PLAY" };
    juce::TextButton stopButton   { "STOP" };
    juce::TextButton recordButton { "REC"  };
    juce::TextButton ffBtn_       { "FF"   };
    juce::TextButton loopBtn_     { "LOOP" };
    juce::TextButton patModeBtn_  { "PAT"  };
    juce::TextButton songModeBtn_ { "SONG" };
    juce::Slider     bpmSlider;
    juce::Label      bpmLabel;
    juce::Label      titleLabel;
    juce::Slider     masterVolSlider;

    // Recording elapsed time label
    juce::Label      recTimeLabel;

    // Row 2 — pattern management
    juce::Label      patternLabel;
    juce::ComboBox   patternBox;
    juce::TextButton newPatBtn    { "+"      };
    juce::TextButton dupPatBtn    { "="      };
    juce::TextButton delPatBtn    { "-"      };
    juce::TextButton renamePatBtn { "Rename" };

    // M4 — file operation buttons (row 2, right side)
    juce::TextButton newFileBtn    { "New"     };
    juce::TextButton openFileBtn   { "Open"    };
    juce::TextButton saveFileBtn   { "Save"    };
    juce::TextButton saveAsFileBtn { "Save As" };

    // M10 — export buttons
    juce::TextButton exportBtn      { "Export WAV" };
    juce::TextButton exportStemsBtn { "Stems" };

    // M5 — mixer toggle button
    juce::TextButton mixerBtn      { "Mixer" };

    // M12 — MIDI button
    juce::TextButton audioBtn         { "Audio" };
    juce::TextButton midiBtn          { "MIDI" };

    // Launchpad button
    juce::TextButton launchpadBtn     { "Pad" };

    // Trackpad multitouch button
    juce::TextButton trackpadBtn      { "Touch" };

    // Live Performance mode indicator button
    juce::TextButton liveModeBtn_     { "LIVE" };

    bool     playing          = false;
    bool     recordingActive_ = false;
    bool     loopEnabled_     = false;
    int      blinkCounter_    = 0;
    float    inputLevelL_     = 0.0f;
    float    inputLevelR_     = 0.0f;
    double   recElapsedSec_   = 0.0;
    float    reelAngle_       = 0.0f;
    double   barPos_          = 0.0;
    PlayMode playMode = PlayMode::Pattern;

    // Draw a decorative tape reel at (cx, cy) with radius r
    void drawTapeReel(juce::Graphics& g, float cx, float cy, float r) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToolbarComponent)
};
