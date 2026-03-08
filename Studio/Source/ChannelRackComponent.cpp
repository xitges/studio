#include "ChannelRackComponent.h"

ChannelRackComponent::ChannelRackComponent()
{
    addChannel("Kick");
    addChannel("Snare");
    addChannel("HiHat");

    // Default beat pattern
    channels[0].steps[0]  = true;
    channels[0].steps[4]  = true;
    channels[0].steps[8]  = true;
    channels[0].steps[12] = true;

    channels[1].steps[4]  = true;
    channels[1].steps[12] = true;

    for (int i = 0; i < 16; ++i)
        channels[2].steps[i] = true;

    // Add Channel button
    addAndMakeVisible(addChannelBtn);
    addChannelBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0f3460));
    addChannelBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addChannelBtn.onClick = [this]
    {
        addChannel("Channel " + juce::String(channels.size() + 1));
        resized();
        repaint();
    };

    // Step count slider — any value 1..kMaxSteps by 1
    addAndMakeVisible(stepCountSlider);
    stepCountSlider.setRange(1, Pattern::kMaxSteps, 1);
    stepCountSlider.setValue(16, juce::dontSendNotification);
    stepCountSlider.setSliderStyle(juce::Slider::IncDecButtons);
    stepCountSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 24);
    stepCountSlider.setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
    stepCountSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff16213e));
    stepCountSlider.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff0f3460));
    stepCountSlider.onValueChange = [this]
    {
        stepCount = (int)stepCountSlider.getValue();
        repaint();
    };
    stepCount = 16;

    startTimerHz(30);
}

ChannelRackComponent::~ChannelRackComponent()
{
    stopTimer();
}

// ---------------------------------------------------------------------------

void ChannelRackComponent::addChannel(const juce::String& name)
{
    ChannelRow row;
    row.name = name;

    const int ch = (int)channels.size();

    // ---- Mute button
    row.muteBtn = std::make_unique<juce::TextButton>("M");
    row.muteBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2c2c54));
    row.muteBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    row.muteBtn->onClick = [this, ch]
    {
        channels[ch].muted = !channels[ch].muted;
        channels[ch].muteBtn->setColour(juce::TextButton::buttonColourId,
            channels[ch].muted ? juce::Colour(0xffe74c3c) : juce::Colour(0xff2c2c54));
        if (onMuteChanged) onMuteChanged(ch, channels[ch].muted);
    };
    addAndMakeVisible(*row.muteBtn);

    // ---- Solo button
    row.soloBtn = std::make_unique<juce::TextButton>("S");
    row.soloBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2c2c54));
    row.soloBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    row.soloBtn->onClick = [this, ch]
    {
        channels[ch].soloed = !channels[ch].soloed;
        channels[ch].soloBtn->setColour(juce::TextButton::buttonColourId,
            channels[ch].soloed ? juce::Colour(0xfff39c12) : juce::Colour(0xff2c2c54));
        if (onSoloChanged) onSoloChanged(ch, channels[ch].soloed);
    };
    addAndMakeVisible(*row.soloBtn);

    // ---- M1.1 Volume slider
    row.volSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                    juce::Slider::TextBoxRight);
    row.volSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 14);
    row.volSlider->setNumDecimalPlacesToDisplay(2);
    row.volSlider->setRange(0.0, 1.0, 0.01);
    row.volSlider->setValue(row.volume, juce::dontSendNotification);
    row.volSlider->setColour(juce::Slider::thumbColourId,             juce::Colour(0xff3498db));
    row.volSlider->setColour(juce::Slider::trackColourId,             juce::Colour(0xff3498db));
    row.volSlider->setColour(juce::Slider::backgroundColourId,        juce::Colour(0xff1a1a2e));
    row.volSlider->setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
    row.volSlider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff16213e));
    row.volSlider->setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff0f3460));
    row.volSlider->onValueChange = [this, ch]
    {
        float v = (float)channels[ch].volSlider->getValue();
        channels[ch].volume = v;
        if (onVolumeChanged) onVolumeChanged(ch, v);
    };
    addAndMakeVisible(*row.volSlider);

    // ---- M1.1 Pan slider
    row.panSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                    juce::Slider::TextBoxRight);
    row.panSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 14);
    row.panSlider->setNumDecimalPlacesToDisplay(2);
    row.panSlider->setRange(-1.0, 1.0, 0.01);
    row.panSlider->setValue(row.pan, juce::dontSendNotification);
    row.panSlider->setColour(juce::Slider::thumbColourId,             juce::Colour(0xffe67e22));
    row.panSlider->setColour(juce::Slider::trackColourId,             juce::Colour(0xffe67e22));
    row.panSlider->setColour(juce::Slider::backgroundColourId,        juce::Colour(0xff1a1a2e));
    row.panSlider->setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
    row.panSlider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff16213e));
    row.panSlider->setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff0f3460));
    row.panSlider->onValueChange = [this, ch]
    {
        float p = (float)channels[ch].panSlider->getValue();
        channels[ch].pan = p;
        if (onPanChanged) onPanChanged(ch, p);
    };
    addAndMakeVisible(*row.panSlider);

    // ---- M1.2 Pitch slider (horizontal, third row)
    row.pitchSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                      juce::Slider::TextBoxRight);
    row.pitchSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 14);
    row.pitchSlider->setNumDecimalPlacesToDisplay(1);
    row.pitchSlider->setTextValueSuffix(" st");
    row.pitchSlider->setRange(-24.0, 24.0, 0.5);
    row.pitchSlider->setValue(row.pitch, juce::dontSendNotification);
    row.pitchSlider->setColour(juce::Slider::thumbColourId,             juce::Colour(0xff2ecc71));
    row.pitchSlider->setColour(juce::Slider::trackColourId,             juce::Colour(0xff2ecc71));
    row.pitchSlider->setColour(juce::Slider::backgroundColourId,        juce::Colour(0xff1a1a2e));
    row.pitchSlider->setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
    row.pitchSlider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff16213e));
    row.pitchSlider->setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff0f3460));
    row.pitchSlider->onValueChange = [this, ch]
    {
        float s = (float)channels[ch].pitchSlider->getValue();
        channels[ch].pitch = s;
        if (onPitchChanged) onPitchChanged(ch, s);
    };
    addAndMakeVisible(*row.pitchSlider);

    channels.push_back(std::move(row));
}

// ---------------------------------------------------------------------------

// M2.1 — Load pattern steps into the UI grid
void ChannelRackComponent::loadPattern(const Pattern& pat)
{
    // Sync step count
    stepCount = juce::jlimit(1, Pattern::kMaxSteps, pat.stepCount);
    stepCountSlider.setValue(stepCount, juce::dontSendNotification);

    // Copy step data, sample name, and per-pattern vol/pan/pitch for each channel
    for (int ch = 0; ch < (int)channels.size() && ch < Pattern::kMaxChannels; ++ch)
    {
        for (int s = 0; s < Pattern::kMaxSteps; ++s)
            channels[ch].steps[s] = pat.steps[ch][s];

        if (pat.samplePaths[ch].isNotEmpty())
            channels[ch].sampleName = juce::File(pat.samplePaths[ch]).getFileNameWithoutExtension();
        else
            channels[ch].sampleName = "Drop Sample";

        // Restore vol/pan/pitch — fires onValueChange → audio engine syncs
        channels[ch].volume = pat.channelVolume[ch];
        channels[ch].pan    = pat.channelPan[ch];
        channels[ch].pitch  = pat.channelPitch[ch];
        if (channels[ch].volSlider)
            channels[ch].volSlider->setValue(pat.channelVolume[ch], juce::sendNotification);
        if (channels[ch].panSlider)
            channels[ch].panSlider->setValue(pat.channelPan[ch], juce::sendNotification);
        if (channels[ch].pitchSlider)
            channels[ch].pitchSlider->setValue(pat.channelPitch[ch], juce::sendNotification);
    }

    repaint();
}

// M2.1 — Write UI grid state back into a Pattern
void ChannelRackComponent::saveToPattern(Pattern& pat) const
{
    pat.stepCount = stepCount;
    for (int ch = 0; ch < (int)channels.size() && ch < Pattern::kMaxChannels; ++ch)
    {
        for (int s = 0; s < Pattern::kMaxSteps; ++s)
            pat.steps[ch][s] = channels[ch].steps[s];

        pat.channelVolume[ch] = channels[ch].volume;
        pat.channelPan[ch]    = channels[ch].pan;
        pat.channelPitch[ch]  = channels[ch].pitch;
    }
}

// Reset channel list to match a saved project's channel count and names
void ChannelRackComponent::resetToChannelCount(int count, const juce::String* names)
{
    // Remove all existing child components
    for (auto& row : channels)
    {
        if (row.muteBtn)    removeChildComponent(row.muteBtn.get());
        if (row.soloBtn)    removeChildComponent(row.soloBtn.get());
        if (row.volSlider)  removeChildComponent(row.volSlider.get());
        if (row.panSlider)  removeChildComponent(row.panSlider.get());
        if (row.pitchSlider) removeChildComponent(row.pitchSlider.get());
    }
    channels.clear();

    const int n = juce::jlimit(1, Pattern::kMaxChannels, count);
    for (int i = 0; i < n; ++i)
    {
        const juce::String nm = (names != nullptr && names[i].isNotEmpty())
                                    ? names[i]
                                    : "Channel " + juce::String(i + 1);
        addChannel(nm);
    }

    resized();
    repaint();
}

// ---------------------------------------------------------------------------

void ChannelRackComponent::timerCallback()
{
    if (getCurrentStep)
        setPlaybackStep(getCurrentStep());
}

// ---------------------------------------------------------------------------

void ChannelRackComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
    drawHeader(g);
    drawChannelLabels(g);
    drawStepGrid(g);
}

void ChannelRackComponent::drawHeader(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff16213e));
    g.fillRect(0, 0, getWidth(), HEADER_HEIGHT);

    const float stepW = (getWidth() - LABEL_WIDTH) / (float)stepCount;

    for (int i = 0; i < stepCount; ++i)
    {
        const int x = LABEL_WIDTH + (int)(i * stepW);

        if (i == currentPlayStep)
        {
            g.setColour(juce::Colour(0xff3498db).withAlpha(0.4f));
            g.fillRect(x, 0, (int)stepW, HEADER_HEIGHT);
        }

        g.setColour(i % 4 == 0 ? juce::Colours::white : juce::Colours::grey);
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        g.drawText(juce::String(i + 1), x, 0, (int)stepW, HEADER_HEIGHT,
                   juce::Justification::centred);
    }
}

void ChannelRackComponent::drawChannelLabels(juce::Graphics& g)
{
    for (int i = 0; i < (int)channels.size(); ++i)
    {
        const int y = HEADER_HEIGHT + i * ROW_HEIGHT;

        // Row background
        if (i == dragHoverChannel)
            g.setColour(juce::Colour(0xff2ecc71).withAlpha(0.3f));
        else
            g.setColour(i % 2 == 0 ? juce::Colour(0xff1e1e3a) : juce::Colour(0xff16213e));
        g.fillRect(0, y, LABEL_WIDTH, ROW_HEIGHT);

        // Channel name (top half, left area)
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        g.drawText(channels[i].name, 8, y + 2, 86, 18,
                   juce::Justification::centredLeft);

        // Sample name / type badge (bottom half, left area)
        const bool isMelodic = (i < (int)channelTypes.size() &&
                                 channelTypes[(size_t)i] == ChannelType::Melodic);
        if (isMelodic)
        {
            g.setColour(juce::Colour(0xff2ecc71).withAlpha(0.25f));
            g.fillRoundedRectangle(6.0f, (float)(y + 23), 38.0f, 14.0f, 3.0f);
            g.setColour(juce::Colour(0xff2ecc71));
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            g.drawText("MELODIC", 6, y + 23, 38, 14, juce::Justification::centred);
        }
        else
        {
            g.setColour(juce::Colour(0xff3498db));
            g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
            g.drawText(channels[(size_t)i].sampleName, 8, y + 24, 86, 16,
                       juce::Justification::centredLeft);
        }

        // Small labels left of sliders
        g.setColour(juce::Colours::grey);
        g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
        g.drawText("V", 88, y + 4,  8, 10, juce::Justification::centred);
        g.drawText("P", 88, y + 22, 8, 10, juce::Justification::centred);
        g.drawText("T", 88, y + 40, 8, 10, juce::Justification::centred);

        // Separator line
        g.setColour(juce::Colour(0xff0f3460));
        g.drawLine(0.0f, (float)(y + ROW_HEIGHT), (float)LABEL_WIDTH,
                   (float)(y + ROW_HEIGHT), 1.0f);
    }
}

void ChannelRackComponent::drawStepGrid(juce::Graphics& g)
{
    const int   stepAreaX = LABEL_WIDTH;
    const float stepW     = (getWidth() - stepAreaX) / (float)stepCount;

    for (int ch = 0; ch < (int)channels.size(); ++ch)
    {
        const int y = HEADER_HEIGHT + ch * ROW_HEIGHT;

        for (int s = 0; s < stepCount; ++s)
        {
            const int x = stepAreaX + (int)(s * stepW);
            const int w = (int)stepW - 2;
            const int h = ROW_HEIGHT - 4;

            const bool isCurrentStep = (s == currentPlayStep);

            if (channels[ch].steps[s])
            {
                juce::Colour c = isCurrentStep
                    ? juce::Colour(0xffffffff)
                    : (s % 4 == 0 ? juce::Colour(0xff3498db) : juce::Colour(0xff2980b9));
                g.setColour(c);
                g.fillRoundedRectangle(x + 1, y + 2, w, h, 4.0f);
            }
            else
            {
                juce::Colour c = isCurrentStep ? juce::Colour(0xff555588)
                                               : juce::Colour(0xff2c2c54);
                g.setColour(c);
                g.fillRoundedRectangle(x + 1, y + 2, w, h, 4.0f);

                if (!isCurrentStep)
                {
                    g.setColour(juce::Colour(0xff3d3d6b));
                    g.drawRoundedRectangle(x + 1, y + 2, w, h, 4.0f, 1.0f);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------

void ChannelRackComponent::mouseDown(const juce::MouseEvent& e)
{
    const int stepAreaX = LABEL_WIDTH;
    const float stepW   = (getWidth() - stepAreaX) / (float)stepCount;

    const int x = e.getPosition().getX();
    const int y = e.getPosition().getY() - HEADER_HEIGHT;

    if (y < 0) return;

    const int ch = y / ROW_HEIGHT;

    // Right-click on label area → channel context menu (M3)
    if (e.mods.isRightButtonDown() && x < stepAreaX
        && ch >= 0 && ch < (int)channels.size())
    {
        const bool isMelodic = (channelTypes[(size_t)ch] == ChannelType::Melodic);
        juce::PopupMenu menu;
        menu.addItem(1, "Rename");
        menu.addSeparator();
        menu.addItem(2, isMelodic ? "Switch to Drum" : "Switch to Melodic");
        if (isMelodic)
            menu.addItem(3, "Open Piano Roll");
        menu.addSeparator();
        menu.addItem(4, "Open Synth Editor");   // M13

        menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
            [this, ch, isMelodic](int result)
            {
                if (ch < 0 || ch >= (int)channels.size()) return;
                if (result == 1)
                {
                    auto* dialog = new juce::AlertWindow("Rename Channel",
                                                          "Enter a new name:",
                                                          juce::MessageBoxIconType::NoIcon);
                    dialog->addTextEditor("name", channels[(size_t)ch].name);
                    dialog->addButton("OK", 1); dialog->addButton("Cancel", 0);
                    dialog->enterModalState(true,
                        juce::ModalCallbackFunction::create([this, ch, dialog](int r) {
                            if (r == 1) { channels[(size_t)ch].name = dialog->getTextEditorContents("name"); repaint(); }
                            delete dialog;
                        }), false);
                }
                else if (result == 2)
                {
                    const ChannelType newType = isMelodic ? ChannelType::Drum : ChannelType::Melodic;
                    channelTypes[(size_t)ch] = newType;
                    if (onChannelTypeChanged) onChannelTypeChanged(ch, newType);
                    repaint();
                }
                else if (result == 3)
                {
                    if (onOpenPianoRoll) onOpenPianoRoll(ch);
                }
                else if (result == 4)
                {
                    if (onOpenSynthEditor) onOpenSynthEditor(ch);   // M13
                }
            });
        return;
    }

    // Left-click on step grid
    if (x < stepAreaX) return;

    const int s = (int)((x - stepAreaX) / stepW);

    // M3: only Drum channels use the step grid; Melodic channels use the piano roll
    if (ch >= 0 && ch < (int)channels.size() && s >= 0 && s < stepCount
        && channelTypes[(size_t)ch] == ChannelType::Drum)
    {
        const bool oldState = channels[(size_t)ch].steps[(size_t)s];
        channels[(size_t)ch].steps[(size_t)s] = !oldState;
        if (onStepToggled) onStepToggled(ch, s, !oldState, oldState);
        repaint();
    }
    // Melodic channel: double-click opens piano roll (handled in mouseDoubleClick)
}

// M1.5 / M3 — Double-click on channel label area
void ChannelRackComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int x = e.getPosition().getX();
    const int y = e.getPosition().getY() - HEADER_HEIGHT;

    if (x > 94 || y < 0) return;

    const int ch = y / ROW_HEIGHT;
    if (ch < 0 || ch >= (int)channels.size()) return;

    // M3: melodic channels open piano roll on double-click
    if (channelTypes[(size_t)ch] == ChannelType::Melodic)
    {
        if (onOpenPianoRoll) onOpenPianoRoll(ch);
        return;
    }

    auto* dialog = new juce::AlertWindow("Rename Channel",
                                          "Enter a new name:",
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", channels[ch].name);
    dialog->addButton("OK",     1);
    dialog->addButton("Cancel", 0);

    const int chCapture = ch;
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create([this, chCapture, dialog](int result)
        {
            if (result == 1)
            {
                channels[chCapture].name = dialog->getTextEditorContents("name");
                repaint();
            }
            delete dialog;
        }),
        false  // deleteWhenDismissed = false (we delete in callback)
    );
}

// ---------------------------------------------------------------------------

void ChannelRackComponent::resized()
{
    // Compute required height and self-size (enables Viewport scrolling)
    const int needed  = getNeededHeight();
    const int parentH = (getParentComponent() != nullptr)
                            ? getParentComponent()->getHeight()
                            : 400;
    const int desired = juce::jmax(needed, parentH);

    if (getHeight() != desired)
    {
        setSize(getWidth(), desired);
        return;  // setSize triggers resized() again with correct dimensions
    }

    // Bottom controls
    const int controlsY = HEADER_HEIGHT + (int)channels.size() * ROW_HEIGHT + 8;
    addChannelBtn   .setBounds(10,  controlsY, 140, 30);
    stepCountSlider .setBounds(160, controlsY, 100, 30);

    // Per-channel controls layout
    for (int i = 0; i < (int)channels.size(); ++i)
    {
        const int y = HEADER_HEIGHT + i * ROW_HEIGHT;

        // Three horizontal sliders stacked, each with text box on right
        if (channels[i].volSlider)
            channels[i].volSlider->setBounds(98, y + 4, 136, 14);

        if (channels[i].panSlider)
            channels[i].panSlider->setBounds(98, y + 22, 136, 14);

        if (channels[i].pitchSlider)
            channels[i].pitchSlider->setBounds(98, y + 40, 136, 14);

        // Mute / Solo buttons — far right, aligned to rows 1 & 2
        if (channels[i].muteBtn)
            channels[i].muteBtn->setBounds(236, y + 4, 12, 16);
        if (channels[i].soloBtn)
            channels[i].soloBtn->setBounds(236, y + 22, 12, 16);
    }
}

// ---------------------------------------------------------------------------

int ChannelRackComponent::getChannelIndexAt(int y) const
{
    const int relY = y - HEADER_HEIGHT;
    if (relY < 0) return -1;
    const int ch = relY / ROW_HEIGHT;
    return (ch < (int)channels.size()) ? ch : -1;
}

bool ChannelRackComponent::isInterestedInFileDrag(const juce::StringArray&)
{
    return true;
}

void ChannelRackComponent::fileDragEnter(const juce::StringArray&, int, int y)
{
    dragHoverChannel = getChannelIndexAt(y);
    repaint();
}

void ChannelRackComponent::fileDragMove(const juce::StringArray&, int, int y)
{
    const int newHover = getChannelIndexAt(y);
    if (newHover != dragHoverChannel)
    {
        dragHoverChannel = newHover;
        repaint();
    }
}

void ChannelRackComponent::fileDragExit(const juce::StringArray&)
{
    dragHoverChannel = -1;
    repaint();
}

void ChannelRackComponent::filesDropped(const juce::StringArray& files, int, int y)
{
    dragHoverChannel = -1;

    const int ch = getChannelIndexAt(y);
    if (ch < 0) return;

    juce::File file(files[0]);
    channels[ch].sampleName = file.getFileNameWithoutExtension();
    repaint();

    if (onSampleDropped)
        onSampleDropped(ch, file);
}

// M15 — internal drag from SampleBrowserComponent
bool ChannelRackComponent::isInterestedInDragSource(
    const juce::DragAndDropTarget::SourceDetails& details)
{
    const juce::File f(details.description.toString());
    return f.existsAsFile();
}

void ChannelRackComponent::itemDropped(
    const juce::DragAndDropTarget::SourceDetails& details)
{
    const juce::File f(details.description.toString());
    if (!f.existsAsFile()) return;

    const int ch = getChannelIndexAt(details.localPosition.y);
    if (ch < 0 || ch >= (int)channels.size()) return;

    channels[(size_t)ch].sampleName = f.getFileNameWithoutExtension();
    repaint();

    if (onSampleDropped)
        onSampleDropped(ch, f);
}
