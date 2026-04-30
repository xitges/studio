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

class PremiumTransportButton : public juce::Button
{
public:
    PremiumTransportButton(const juce::String& buttonName)
        : juce::Button(buttonName)
    {
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = getLocalBounds().toFloat();
        const float cornerSize = 4.0f;

        // 1. 버튼 배경색상
        juce::Colour baseColor = juce::Colour(0xff2a2b2e);
        if (shouldDrawButtonAsDown)      baseColor = baseColor.darker(0.2f);
        else if (shouldDrawButtonAsHighlighted) baseColor = baseColor.brighter(0.1f);

        // 2. 배경 그라데이션
        juce::ColourGradient bgGradient(baseColor.brighter(0.05f), 0.0f, 0.0f,
                                        baseColor.darker(0.1f), 0.0f, bounds.getHeight(), false);
        g.setGradientFill(bgGradient);
        g.fillRoundedRectangle(bounds, cornerSize);

        // 3. 버튼 테두리 디테일
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerSize, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(bounds.reduced(1.5f), cornerSize - 1.0f, 1.0f);

        // 4. 아이콘 및 LED 상태
        bool isActive = getToggleState();
        juce::Colour iconColor = isActive ? themeColor : themeColor.withAlpha(0.4f);

        if (isActive)
        {
            juce::ColourGradient glow(themeColor.withAlpha(0.4f), bounds.getCentreX(), bounds.getCentreY(),
                                      themeColor.withAlpha(0.0f), bounds.getCentreX(), bounds.getCentreY() + bounds.getWidth() * 0.5f, true);
            g.setGradientFill(glow);
            g.fillEllipse(bounds.reduced(4.0f));
        }

        // 5. 텍스트 그리기
        g.setColour(iconColor);
        g.setFont(juce::Font(bounds.getHeight() * 0.45f).withStyle(juce::Font::bold));
        
        auto textBounds = shouldDrawButtonAsDown ? getLocalBounds().translated(0, 1) : getLocalBounds();
        g.drawText(getName(), textBounds, juce::Justification::centred);
    }

private:
    juce::Colour themeColor;
};

// ==============================================================================
// 새로 추가된 커스텀 Segmented Control 클래스
// ==============================================================================
class SegmentedControl : public juce::Component
{
public:
    std::function<void(int)> onSelectionChanged;
    int selectedIndex = 0; // 0: PAT, 1: SONG

    SegmentedControl() {}

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        auto cornerSize = 6.0f;
        
        // LF::kAccent가 정의된 곳이 있다면 그대로 쓰시고, 만약 에러가 난다면
        // juce::Colours::red 등으로 임시 교체하세요.
        auto accentColor = juce::Colour(0xffc95c54);

        // 1. 전체 배경 (파인 느낌의 그림자)
        g.setColour(juce::Colours::black.withAlpha(0.2f));
        g.fillRoundedRectangle(bounds, cornerSize);
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), cornerSize, 1.0f);

        // 2. 선택된 부분 하이라이트 박스
        auto segmentWidth = bounds.getWidth() / 2.0f;
        auto highlightArea = juce::Rectangle<float>(
            selectedIndex * segmentWidth, 0, segmentWidth, bounds.getHeight()
        ).reduced(2.0f);

        g.setColour(accentColor);
        g.fillRoundedRectangle(highlightArea, cornerSize - 1.0f);

        // 3. 텍스트 그리기
        g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 12.0f, juce::Font::bold)));
        
        // PAT
        g.setColour(selectedIndex == 0 ? juce::Colours::white : juce::Colours::grey);
            g.drawFittedText("PAT", bounds.withWidth(segmentWidth).toNearestInt(), juce::Justification::centred, 1);
            
            // SONG
        g.setColour(selectedIndex == 1 ? juce::Colours::white : juce::Colours::grey);
        g.drawFittedText("SONG", bounds.withWidth(segmentWidth).withX(segmentWidth).toNearestInt(), juce::Justification::centred, 1);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        int newIndex = e.x < (getWidth() / 2) ? 0 : 1;
        if (newIndex != selectedIndex)
        {
            selectedIndex = newIndex;
            if (onSelectionChanged) onSelectionChanged(selectedIndex);
            repaint();
        }
    }
};

// ==============================================================================

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
    
    // [수정됨] 기존 버튼 대신 SegmentedControl을 업데이트합니다.
    void setPlayMode(PlayMode mode)
    {
        playMode = mode;
        modeSelector.selectedIndex = (mode == PlayMode::Pattern) ? 0 : 1;
        modeSelector.repaint(); // UI 갱신
    }

    // [수정됨] 토글 로직도 SegmentedControl에 맞춰 변경했습니다.
    void togglePlayMode()
    {
        playMode = (playMode == PlayMode::Pattern) ? PlayMode::Song : PlayMode::Pattern;
        modeSelector.selectedIndex = (playMode == PlayMode::Pattern) ? 0 : 1;
        modeSelector.repaint(); // UI 갱신
        
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
    
    // [수정됨] 기존 patModeBtn_ 과 songModeBtn_ 을 삭제하고 새 컨트롤을 넣었습니다.
    SegmentedControl modeSelector;
    
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
