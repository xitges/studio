/*
  ==============================================================================

    ToolbarComponent.cpp
    Created: 6 Mar 2026 11:31:42am
    Author:  홍준영

  ==============================================================================
*/

#include "ToolbarComponent.h"

ToolbarComponent::ToolbarComponent()
{
    // ---- Row 1: Transport buttons
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(recordButton);
    playButton.addListener(this);
    stopButton.addListener(this);
    recordButton.addListener(this);

    playButton  .setColour(juce::TextButton::buttonColourId, juce::Colour(0xffb0b0b8));
    playButton  .setColour(juce::TextButton::textColourOffId, juce::Colour(0xff000000));
    stopButton  .setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3c));
    recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffff453a));
    recordButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffffff));

    // ---- Row 1: Play mode combo
    addAndMakeVisible(playModeBox);
    playModeBox.addItem("Pattern", (int)PlayMode::Pattern + 1);
    playModeBox.addItem("Song",    (int)PlayMode::Song    + 1);
    playModeBox.setSelectedId((int)PlayMode::Pattern + 1, juce::dontSendNotification);
    playModeBox.addListener(this);
    playModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff142014));
    playModeBox.setColour(juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    playModeBox.setColour(juce::ComboBox::textColourId,       juce::Colours::white);

    // ---- Row 1: BPM
    addAndMakeVisible(bpmSlider);
    bpmSlider.setRange(60.0, 200.0, 1.0);
    bpmSlider.setValue(70.0);
    bpmSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
    bpmSlider.addListener(this);
    bpmSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffb0b0b8));

    addAndMakeVisible(bpmLabel);
    bpmLabel.setText("BPM", juce::dontSendNotification);
    bpmLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    bpmLabel.setJustificationType(juce::Justification::centred);

    // ---- Row 1: Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("Studio", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions().withHeight(18.0f).withStyle("Bold")));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffecf0f1));
    titleLabel.setJustificationType(juce::Justification::centredRight);

    // ---- Row 2: Pattern label
    addAndMakeVisible(patternLabel);
    patternLabel.setText("PATTERN", juce::dontSendNotification);
    patternLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    patternLabel.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
    patternLabel.setJustificationType(juce::Justification::centredRight);

    // ---- Row 2: Pattern combo (items populated via updatePatternList)
    addAndMakeVisible(patternBox);
    patternBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff142014));
    patternBox.setColour(juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    patternBox.setColour(juce::ComboBox::textColourId,       juce::Colours::white);
    patternBox.onChange = [this]
    {
        const int id = patternBox.getSelectedId();
        if (id > 0 && onPatternSelected)
            onPatternSelected(id);
    };

    // ---- Row 2: New / Duplicate / Delete pattern buttons
    addAndMakeVisible(newPatBtn);
    newPatBtn.setTooltip("New pattern");
    newPatBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff484850));
    newPatBtn.onClick = [this] { if (onNewPattern) onNewPattern(); };

    addAndMakeVisible(dupPatBtn);
    dupPatBtn.setTooltip("Duplicate pattern");
    dupPatBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    dupPatBtn.onClick = [this] { if (onDuplicatePattern) onDuplicatePattern(); };

    addAndMakeVisible(delPatBtn);
    delPatBtn.setTooltip("Delete pattern");
    delPatBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a1414));
    delPatBtn.onClick = [this] { if (onDeletePattern) onDeletePattern(); };

    addAndMakeVisible(renamePatBtn);
    renamePatBtn.setTooltip("Rename pattern");
    renamePatBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    renamePatBtn.onClick = [this] { if (onRenamePattern) onRenamePattern(); };

    // ---- M4: File buttons (row 2, right side)
    addAndMakeVisible(newFileBtn);
    newFileBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    newFileBtn.onClick = [this] { if (onNewFile) onNewFile(); };

    addAndMakeVisible(openFileBtn);
    openFileBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    openFileBtn.onClick = [this] { if (onOpenFile) onOpenFile(); };

    addAndMakeVisible(saveFileBtn);
    saveFileBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff484850));
    saveFileBtn.onClick = [this] { if (onSaveFile) onSaveFile(); };

    addAndMakeVisible(saveAsFileBtn);
    saveAsFileBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    saveAsFileBtn.onClick = [this] { if (onSaveFileAs) onSaveFileAs(); };

    addAndMakeVisible(exportBtn);
    exportBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a4a1a));
    exportBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    exportBtn.onClick = [this] { if (onExport) onExport(); };

    addAndMakeVisible(mixerBtn);
    mixerBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    mixerBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    mixerBtn.onClick = [this] { if (onToggleMixer) onToggleMixer(); };

    addAndMakeVisible(audioBtn);
    audioBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    audioBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    audioBtn.onClick = [this] { if (onAudioButton) onAudioButton(); };

    addAndMakeVisible(midiBtn);
    midiBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    midiBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    midiBtn.onClick = [this] { if (onMidiButton) onMidiButton(); };

    addAndMakeVisible(launchpadBtn);
    launchpadBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    launchpadBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    launchpadBtn.onClick = [this] { if (onToggleLaunchpad) onToggleLaunchpad(); };

}

ToolbarComponent::~ToolbarComponent() {}

// ---------------------------------------------------------------------------

void ToolbarComponent::setProjectTitle(const juce::String& filename, bool dirty)
{
    juce::String name = filename.isEmpty() ? "Untitled" : filename;
    juce::String title = "Studio  -  ";
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

// ---------------------------------------------------------------------------

void ToolbarComponent::paint(juce::Graphics& g)
{
    // macOS toolbar: dark surface
    g.fillAll(juce::Colour(0xff1c1c1e));

    // Row divider — subtle white separator
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawLine(0.0f, 40.0f, (float)getWidth(), 40.0f, 1.0f);

    // Bottom border — green accent line
    g.setColour(juce::Colour(0xffb0b0b8).withAlpha(0.5f));
    g.fillRect(0.0f, (float)(getHeight() - 2), (float)getWidth(), 2.0f);
}

void ToolbarComponent::resized()
{
    // ---- Row 1: top 40px
    {
        auto row = getLocalBounds().removeFromTop(40).reduced(6, 4);

        playButton  .setBounds(row.removeFromLeft(44).reduced(2));
        stopButton  .setBounds(row.removeFromLeft(44).reduced(2));
        recordButton.setBounds(row.removeFromLeft(44).reduced(2));
        row.removeFromLeft(12);

        playModeBox.setBounds(row.removeFromLeft(90).reduced(2));
        row.removeFromLeft(12);

        bpmLabel .setBounds(row.removeFromLeft(36).reduced(2));
        bpmSlider.setBounds(row.removeFromLeft(60).reduced(2));
        row.removeFromLeft(8);
        mixerBtn .setBounds(row.removeFromLeft(54).reduced(2));
        row.removeFromLeft(4);
        audioBtn    .setBounds(row.removeFromLeft(56).reduced(2));
        row.removeFromLeft(4);
        midiBtn     .setBounds(row.removeFromLeft(48).reduced(2));
        row.removeFromLeft(4);
        launchpadBtn.setBounds(row.removeFromLeft(44).reduced(2));

        titleLabel.setBounds(row);
    }

    // ---- Row 2: bottom 40px
    {
        auto row = getLocalBounds().removeFromBottom(40).reduced(6, 4);

        patternLabel.setBounds(row.removeFromLeft(58).reduced(2));
        patternBox  .setBounds(row.removeFromLeft(140).reduced(2));
        row.removeFromLeft(6);

        newPatBtn   .setBounds(row.removeFromLeft(28).reduced(2));
        dupPatBtn   .setBounds(row.removeFromLeft(28).reduced(2));
        delPatBtn   .setBounds(row.removeFromLeft(28).reduced(2));
        row.removeFromLeft(6);
        renamePatBtn.setBounds(row.removeFromLeft(56).reduced(2));

        // File + Export buttons on the right side
        exportBtn    .setBounds(row.removeFromRight(76).reduced(2));
        saveAsFileBtn.setBounds(row.removeFromRight(58).reduced(2));
        saveFileBtn  .setBounds(row.removeFromRight(46).reduced(2));
        openFileBtn  .setBounds(row.removeFromRight(46).reduced(2));
        newFileBtn   .setBounds(row.removeFromRight(40).reduced(2));
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
