/*
  ==============================================================================

    ToolbarComponent.cpp
    Created: 6 Mar 2026 11:31:42am
    Author:  홍준영

  ==============================================================================
*/

#include "ToolbarComponent.h"
#include "StudioLookAndFeel.h"

ToolbarComponent::ToolbarComponent()
{
    // ---- Row 1: Transport buttons (icon-based, reference style)
    using LF = StudioLookAndFeel;

    auto setupTransportBtn = [](juce::TextButton& btn, juce::uint32 onColor)
    {
        btn.setClickingTogglesState(false);
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(LF::kText));
        btn.setColour(juce::TextButton::textColourOnId,  juce::Colour(0xffffffff));
        btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(onColor));
    };

    addAndMakeVisible(rewBtn_);
        setupTransportBtn(rewBtn_, LF::kTextDim);
        rewBtn_.onClick = [this] { if (onRewind) onRewind(); };

        addAndMakeVisible(playButton);
        setupTransportBtn(playButton, LF::kTextDim); // 색상 통일 (원래: kLedGreen)
        playButton.addListener(this);

        addAndMakeVisible(stopButton);
        setupTransportBtn(stopButton, LF::kTextDim);
        stopButton.addListener(this);

        addAndMakeVisible(recordButton);
        setupTransportBtn(recordButton, LF::kTextDim); // 색상 통일 (원래: kLedRed)
        recordButton.onClick = [this]
        {
            if (recordingActive_) { if (onRecordStop)  onRecordStop();  }
            else                  { if (onRecordStart) onRecordStart(); }
        };

        addAndMakeVisible(ffBtn_);
        setupTransportBtn(ffBtn_, LF::kTextDim);
        ffBtn_.onClick = [this] { if (onFastForward) onFastForward(); };

        addAndMakeVisible(loopBtn_);
        setupTransportBtn(loopBtn_, LF::kTextDim); // 색상 통일 (원래: kLedAmber)
        loopBtn_.setClickingTogglesState(true);
        loopBtn_.onClick = [this] { if (onToggleLoop) onToggleLoop(); };

    // ---- Row 1: MASTER volume knob
    addAndMakeVisible(masterVolSlider);
    masterVolSlider.setRange(0.0, 1.0, 0.01);
    masterVolSlider.setValue(0.82);
    masterVolSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    masterVolSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    masterVolSlider.addListener(this);
    masterVolSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(LF::kAccent));

    // ---- Row 1: Recording elapsed time label (kept for compatibility, hidden in layout)
    recTimeLabel.setText("", juce::dontSendNotification);
    recTimeLabel.setColour(juce::Label::textColourId, juce::Colour(LF::kLedRed));
    recTimeLabel.setFont(LF::displayFont(14.0f));
    recTimeLabel.setJustificationType(juce::Justification::centredLeft);

    // ---- Row 1: Play mode selector (PAT | SONG segmented control)
        addAndMakeVisible(modeSelector);
        
        // 초기 선택 상태 동기화
        modeSelector.selectedIndex = (playMode == PlayMode::Pattern) ? 0 : 1;
        
        // 클릭 시 동작 설정
        modeSelector.onSelectionChanged = [this](int index)
        {
            auto newMode = (index == 0) ? PlayMode::Pattern : PlayMode::Song;
            
            if (playMode != newMode)
            {
                playMode = newMode;
                if (onPlayModeChanged) onPlayModeChanged(playMode);
            }
        };

    // ---- Row 1: BPM display + invisible slider
    addAndMakeVisible(bpmSlider);
    bpmSlider.setRange(60.0, 200.0, 1.0);
    bpmSlider.setValue(70.0);
    bpmSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    bpmSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.addListener(this);
    bpmSlider.setAlpha(0.0f);   // invisible but interactive (drag to change BPM)

    addAndMakeVisible(bpmLabel);
    bpmLabel.setText("70.0", juce::dontSendNotification);
    bpmLabel.setColour(juce::Label::textColourId, juce::Colour(StudioLookAndFeel::kDisplayFg));
    bpmLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    bpmLabel.setFont(StudioLookAndFeel::displayFont(18.0f));
    bpmLabel.setJustificationType(juce::Justification::centred);

    // ---- Row 1: Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Studio", juce::dontSendNotification);
    titleLabel.setFont(StudioLookAndFeel::monoFont(11.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(StudioLookAndFeel::kText));
    titleLabel.setJustificationType(juce::Justification::centredRight);

    // ---- Row 2: Pattern label
    addAndMakeVisible(patternLabel);
    patternLabel.setText("PATTERN", juce::dontSendNotification);
    patternLabel.setColour(juce::Label::textColourId, juce::Colour(StudioLookAndFeel::kTextFaint));
    patternLabel.setFont(StudioLookAndFeel::monoFont(9.0f, juce::Font::bold));
    patternLabel.setJustificationType(juce::Justification::centredRight);

    // ---- Row 2: Pattern combo (items populated via updatePatternList)
    addAndMakeVisible(patternBox);
    patternBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(StudioLookAndFeel::kChassis2));
    patternBox.setColour(juce::ComboBox::outlineColourId,    juce::Colour(StudioLookAndFeel::kPanelRim));
    patternBox.setColour(juce::ComboBox::textColourId,       juce::Colour(StudioLookAndFeel::kText));
    patternBox.onChange = [this]
    {
        const int id = patternBox.getSelectedId();
        if (id > 0 && onPatternSelected)
            onPatternSelected(id);
    };

    // ---- Row 2: New / Duplicate / Delete pattern buttons
    addAndMakeVisible(newPatBtn);
    newPatBtn.setButtonText("+");
    newPatBtn.setTooltip("New pattern");
    newPatBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
    newPatBtn.onClick = [this] { if (onNewPattern) onNewPattern(); };

    addAndMakeVisible(dupPatBtn);
    dupPatBtn.setButtonText("DUP");
    dupPatBtn.setTooltip("Duplicate pattern");
    dupPatBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
    dupPatBtn.onClick = [this] { if (onDuplicatePattern) onDuplicatePattern(); };

    addAndMakeVisible(delPatBtn);
    delPatBtn.setButtonText("DEL");
    delPatBtn.setTooltip("Delete pattern");
    delPatBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kAccent).withAlpha(0.2f));
    delPatBtn.onClick = [this] { if (onDeletePattern) onDeletePattern(); };

    addAndMakeVisible(renamePatBtn);
    renamePatBtn.setButtonText("NAME");
    renamePatBtn.setTooltip("Rename pattern");
    renamePatBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
    renamePatBtn.onClick = [this] { if (onRenamePattern) onRenamePattern(); };

    // ---- M4: File buttons (row 2, right side)
    addAndMakeVisible(newFileBtn);
    newFileBtn.setButtonText("NEW");
    newFileBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
    newFileBtn.onClick = [this] { if (onNewFile) onNewFile(); };

    addAndMakeVisible(openFileBtn);
    openFileBtn.setButtonText("OPEN");
    openFileBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
    openFileBtn.onClick = [this] { if (onOpenFile) onOpenFile(); };

    addAndMakeVisible(saveFileBtn);
    saveFileBtn.setButtonText("SAVE");
    saveFileBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
    saveFileBtn.onClick = [this] { if (onSaveFile) onSaveFile(); };

    addAndMakeVisible(saveAsFileBtn);
    saveAsFileBtn.setButtonText("SAVE AS");
    saveAsFileBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
    saveAsFileBtn.onClick = [this] { if (onSaveFileAs) onSaveFileAs(); };

    addAndMakeVisible(exportBtn);
    exportBtn.setButtonText("EXPORT");
    exportBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kAccent));
    exportBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffffff));
    exportBtn.onClick = [this] { if (onExport) onExport(); };

    addAndMakeVisible(exportStemsBtn);
    exportStemsBtn.setButtonText("STEMS");
    exportStemsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kAccent).darker(0.1f));
    exportStemsBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffffff));
    exportStemsBtn.onClick = [this] { if (onExportStems) onExportStems(); };

    addAndMakeVisible(mixerBtn);
    mixerBtn.setButtonText("MIX");
    mixerBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
    mixerBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextDim));
    mixerBtn.onClick = [this] { if (onToggleMixer) onToggleMixer(); };

    addAndMakeVisible(audioBtn);
    audioBtn.setButtonText("AUDIO");
    audioBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
    audioBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextDim));
    audioBtn.onClick = [this] { if (onAudioButton) onAudioButton(); };

    addAndMakeVisible(midiBtn);
    midiBtn.setButtonText("MIDI");
    midiBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
    midiBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextDim));
    midiBtn.onClick = [this] { if (onMidiButton) onMidiButton(); };

    addAndMakeVisible(launchpadBtn);
    launchpadBtn.setButtonText("PAD");
    launchpadBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
    launchpadBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextDim));
    launchpadBtn.onClick = [this] { if (onToggleLaunchpad) onToggleLaunchpad(); };

    addAndMakeVisible(trackpadBtn);
    trackpadBtn.setButtonText("TOUCH");
    trackpadBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
    trackpadBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextDim));
    trackpadBtn.setClickingTogglesState(true);
    trackpadBtn.onClick = [this] { if (onToggleTrackpad) onToggleTrackpad(); };

    // ---- LIVE mode indicator button
    addAndMakeVisible(liveModeBtn_);
    liveModeBtn_.setButtonText("LIVE");
    liveModeBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
    liveModeBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextFaint));
    liveModeBtn_.setTooltip("Toggle Live Performance Mode");
    liveModeBtn_.onClick = [this] { if (onToggleLiveMode) onToggleLiveMode(); };

    // Start timer for blinking and level meter updates
    startTimerHz(15);
}

ToolbarComponent::~ToolbarComponent() {}

void ToolbarComponent::setLiveModeActive(bool active)
{
    if (active)
    {
        liveModeBtn_.setButtonText("LIVE ON");
        liveModeBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kAccent));
        liveModeBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffffff));
    }
    else
    {
        liveModeBtn_.setButtonText("LIVE");
        liveModeBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
        liveModeBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextFaint));
    }
    liveModeBtn_.repaint();
}

void ToolbarComponent::setRecordingActive(bool active)
{
    recordingActive_ = active;
    blinkCounter_ = 0;
    recordButton.setToggleState(active, juce::dontSendNotification);
    if (!active)
    {
        recTimeLabel.setText("", juce::dontSendNotification);
        recElapsedSec_ = 0.0;
    }
}

void ToolbarComponent::setInputLevels(float levelL, float levelR)
{
    inputLevelL_ = levelL;
    inputLevelR_ = levelR;
}

void ToolbarComponent::setRecordingElapsed(double seconds)
{
    recElapsedSec_ = seconds;
}

void ToolbarComponent::timerCallback()
{
    ++blinkCounter_;

    // Animate tape reels + position display + VU when playing
    if (playing)
    {
        reelAngle_ = std::fmod(reelAngle_ + 2.5f, 360.0f);
        repaint(0, 0, 290, 92);                         // reels + brand area
        repaint(300, 0, 130, 92);                        // position display
        repaint(getWidth() - 256, 0, 256, 92);           // right section (meter + LEDs)
    }
    else
    {
        repaint(getWidth() - 256, 0, 256, 92);
    }

    if (recordingActive_)
    {
        // Blink recordButton toggle to create LED pulse effect
        const bool bright = (blinkCounter_ % 15) < 10;
        recordButton.setToggleState(bright, juce::dontSendNotification);
    }
}

// ---------------------------------------------------------------------------

void ToolbarComponent::setProjectTitle(const juce::String& filename, bool dirty)
{
    juce::String name = filename.isEmpty() ? "Untitled" : filename;
    juce::String title = "PROJECT  ";
    title += name;
    if (dirty) title += "  *";

    titleLabel.setText(title, juce::dontSendNotification);
}

void ToolbarComponent::updatePatternList(const std::vector<Pattern>& patterns, int selectedId)
{
    patternBox.clear(juce::dontSendNotification);
    for (const auto& p : patterns)
        patternBox.addItem(p.name, p.id);   // item ID == pattern ID
    patternBox.setSelectedId(selectedId, juce::dontSendNotification);
}

void ToolbarComponent::paint(juce::Graphics& g)
{
    using LF = StudioLookAndFeel;
    const float W     = (float)getWidth();
    const float row1H = 92.0f;

    // =========================================================
    // ROW 1 — Transport Panel (rounded, reference-matched)
    // =========================================================
    {
        const juce::Rectangle<float> panel(5.0f, 5.0f, W - 10.0f, row1H - 8.0f);

        // Panel bg gradient
        juce::ColourGradient panelBg(juce::Colour(0xffffffff), 0.0f, 0.0f,
                                     juce::Colour(0xfff2f2f2), 0.0f, row1H, false);
        g.setGradientFill(panelBg);
        g.fillRoundedRectangle(panel, 12.0f);

        // Top specular highlight
        g.setColour(juce::Colours::white.withAlpha(0.42f));
        g.fillRoundedRectangle(panel.withHeight(panel.getHeight() * 0.5f), 12.0f);

        // Border
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawRoundedRectangle(panel.reduced(0.5f), 12.0f, 1.0f);

        // Bottom inset shadow
        g.setColour(juce::Colour(0x18000000));
        g.fillRoundedRectangle(panel.withTop(panel.getBottom() - 3.0f), 3.0f);

        const float midY = row1H * 0.47f;   // vertical centre

        // ---- LEFT: Brand + Tape Reels ----------------------------------------
        {
            // "fieldlab." — Space Grotesk 20px Bold
            g.setFont(LF::brandFont(20.0f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kText));
            g.drawText("xitges", 14, 10, 84, 24, juce::Justification::centredLeft);
            g.setColour(juce::Colour(LF::kAccent));
            g.drawText(".", 96, 10, 14, 24, juce::Justification::centredLeft);

            // Subtitle
            g.setFont(LF::monoFont(7.5f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kTextFaint));
            g.drawText(juce::String::fromUTF8("TR-1  \xe2\x80\x94  TABLETOP DAW"), 14, 38, 114, 10,
                        juce::Justification::centredLeft);

            // Tags
            g.setFont(LF::monoFont(7.0f, juce::Font::bold));
            auto drawTag = [&](int x, int y, int w, const juce::String& txt, juce::uint32 col)
            {
                g.setColour(juce::Colour(col).withAlpha(0.14f));
                g.fillRoundedRectangle((float)x, (float)y, (float)w, 12.0f, 2.0f);
                g.setColour(juce::Colour(0x28000000));
                g.drawRoundedRectangle((float)x + 0.5f, (float)y + 0.5f, (float)w - 1.0f, 11.0f, 2.0f, 0.5f);
                g.setColour(juce::Colour(col));
                g.drawText(txt, x + 3, y + 1, w - 6, 10, juce::Justification::centredLeft);
            };
            drawTag(14, 52, 62, juce::String::fromUTF8("Studio \xc2\xb7 04"), LF::kTextDim);
            if (recordingActive_)
                drawTag(80, 52, 66, juce::String::fromUTF8("\xe2\x97\x8f REC ARM"), LF::kLedRed);

            // Dashed separator after brand
            g.setColour(juce::Colour(0x28000000));
            for (float dy = 14.0f; dy < row1H - 14.0f; dy += 6.0f)
                g.drawLine(120.0f, dy, 120.0f, dy + 3.0f, 1.0f);

            // Tape reels — size=56px (r=28), matching reference 64px proportion
            const float reelCy = midY;
            const float reelR  = 28.0f;
            const float reel1X = 158.0f;
            const float reel2X = 240.0f;

            drawTapeReel(g, reel1X, reelCy, reelR);

            // Tape strip (reference: width=40, gradient #2a1f15→#4a3a25→#2a1f15)
            const float stripX = reel1X + reelR + 3.0f;
            const float stripW = reel2X - reelR - 3.0f - stripX;
            juce::ColourGradient tapeGrad(juce::Colour(0xff2a1f15), stripX, 0,
                                          juce::Colour(0xff2a1f15), stripX + stripW, 0, false);
            tapeGrad.addColour(0.5, juce::Colour(0xff4a3a25));
            g.setGradientFill(tapeGrad);
            g.fillRoundedRectangle(stripX, reelCy - 1.5f, stripW, 3.0f, 1.0f);

            drawTapeReel(g, reel2X, reelCy, reelR);

            // L / R labels
            g.setFont(LF::monoFont(7.0f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kTextFaint));
            g.drawText("L", (int)(reel1X - 8.0f), (int)(reelCy + reelR + 3.0f), 16, 9,
                        juce::Justification::centred);
            g.drawText("R", (int)(reel2X - 8.0f), (int)(reelCy + reelR + 3.0f), 16, 9,
                        juce::Justification::centred);

            // Dashed separator after reels
            for (float dy = 14.0f; dy < row1H - 14.0f; dy += 6.0f)
                g.drawLine(280.0f, dy, 280.0f, dy + 3.0f, 1.0f);
        }

        // ---- CENTER-LEFT: BAR.BEAT.TICK Segment Display ----------------------
        {
            const int dispX = 350, dispY = 25, dispW = 130, dispH = 38;

            g.setColour(juce::Colour(LF::kDisplayBg));
            g.fillRoundedRectangle((float)dispX, (float)dispY, (float)dispW, (float)dispH, 4.0f);

            // Scanlines
            g.setColour(juce::Colour(0x1e000000));
            for (int sy = dispY + 2; sy < dispY + dispH - 2; sy += 3)
                g.fillRect(dispX + 2, sy, dispW - 4, 1);

            g.setColour(juce::Colour(0xff000000).withAlpha(0.55f));
            g.drawRoundedRectangle((float)dispX + 0.5f, (float)dispY + 0.5f,
                                   (float)dispW - 1.0f, (float)dispH - 1.0f, 4.0f, 1.0f);

            // Position text
            const int    bar  = juce::jmax(1, (int)barPos_ + 1);
            const double frac = barPos_ - std::floor(barPos_);
            const int    beat = (int)(frac * 4.0) + 1;
            const int    tick = (int)((frac * 4.0 - std::floor(frac * 4.0)) * 240.0);
            const auto   posText = juce::String::formatted("%03d.%d.%03d", bar, beat, tick);

            g.setFont(LF::displayFont((float)dispH * 0.75f));
            // Phosphor glow passes (simulate text-shadow: 0 0 6px rgba(185,255,102,0.6))
            g.setColour(juce::Colour(LF::kDisplayFg).withAlpha(0.07f));
            g.drawText(posText, dispX, dispY, dispW, dispH, juce::Justification::centred);
            g.setColour(juce::Colour(LF::kDisplayFg).withAlpha(0.12f));
            g.drawText(posText, dispX + 1, dispY, dispW - 2, dispH, juce::Justification::centred);
            g.drawText(posText, dispX, dispY + 1, dispW, dispH - 2, juce::Justification::centred);
            g.setColour(juce::Colour(LF::kDisplayFg).withAlpha(0.18f));
            g.drawText(posText, dispX + 1, dispY + 1, dispW - 2, dispH - 2,
                        juce::Justification::centred);
            g.setColour(juce::Colour(LF::kDisplayFg));
            g.drawText(posText, dispX + 2, dispY + 2, dispW - 4, dispH - 4,
                        juce::Justification::centred);

            // BAR / BEAT / TICK labels
            g.setFont(LF::monoFont(6.5f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kTextFaint));
            const int lY = dispY + dispH + 4;
            const int cW = dispW / 3;
            g.drawText("BAR",  dispX,          lY, cW, 9, juce::Justification::centred);
            g.drawText("BEAT", dispX + cW,     lY, cW, 9, juce::Justification::centred);
            g.drawText("TICK", dispX + cW * 2, lY, cW, 9, juce::Justification::centred);
        }

        // ---- BPM Segment Display (painted behind the bpmLabel child) ----------
        {
            const auto bR = bpmLabel.getBounds().toFloat();
            if (bR.getWidth() > 4 && bR.getHeight() > 4)
            {
                g.setColour(juce::Colour(LF::kDisplayBg));
                g.fillRoundedRectangle(bR, 4.0f);
                // Scanlines
                g.setColour(juce::Colour(0x1e000000));
                for (int sy = (int)bR.getY() + 2; sy < (int)bR.getBottom() - 2; sy += 3)
                    g.fillRect((int)bR.getX() + 2, sy, (int)bR.getWidth() - 4, 1);
                // Border
                g.setColour(juce::Colour(0x55000000));
                g.drawRoundedRectangle(bR.reduced(0.5f), 4.0f, 1.0f);
                // "BPM · 4/4" sub-label below
                g.setFont(LF::monoFont(6.5f, juce::Font::bold));
                g.setColour(juce::Colour(LF::kTextFaint));
                g.drawText(juce::String::fromUTF8("BPM \xc2\xb7 4/4"), (int)bR.getX(), (int)bR.getBottom() + 2,
                            (int)bR.getWidth(), 9, juce::Justification::centred);
            }
        }

        // ---- RIGHT: Status + MasterMeter + MASTER label ---------------------
        {
            const int rightX = (int)W - 350;

            // Dashed separator
            g.setColour(juce::Colour(0x28000000));
            for (float dy = 14.0f; dy < row1H - 14.0f; dy += 6.0f)
                g.drawLine((float)rightX, dy, (float)rightX, dy + 3.0f, 1.0f);

            // LED row — inline: [LED] PLAY [LED] REC [LED] LOOP
            auto drawLed = [&](float cx, float cy, juce::uint32 col, bool on)
            {
                const float r = 3.5f;
                if (on)
                {
                    g.setColour(juce::Colour(col).withAlpha(0.3f));
                    g.fillEllipse(cx - r*2.2f, cy - r*2.2f, r*4.4f, r*4.4f);
                }
                g.setColour(on ? juce::Colour(col) : juce::Colour(LF::kLedOff));
                g.fillEllipse(cx - r, cy - r, r*2.0f, r*2.0f);
                g.setColour(juce::Colour(0x80000000));
                g.drawEllipse(cx - r, cy - r, r*2.0f, r*2.0f, 0.5f);
            };

            const float ledY   = 20.0f;
            const float lx0    = (float)rightX + 10.0f;
            g.setFont(LF::monoFont(8.0f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kTextDim));

            // PLAY LED + label
            drawLed(lx0 + 3.5f, ledY, LF::kLedGreen, playing);
            g.drawText("PLAY", (int)(lx0 + 10), (int)(ledY - 5), 28, 10, juce::Justification::centredLeft);

            // REC LED + label
            const float rx = lx0 + 47.0f;
            drawLed(rx + 3.5f, ledY, LF::kLedRed, recordingActive_);
            g.drawText("REC",  (int)(rx + 10), (int)(ledY - 5), 24, 10, juce::Justification::centredLeft);

            // LOOP LED + label
            const float lox = rx + 40.0f;
            drawLed(lox + 3.5f, ledY, LF::kLedAmber, loopEnabled_);
            g.drawText("LOOP", (int)(lox + 10), (int)(ledY - 5), 30, 10, juce::Justification::centredLeft);

            // Info text lines
            g.setFont(LF::monoFont(7.5f));
            g.setColour(juce::Colour(LF::kTextFaint));
            g.drawText(juce::String::fromUTF8("44.1kHz \xc2\xb7 24bit \xc2\xb7 BUF 128"),
                        rightX + 6, 34, 148, 10, juce::Justification::centredLeft);
            g.drawText(juce::String::fromUTF8("CPU 18% \xc2\xb7 MEM 1.4G"),
                        rightX + 6, 47, 148, 10, juce::Justification::centredLeft);

            // MasterMeter — 18 segs, 1.6px height, 8px wide per bar, gap:6 between bars
            // Container: kDisplayBg bg, borderRadius 4, inset shadow, 1px dark border
            {
                const int SEGS   = 18;
                const float segH = 1.6f;
                const float segW = 8.0f;
                const float segGap = 1.0f;
                const float barGap = 6.0f;
                const float meterH = SEGS * segH + (SEGS - 1) * segGap;  // ≈ 45.8px
                const float meterW = segW * 2 + barGap;                   // 22px
                const float padX = 8.0f, padY = 6.0f;
                const float contW = meterW + padX * 2;                    // 34px
                const float contH = meterH + padY * 2;                    // ≈58px (fits in 84px)
                const float contX = (float)rightX + 235.0f;
                const float contY = ((float)row1H - contH) * 0.5f;

                g.setColour(juce::Colour(LF::kDisplayBg));
                g.fillRoundedRectangle(contX, contY, contW, contH, 4.0f);
                g.setColour(juce::Colour(0x99000000));
                g.drawRoundedRectangle(contX + 0.5f, contY + 0.5f, contW - 1.0f, contH - 1.0f, 4.0f, 1.0f);

                const float vuL = (inputLevelL_ > 0.01f)
                    ? inputLevelL_
                    : (playing ? 0.55f + 0.22f * std::sin((float)blinkCounter_ * 0.36f) : 0.04f);
                const float vuR = (inputLevelR_ > 0.01f)
                    ? inputLevelR_
                    : (playing ? 0.48f + 0.25f * std::sin((float)blinkCounter_ * 0.51f + 1.1f) : 0.04f);

                auto drawBar = [&](float bx, float lv)
                {
                    for (int i = 0; i < SEGS; ++i)
                    {
                        const float t   = (float)i / (SEGS - 1);
                        const bool  on  = t < lv;
                        const auto  col = t < 0.70f ? LF::kLedGreen
                                        : t < 0.88f ? LF::kLedAmber
                                        :             LF::kLedRed;
                        const float sy  = contY + padY + (SEGS - 1 - i) * (segH + segGap);
                        if (on)
                        {
                            g.setColour(juce::Colour(col));
                            g.fillRoundedRectangle(bx, sy, segW, segH, 0.5f);
                        }
                        else
                        {
                            g.setColour(juce::Colour(0x38ffffff));
                            g.fillRoundedRectangle(bx, sy, segW, segH, 0.5f);
                        }
                    }
                };
                drawBar(contX + padX,             vuL);
                drawBar(contX + padX + segW + barGap, vuR);

                // "MASTER L · R" label
                g.setFont(LF::monoFont(6.5f, juce::Font::bold));
                g.setColour(juce::Colour(LF::kTextFaint));
                g.drawText(juce::String::fromUTF8("MASTER L \xc2\xb7 R"),
                            (int)contX - 2, (int)(contY + contH + 3), (int)contW + 4, 9,
                            juce::Justification::centred);
            }

            // "MASTER" label under knob
            {
                const int kX = getWidth() - 58;
                g.setFont(LF::monoFont(7.0f, juce::Font::bold));
                g.setColour(juce::Colour(LF::kTextFaint));
                g.drawText("MASTER", kX - 4, row1H - 14, 56, 10, juce::Justification::centred);
            }
        }
    }

    // =========================================================
    // ROW 2 — Pattern / Utility / File strip
    // =========================================================
    {
        const float y2 = row1H;
        const float h2 = (float)getHeight() - y2;

        juce::ColourGradient strip(juce::Colour(0xfff0f0f0), 0.0f, y2,
                                   juce::Colour(0xffe0e0e0), 0.0f, y2 + h2, false);
        g.setGradientFill(strip);
        g.fillRect(0.0f, y2, W, h2);

        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine(0.0f, y2, W, y2, 1.0f);

        g.setColour(juce::Colour(LF::kAccent).withAlpha(0.5f));
        g.fillRect(0.0f, (float)getHeight() - 2.0f, W, 2.0f);
    }
}

void ToolbarComponent::resized()
{
    const int row1H = 92;

    // ---- Row 1: MASTER knob (right edge, placed first to fix rightEnd)
    {
        const int kSize = 48;
        masterVolSlider.setBounds(getWidth() - kSize - 10, (row1H - kSize) / 2, kSize, kSize);
    }

    // ---- Row 1: Transport + mode selector + BPM (center section) ------------
    {
        const int leftEnd  = 406;
        const int rightEnd = getWidth() - 260;
        const int avail    = juce::jmax(0, rightEnd - leftEnd);

        // Square transport buttons: REW(28)+PLAY(38)+STOP(28)+REC(28)+FF(28)+LOOP(28) + 5×5gaps
        // + gap(10) + PAT(38)+gap(3)+SONG(46) + gap(10) + bpmArea(72)
        const int ctrlW = (28 + 38 + 28 + 28 + 28 + 28) + 5*5 + 10 + (38 + 3 + 46) + 10 + 72;
        int curX = leftEnd + juce::jmax(0, (avail - ctrlW) / 2);

        // Helper: place a square button centered vertically in row1H
        auto placeSquare = [&](juce::TextButton& btn, int w)
        {
            btn.setBounds(curX, (row1H - w) / 2, w, w);
            curX += w + 5;
        };

        placeSquare(rewBtn_,      28);
        placeSquare(playButton,   38);
        placeSquare(stopButton,   28);
        placeSquare(recordButton, 28);
        placeSquare(ffBtn_,       28);
        // loopBtn_ last — no extra gap after it
        loopBtn_.setBounds(curX, (row1H - 28) / 2, 28, 28);
        curX += 28 + 10;

        // PAT | SONG segmented selector (28px tall, same visual weight as buttons)
        // ---- Row 1: Play mode selector (PAT | SONG) 배치
        // 기존 두 버튼의 너비(38+46)와 간격(3)을 합친 87을 전체 너비로 설정합니다.
        modeSelector.setBounds(curX, (row1H - 28) / 2, 90, 28);
        
        // 다음 컴포넌트를 위해 curX 이동 (modeSelector 너비 87 + 우측 여백 10)
        curX += 90 + 10;

        // BPM display (painted bg + label overlay + invisible drag slider)
        {
            const int bpmH = 42;
            juce::Rectangle<int> bpmArea(curX, (row1H - bpmH) / 2, 72, bpmH);
            bpmLabel .setBounds(bpmArea);
            bpmSlider.setBounds(bpmArea);
        }

        recTimeLabel.setBounds(0, 0, 0, 0);
    }

    // ---- Row 2: Pattern / Utility / File strip --------------------------------
    {
        auto row2 = getLocalBounds().removeFromBottom(getHeight() - row1H).reduced(6, 4);

        // LEFT: pattern management
        patternLabel.setBounds(row2.removeFromLeft(58).reduced(2));
        patternBox  .setBounds(row2.removeFromLeft(140).reduced(2));
        row2.removeFromLeft(6);
        newPatBtn   .setBounds(row2.removeFromLeft(24).reduced(2));
        dupPatBtn   .setBounds(row2.removeFromLeft(40).reduced(2));
        delPatBtn   .setBounds(row2.removeFromLeft(34).reduced(2));
        row2.removeFromLeft(6);
        renamePatBtn.setBounds(row2.removeFromLeft(50).reduced(2));
        row2.removeFromLeft(10);

        // Utility buttons (mixer/audio/midi/pad/touch/live)
        mixerBtn    .setBounds(row2.removeFromLeft(38).reduced(2));
        row2.removeFromLeft(2);
        audioBtn    .setBounds(row2.removeFromLeft(46).reduced(2));
        row2.removeFromLeft(2);
        midiBtn     .setBounds(row2.removeFromLeft(40).reduced(2));
        row2.removeFromLeft(2);
        launchpadBtn.setBounds(row2.removeFromLeft(36).reduced(2));
        row2.removeFromLeft(2);
        trackpadBtn .setBounds(row2.removeFromLeft(46).reduced(2));
        row2.removeFromLeft(2);
        liveModeBtn_.setBounds(row2.removeFromLeft(48).reduced(2));

        // RIGHT: file + export buttons
        exportStemsBtn.setBounds(row2.removeFromRight(50).reduced(2));
        exportBtn     .setBounds(row2.removeFromRight(66).reduced(2));
        row2.removeFromRight(6);
        saveAsFileBtn .setBounds(row2.removeFromRight(60).reduced(2));
        saveFileBtn   .setBounds(row2.removeFromRight(46).reduced(2));
        openFileBtn   .setBounds(row2.removeFromRight(46).reduced(2));
        newFileBtn    .setBounds(row2.removeFromRight(40).reduced(2));

        // Title: remaining space in centre
        titleLabel.setBounds(row2.reduced(4, 0));
    }
}

// ---------------------------------------------------------------------------

void ToolbarComponent::buttonClicked(juce::Button* btn)
{
    if (btn == &playButton)
    {
        playing = !playing;  // toggle play/pause
        playButton.setToggleState(playing, juce::dontSendNotification);
        playButton.setButtonText(playing ? "PAUSE" : "PLAY");
        if (playing) { if (onPlay) onPlay(); }
        else         { if (onStop) onStop(); }
    }
    else if (btn == &stopButton)
    {
        playing = false;
        playButton.setToggleState(false, juce::dontSendNotification);
        playButton.setButtonText("PLAY");
        if (onStop) onStop();
    }
}

void ToolbarComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &bpmSlider)
    {
        bpmLabel.setText(juce::String(bpmSlider.getValue(), 1), juce::dontSendNotification);
        if (onBPMChanged) onBPMChanged(bpmSlider.getValue());
    }
    else if (slider == &masterVolSlider)
    {
        if (onMasterVolChanged) onMasterVolChanged(masterVolSlider.getValue());
    }
}

void ToolbarComponent::comboBoxChanged(juce::ComboBox* /*box*/)
{
    // patternBox uses onChange lambda; no other combo boxes need listener handling
}

// ---------------------------------------------------------------------------

void ToolbarComponent::drawTapeReel(juce::Graphics& g, float cx, float cy, float r) const
{
    using LF = StudioLookAndFeel;

    // Outer rim — reference: radial-gradient(circle at 30% 25%, #4a4338, #1a1612 70%, #0a0805)
    {
        juce::ColourGradient grad(juce::Colour(0xff4a4338), cx - r*0.4f, cy - r*0.5f,
                                  juce::Colour(0xff0a0805), cx + r*0.5f, cy + r*0.5f, true);
        grad.addColour(0.7, juce::Colour(0xff1a1612));
        g.setGradientFill(grad);
        g.fillEllipse(cx - r, cy - r, r*2.0f, r*2.0f);
    }
    // Rim border
    g.setColour(juce::Colour(0x99000000));
    g.drawEllipse(cx - r + 0.5f, cy - r + 0.5f, r*2.0f - 1.0f, r*2.0f - 1.0f, 1.0f);

    // Tape ring — reference: radial-gradient(circle, #2a1f15 0%, #1a1208 60%, #0d0805 100%)
    {
        const float tr = r * 0.60f;
        juce::ColourGradient grad(juce::Colour(0xff2a1f15), cx, cy,
                                  juce::Colour(0xff0d0805), cx + tr, cy, true);
        grad.addColour(0.6, juce::Colour(0xff1a1208));
        g.setGradientFill(grad);
        g.fillEllipse(cx - tr, cy - tr, tr*2.0f, tr*2.0f);
    }

    // 3 spokes at 0°,60°,120° — reference: linear-gradient(90deg,#c7bb9a,#888070), h=3px, w=size*0.7
    // Each spoke is a centered rect rotated through center → creates 6-arm star pattern
    {
        const float spokeLen = r * 0.70f;   // half-length (goes both directions)
        const float spokeH   = 3.0f;
        const float angles[] = { 0.0f, 60.0f, 120.0f };
        for (float angleDeg : angles)
        {
            const float a   = juce::degreesToRadians(angleDeg + reelAngle_);
            const float cos = std::cos(a), sin = std::sin(a);
            // Draw as a thick line (both directions from center)
            juce::Path spoke;
            spoke.addRoundedRectangle(-spokeLen, -spokeH * 0.5f,
                                      spokeLen * 2.0f, spokeH, 1.0f);
            juce::AffineTransform xf = juce::AffineTransform::rotation(a).translated(cx, cy);
            // Cream gradient along the spoke
            juce::ColourGradient sg(juce::Colour(0xffc7bb9a), cx - spokeLen*cos, cy - spokeLen*sin,
                                    juce::Colour(0xff888070), cx + spokeLen*cos, cy + spokeLen*sin, false);
            g.setGradientFill(sg);
            g.fillPath(spoke, xf);
        }
    }

    // Hub — reference: radial-gradient(circle at 30% 25%, #f3ecda, #c7bb9a 60%, #8d8268)
    {
        const float hubR = r * 0.28f;
        const float hx   = cx - hubR * 0.4f;
        const float hy   = cy - hubR * 0.5f;
        juce::ColourGradient grad(juce::Colour(0xfff3ecda), hx, hy,
                                  juce::Colour(0xff8d8268), cx + hubR, cy + hubR, true);
        grad.addColour(0.6, juce::Colour(0xffc7bb9a));
        g.setGradientFill(grad);
        g.fillEllipse(cx - hubR, cy - hubR, hubR*2.0f, hubR*2.0f);
        g.setColour(juce::Colour(0x80000000));
        g.drawEllipse(cx - hubR + 0.5f, cy - hubR + 0.5f, hubR*2.0f - 1.0f, hubR*2.0f - 1.0f, 1.0f);
    }

    // Center dot — accent red, 3px diameter
    {
        const float dotR = 1.5f;
        g.setColour(juce::Colour(LF::kAccent));
        g.fillEllipse(cx - dotR, cy - dotR, dotR*2.0f, dotR*2.0f);
    }
}
