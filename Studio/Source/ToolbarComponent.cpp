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
    // ---- Row 1: Transport buttons
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(recordButton);
    playButton.setButtonText("PLAY");
    stopButton.setButtonText("STOP");
    recordButton.setButtonText("REC");
    playButton.addListener(this);
    stopButton.addListener(this);

    playButton  .setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
    playButton  .setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kText));
    stopButton  .setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));

    // Rec button
    recordButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
    recordButton.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kLedRed));
    recordButton.onClick = [this]
    {
        if (recordingActive_)
        {
            if (onRecordStop) onRecordStop();
        }
        else
        {
            if (onRecordStart) onRecordStart();
        }
    };

    // ---- Row 1: Recording elapsed time label
    addAndMakeVisible(recTimeLabel);
    recTimeLabel.setText("", juce::dontSendNotification);
    recTimeLabel.setColour(juce::Label::textColourId, juce::Colour(StudioLookAndFeel::kLedRed));
    recTimeLabel.setFont(StudioLookAndFeel::displayFont(18.0f));
    recTimeLabel.setJustificationType(juce::Justification::centredLeft);

    // ---- Row 1: Play mode combo
    addAndMakeVisible(playModeBox);
    playModeBox.addItem("Pattern", (int)PlayMode::Pattern + 1);
    playModeBox.addItem("Song",    (int)PlayMode::Song    + 1);
    playModeBox.setSelectedId((int)PlayMode::Pattern + 1, juce::dontSendNotification);
    playModeBox.addListener(this);
    playModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(StudioLookAndFeel::kChassis2));
    playModeBox.setColour(juce::ComboBox::outlineColourId,    juce::Colour(StudioLookAndFeel::kPanelRim));
    playModeBox.setColour(juce::ComboBox::textColourId,       juce::Colour(StudioLookAndFeel::kText));

    // ---- Row 1: BPM
    addAndMakeVisible(bpmSlider);
    bpmSlider.setRange(60.0, 200.0, 1.0);
    bpmSlider.setValue(70.0);
    bpmSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    bpmSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    bpmSlider.addListener(this);
    bpmSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(StudioLookAndFeel::kAccent));

    addAndMakeVisible(bpmLabel);
    bpmLabel.setText("BPM  /  4-4", juce::dontSendNotification);
    bpmLabel.setColour(juce::Label::textColourId, juce::Colour(StudioLookAndFeel::kTextDim));
    bpmLabel.setFont(StudioLookAndFeel::monoFont(8.5f, juce::Font::bold));
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
    if (!active)
    {
        recordButton.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
        recordButton.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kLedRed));
        recordButton.setButtonText("Rec");
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

    // Animate tape reels when playing
    if (playing)
    {
        reelAngle_ = std::fmod(reelAngle_ + 2.5f, 360.0f);
        repaint(0, 0, 240, 40);
    }

    if (recordingActive_)
    {
        // Solid red with pulsing brightness
        const bool bright = (blinkCounter_ % 15) < 10;
        recordButton.setColour(juce::TextButton::buttonColourId,
            bright ? juce::Colour(StudioLookAndFeel::kAccent) : juce::Colour(StudioLookAndFeel::kAccent).darker(0.3f));
        recordButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffffff));
        recordButton.setButtonText("REC");

        // Update elapsed time
        const int mins = (int)(recElapsedSec_ / 60.0);
        const int secs = (int)(recElapsedSec_) % 60;
        recTimeLabel.setText(juce::String::formatted("%d:%02d", mins, secs),
                            juce::dontSendNotification);

        // Repaint level meter area
        repaint(recordButton.getRight(), 4, 54, 32);
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

    // Cream chassis gradient top-to-bottom
    juce::ColourGradient bg(juce::Colour(LF::kChassis),  0.0f, 0.0f,
                             juce::Colour(LF::kChassis2), 0.0f, (float)getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();

    // Row divider
    g.setColour(juce::Colour(LF::kPanelRim));
    g.drawLine(0.0f, 40.0f, (float)getWidth(), 40.0f, 1.0f);

    // Framed display band behind title and transport details
    g.setColour(juce::Colour(LF::kPanel).withAlpha(0.55f));
    g.fillRoundedRectangle(246.0f, 4.0f, (float)getWidth() - 252.0f, 32.0f, 6.0f);
    g.setColour(juce::Colours::white.withAlpha(0.20f));
    g.drawRoundedRectangle(246.0f, 4.0f, (float)getWidth() - 252.0f, 32.0f, 6.0f, 1.0f);

    // Bottom border accent line
    g.setColour(juce::Colour(LF::kAccent).withAlpha(0.6f));
    g.fillRect(0.0f, (float)(getHeight() - 2), (float)getWidth(), 2.0f);

    // --- Brand area (left 240px, row 1) ---
    {
        // Tape reels
        drawTapeReel(g, 22.0f, 20.0f, 14.0f);
        drawTapeReel(g, 50.0f, 20.0f, 14.0f);

        // Brand name
        g.setFont(juce::Font(juce::FontOptions().withHeight(15.0f).withStyle("Bold")));
        g.setFont(StudioLookAndFeel::brandFont(18.0f, juce::Font::bold));
        g.setColour(juce::Colour(LF::kText));
        g.drawText("fieldlab", 70, 4, 84, 18, juce::Justification::centredLeft);
        g.setColour(juce::Colour(LF::kAccent));
        g.drawText(".", 138, 4, 10, 18, juce::Justification::centredLeft);

        // Subtitle
        g.setFont(StudioLookAndFeel::monoFont(8.5f, juce::Font::bold));
        g.setColour(juce::Colour(LF::kTextFaint));
        g.drawText("TR-1  -  TABLETOP DAW", 70, 25, 150, 12, juce::Justification::centredLeft);

        // Vertical separator
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine(240.0f, 4.0f, 240.0f, 36.0f, 1.0f);
    }

    // BPM display box beside the knob, modeled after the reference segment display
    {
        auto bpmBox = bpmSlider.getBounds().withTrimmedTop(2).withTrimmedBottom(8).translated(66, -2);
        bpmBox.setWidth(64);
        g.setColour(juce::Colour(LF::kDisplayBg));
        g.fillRoundedRectangle(bpmBox.toFloat(), 4.0f);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawRoundedRectangle(bpmBox.toFloat(), 4.0f, 1.0f);

        g.setColour(juce::Colour(LF::kDisplayFg));
        g.setFont(StudioLookAndFeel::displayFont(22.0f));
        g.drawText(juce::String(bpmSlider.getValue(), 1), bpmBox, juce::Justification::centred);
    }

    // --- Input level meter (stereo) ---
    if (recordingActive_)
    {
        const int meterX = recordButton.getRight() + 4;
        const int meterY = 10;
        const int meterW = 46;
        const int meterH = 8;
        const int gap    = 4;

        auto drawMeter = [&](int y, float level)
        {
            if (std::isnan(level) || std::isinf(level) || level < 0.0f)
                level = 0.0f;
            g.setColour(juce::Colour(LF::kDark));
            g.fillRoundedRectangle((float)meterX, (float)y, (float)meterW, (float)meterH, 2.0f);
            const float w = juce::jmin(level, 1.0f) * (float)meterW;
            if (w > 0.0f)
            {
                juce::Colour col = level < 0.7f ? juce::Colour(LF::kLedGreen)
                                 : level < 0.9f ? juce::Colour(LF::kLedAmber)
                                 :                juce::Colour(LF::kLedRed);
                g.setColour(col);
                g.fillRoundedRectangle((float)meterX, (float)y, w, (float)meterH, 2.0f);
            }
        };
        drawMeter(meterY, inputLevelL_);
        drawMeter(meterY + meterH + gap, inputLevelR_);
    }
}

void ToolbarComponent::resized()
{
    // ---- Row 1: top 40px
    {
        auto row = getLocalBounds().removeFromTop(40).reduced(6, 4);
        row.removeFromLeft(240);   // skip brand area

        playButton  .setBounds(row.removeFromLeft(52).reduced(2));
        stopButton  .setBounds(row.removeFromLeft(52).reduced(2));
        recordButton.setBounds(row.removeFromLeft(52).reduced(2));
        row.removeFromLeft(50);   // space for input level meter
        recTimeLabel.setBounds(row.removeFromLeft(40).reduced(2));
        row.removeFromLeft(4);

        playModeBox.setBounds(row.removeFromLeft(96).reduced(2));
        row.removeFromLeft(12);

        bpmLabel .setBounds(row.removeFromLeft(54).reduced(2));
        bpmSlider.setBounds(row.removeFromLeft(62).reduced(2));
        row.removeFromLeft(76);
        mixerBtn .setBounds(row.removeFromLeft(46).reduced(2));
        row.removeFromLeft(4);
        audioBtn    .setBounds(row.removeFromLeft(60).reduced(2));
        row.removeFromLeft(4);
        midiBtn     .setBounds(row.removeFromLeft(52).reduced(2));
        row.removeFromLeft(4);
        launchpadBtn.setBounds(row.removeFromLeft(46).reduced(2));
        row.removeFromLeft(4);
        trackpadBtn .setBounds(row.removeFromLeft(58).reduced(2));
        row.removeFromLeft(4);
        liveModeBtn_.setBounds(row.removeFromLeft(52).reduced(2));

        titleLabel.setBounds(row.reduced(8, 0));
    }

    // ---- Row 2: bottom 40px
    {
        auto row = getLocalBounds().removeFromBottom(40).reduced(6, 4);

        patternLabel.setBounds(row.removeFromLeft(58).reduced(2));
        patternBox  .setBounds(row.removeFromLeft(140).reduced(2));
        row.removeFromLeft(6);

        newPatBtn   .setBounds(row.removeFromLeft(24).reduced(2));
        dupPatBtn   .setBounds(row.removeFromLeft(40).reduced(2));
        delPatBtn   .setBounds(row.removeFromLeft(34).reduced(2));
        row.removeFromLeft(6);
        renamePatBtn.setBounds(row.removeFromLeft(50).reduced(2));

        // File + Export buttons on the right side
        exportStemsBtn.setBounds(row.removeFromRight(54).reduced(2));
        exportBtn     .setBounds(row.removeFromRight(68).reduced(2));
        saveAsFileBtn .setBounds(row.removeFromRight(62).reduced(2));
        saveFileBtn   .setBounds(row.removeFromRight(48).reduced(2));
        openFileBtn   .setBounds(row.removeFromRight(48).reduced(2));
        newFileBtn    .setBounds(row.removeFromRight(44).reduced(2));
    }
}

// ---------------------------------------------------------------------------

void ToolbarComponent::buttonClicked(juce::Button* btn)
{
    if (btn == &playButton)
    {
        playing = true;
        if (onPlay) onPlay();
    }
    else if (btn == &stopButton)
    {
        playing = false;
        if (onStop) onStop();
    }
}

void ToolbarComponent::sliderValueChanged(juce::Slider* slider)
{
    if (slider == &bpmSlider)
        if (onBPMChanged) onBPMChanged(bpmSlider.getValue());
}

void ToolbarComponent::comboBoxChanged(juce::ComboBox* box)
{
    if (box == &playModeBox)
    {
        playMode = (playModeBox.getSelectedId() == (int)PlayMode::Song + 1)
                   ? PlayMode::Song : PlayMode::Pattern;
        if (onPlayModeChanged) onPlayModeChanged(playMode);
    }
    // patternBox uses onChange lambda — no handling needed here
}

// ---------------------------------------------------------------------------

void ToolbarComponent::drawTapeReel(juce::Graphics& g, float cx, float cy, float r) const
{
    using LF = StudioLookAndFeel;

    // Outer body
    juce::ColourGradient outerGrad(juce::Colour(0xff4a4338), cx - r, cy - r,
                                   juce::Colour(0xff1a1612), cx + r, cy + r, true);
    g.setGradientFill(outerGrad);
    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);

    // Tape track ring
    const float tapeR = r * 0.60f;
    g.setColour(juce::Colour(0xff2a2018));
    g.fillEllipse(cx - tapeR, cy - tapeR, tapeR * 2.0f, tapeR * 2.0f);

    // 3 spokes rotating with reelAngle_
    const float innerR = r * 0.28f;
    g.setColour(juce::Colour(0xff3a3328));
    for (int i = 0; i < 3; ++i)
    {
        const float a = juce::degreesToRadians(reelAngle_)
                        + i * juce::MathConstants<float>::twoPi / 3.0f;
        g.drawLine(cx + innerR * std::cos(a), cy + innerR * std::sin(a),
                   cx + tapeR * 0.82f * std::cos(a), cy + tapeR * 0.82f * std::sin(a),
                   2.0f);
    }

    // Hub
    const float hubR = r * 0.22f;
    juce::ColourGradient hubGrad(juce::Colour(LF::kChassis), cx - hubR, cy - hubR,
                                 juce::Colour(LF::kPanelRim), cx + hubR, cy + hubR, true);
    g.setGradientFill(hubGrad);
    g.fillEllipse(cx - hubR, cy - hubR, hubR * 2.0f, hubR * 2.0f);

    // Red center dot
    const float dotR = r * 0.09f;
    g.setColour(juce::Colour(LF::kAccent));
    g.fillEllipse(cx - dotR, cy - dotR, dotR * 2.0f, dotR * 2.0f);
}
