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
                         public juce::ComboBox::Listener
{
public:
    ToolbarComponent();
    ~ToolbarComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void buttonClicked(juce::Button*) override;
    void sliderValueChanged(juce::Slider*) override;
    void comboBoxChanged(juce::ComboBox*) override;

    bool     isPlaying()   const { return playing; }
    PlayMode getPlayMode() const { return playMode; }
    double   getBPM()      const { return bpmSlider.getValue(); }

    // M2.1 — refresh pattern list in the combo; call whenever patterns change
    void updatePatternList(const std::vector<Pattern>& patterns, int selectedId);

    // M4 — update title bar (filename + dirty marker)
    void setProjectTitle(const juce::String& filename, bool dirty);

    // Playback callbacks
    std::function<void()>           onPlay;
    std::function<void()>           onStop;
    std::function<void(double)>     onBPMChanged;
    std::function<void(PlayMode)>   onPlayModeChanged;

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

private:
    // Row 1 — transport
    juce::TextButton playButton   { "Play" };
    juce::TextButton stopButton   { "Stop" };
    juce::TextButton recordButton { "Rec"  };
    juce::ComboBox   playModeBox;
    juce::Slider     bpmSlider;
    juce::Label      bpmLabel;
    juce::Label      titleLabel;

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

    // M10 — export button
    juce::TextButton exportBtn     { "Export WAV" };

    bool     playing  = false;
    PlayMode playMode = PlayMode::Pattern;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToolbarComponent)
};
