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
    addChannelBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff484850));
    addChannelBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addChannelBtn.onClick = [this]
    {
        const juce::String name = "Channel " + juce::String(channels.size() + 1);
        if (onAddChannel)
            onAddChannel(name);   // MainComponent updates project.channelCount + all patterns
        else
        {
            addChannel(name);     // fallback if no callback wired
            resized();
            repaint();
        }
    };

    addAndMakeVisible(clearStepsBtn);
    clearStepsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a2020));
    clearStepsBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffd0d0));
    clearStepsBtn.onClick = [this]
    {
        for (auto& channel : channels)
            for (int s = 0; s < Pattern::kMaxSteps; ++s)
                channel.steps[(size_t)s] = false;

        if (onClearAllSteps) onClearAllSteps();
        repaint();
    };

    // Step count slider — any value 1..kMaxSteps by 1
    addAndMakeVisible(stepCountSlider);
    stepCountSlider.setRange(1, Pattern::kMaxSteps, 1);
    stepCountSlider.setValue(16, juce::dontSendNotification);
    stepCountSlider.setSliderStyle(juce::Slider::IncDecButtons);
    stepCountSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 24);
    stepCountSlider.setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
    stepCountSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1c1c1e));
    stepCountSlider.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff0f3460));
    stepCountSlider.onValueChange = [this]
    {
        stepCount = (int)stepCountSlider.getValue();
        if (onStepCountChanged) onStepCountChanged(stepCount);
        repaint();
    };
    stepCount = 16;

    // Variation A/B/C/D buttons
    auto setupVarBtn = [this](juce::TextButton& btn, int idx)
    {
        btn.setClickingTogglesState(false);
        btn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2a2a3a));
        btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a5fa0));
        btn.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xffa0a0b8));
        btn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
        btn.onClick = [this, idx]
        {
            activeVariation = idx;
            updateVariationButtonStates();
            if (onVariationChanged) onVariationChanged(idx);
        };
        addAndMakeVisible(btn);
    };
    setupVarBtn(varBtnA, 0);
    setupVarBtn(varBtnB, 1);
    setupVarBtn(varBtnC, 2);
    setupVarBtn(varBtnD, 3);
    updateVariationButtonStates();

    // --- Step Inspector setup ---
    // Collapsed by default; openInspector() makes them visible
    inspectorLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    inspectorLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0b0b8));
    addChildComponent(inspectorLabel);

    auto setupInspSlider = [this](juce::Slider& sl, double lo, double hi, double step,
                                  juce::Colour col, const juce::String& suffix)
    {
        sl.setRange(lo, hi, step);
        sl.setSliderStyle(juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 16);
        sl.setNumDecimalPlacesToDisplay(2);
        sl.setTextValueSuffix(suffix);
        sl.setColour(juce::Slider::thumbColourId,             col);
        sl.setColour(juce::Slider::trackColourId,             col.withAlpha(0.7f));
        sl.setColour(juce::Slider::backgroundColourId,        juce::Colour(0xff1e1e24));
        sl.setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
        sl.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff141418));
        sl.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff2a2a35));
        addChildComponent(sl);
    };

    setupInspSlider(inspVelSlider,      0.0,  2.0,  0.01, juce::Colour(0xff3a9ad9), "");
    setupInspSlider(inspGateSlider,     0.0,  2.0,  0.01, juce::Colour(0xff27ae60), "");
    setupInspSlider(inspProbSlider,     0.0,  1.0,  0.01, juce::Colour(0xffe6b32b), "");
    setupInspSlider(inspPitchSlider,  -12.0, 12.0,  1.0,  juce::Colour(0xffb0b0b8), " st");
    inspPitchSlider.setNumDecimalPlacesToDisplay(0);
    setupInspSlider(inspCutoffSlider,  -3.0,  3.0,  0.01, juce::Colour(0xffcc6699), " oct");
    setupInspSlider(inspStartOffSlider, 0.0,  1.0,  0.01, juce::Colour(0xff669966), "");

    inspVelSlider.onValueChange = [this]
    {
        if (inspectorCh < 0 || inspectorStep < 0) return;
        stepParamsStore[inspectorCh][inspectorStep].velocity = (float)inspVelSlider.getValue();
        if (onStepParamsChanged)
            onStepParamsChanged(inspectorCh, inspectorStep, stepParamsStore[inspectorCh][inspectorStep]);
        repaint();
    };
    inspGateSlider.onValueChange = [this]
    {
        if (inspectorCh < 0 || inspectorStep < 0) return;
        stepParamsStore[inspectorCh][inspectorStep].gate = (float)inspGateSlider.getValue();
        if (onStepParamsChanged)
            onStepParamsChanged(inspectorCh, inspectorStep, stepParamsStore[inspectorCh][inspectorStep]);
        repaint();
    };
    inspProbSlider.onValueChange = [this]
    {
        if (inspectorCh < 0 || inspectorStep < 0) return;
        stepParamsStore[inspectorCh][inspectorStep].probability = (float)inspProbSlider.getValue();
        if (onStepParamsChanged)
            onStepParamsChanged(inspectorCh, inspectorStep, stepParamsStore[inspectorCh][inspectorStep]);
        repaint();
    };
    inspPitchSlider.onValueChange = [this]
    {
        if (inspectorCh < 0 || inspectorStep < 0) return;
        stepParamsStore[inspectorCh][inspectorStep].pitchOffset = (int)std::round(inspPitchSlider.getValue());
        if (onStepParamsChanged)
            onStepParamsChanged(inspectorCh, inspectorStep, stepParamsStore[inspectorCh][inspectorStep]);
        repaint();
    };
    inspCutoffSlider.onValueChange = [this]
    {
        if (inspectorCh < 0 || inspectorStep < 0) return;
        stepParamsStore[inspectorCh][inspectorStep].cutoffMod = (float)inspCutoffSlider.getValue();
        if (onStepParamsChanged)
            onStepParamsChanged(inspectorCh, inspectorStep, stepParamsStore[inspectorCh][inspectorStep]);
        repaint();
    };
    inspStartOffSlider.onValueChange = [this]
    {
        if (inspectorCh < 0 || inspectorStep < 0) return;
        stepParamsStore[inspectorCh][inspectorStep].startOffsetFrac = (float)inspStartOffSlider.getValue();
        if (onStepParamsChanged)
            onStepParamsChanged(inspectorCh, inspectorStep, stepParamsStore[inspectorCh][inspectorStep]);
        repaint();
    };

    inspResetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a2020));
    inspResetBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff8888));
    inspResetBtn.onClick = [this]
    {
        if (inspectorCh < 0 || inspectorStep < 0) return;
        stepParamsStore[inspectorCh][inspectorStep].reset();
        refreshInspector();
        if (onStepParamsChanged)
            onStepParamsChanged(inspectorCh, inspectorStep, stepParamsStore[inspectorCh][inspectorStep]);
        repaint();
    };
    addChildComponent(inspResetBtn);

    startTimerHz(30);
}

ChannelRackComponent::~ChannelRackComponent()
{
    stopTimer();
}

void ChannelRackComponent::updateVariationButtonStates()
{
    varBtnA.setColour(juce::TextButton::buttonColourId, activeVariation == 0 ? juce::Colour(0xff3a5fa0) : juce::Colour(0xff2a2a3a));
    varBtnB.setColour(juce::TextButton::buttonColourId, activeVariation == 1 ? juce::Colour(0xff3a5fa0) : juce::Colour(0xff2a2a3a));
    varBtnC.setColour(juce::TextButton::buttonColourId, activeVariation == 2 ? juce::Colour(0xff3a5fa0) : juce::Colour(0xff2a2a3a));
    varBtnD.setColour(juce::TextButton::buttonColourId, activeVariation == 3 ? juce::Colour(0xff3a5fa0) : juce::Colour(0xff2a2a3a));
    repaint();
}

// ---------------------------------------------------------------------------

void ChannelRackComponent::addChannel(const juce::String& name)
{
    ChannelRow row;
    row.name = name;

    const int ch = (int)channels.size();

    // ---- Mute button
    row.muteBtn = std::make_unique<juce::TextButton>("M");
    row.muteBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    row.muteBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    row.muteBtn->onClick = [this, ch]
    {
        channels[ch].muted = !channels[ch].muted;
        channels[ch].muteBtn->setColour(juce::TextButton::buttonColourId,
            channels[ch].muted ? juce::Colour(0xff5a1414) : juce::Colour(0xff323232));
        if (onMuteChanged) onMuteChanged(ch, channels[ch].muted);
    };
    addAndMakeVisible(*row.muteBtn);

    // ---- Solo button
    row.soloBtn = std::make_unique<juce::TextButton>("S");
    row.soloBtn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff323232));
    row.soloBtn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    row.soloBtn->onClick = [this, ch]
    {
        channels[ch].soloed = !channels[ch].soloed;
        channels[ch].soloBtn->setColour(juce::TextButton::buttonColourId,
            channels[ch].soloed ? juce::Colour(0xff6b5000) : juce::Colour(0xff323232));
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
    row.volSlider->setColour(juce::Slider::backgroundColourId,        juce::Colour(0xff2c2c2e));
    row.volSlider->setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
    row.volSlider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1c1c1e));
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
    row.panSlider->setColour(juce::Slider::backgroundColourId,        juce::Colour(0xff2c2c2e));
    row.panSlider->setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
    row.panSlider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1c1c1e));
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
    row.pitchSlider->setColour(juce::Slider::thumbColourId,             juce::Colour(0xffb0b0b8));
    row.pitchSlider->setColour(juce::Slider::trackColourId,             juce::Colour(0xffb0b0b8));
    row.pitchSlider->setColour(juce::Slider::backgroundColourId,        juce::Colour(0xff2c2c2e));
    row.pitchSlider->setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
    row.pitchSlider->setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1c1c1e));
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
// Step Inspector
// ---------------------------------------------------------------------------

void ChannelRackComponent::openInspector(int ch, int step)
{
    inspectorCh   = ch;
    inspectorStep = step;
    refreshInspector();
    inspectorLabel   .setVisible(true);
    inspVelSlider    .setVisible(true);
    inspGateSlider   .setVisible(true);
    inspProbSlider   .setVisible(true);
    inspPitchSlider  .setVisible(true);
    inspCutoffSlider .setVisible(true);
    inspStartOffSlider.setVisible(true);
    inspResetBtn     .setVisible(true);
    layoutInspector();
    repaint();
}

void ChannelRackComponent::closeInspector()
{
    inspectorCh = inspectorStep = -1;
    inspectorLabel   .setVisible(false);
    inspVelSlider    .setVisible(false);
    inspGateSlider   .setVisible(false);
    inspProbSlider   .setVisible(false);
    inspPitchSlider  .setVisible(false);
    inspCutoffSlider .setVisible(false);
    inspStartOffSlider.setVisible(false);
    inspResetBtn     .setVisible(false);
    repaint();
}

void ChannelRackComponent::refreshInspector()
{
    if (inspectorCh < 0 || inspectorStep < 0) return;
    const StepParams& p = stepParamsStore[inspectorCh][inspectorStep];
    inspVelSlider    .setValue(p.velocity,           juce::dontSendNotification);
    inspGateSlider   .setValue(p.gate,               juce::dontSendNotification);
    inspProbSlider   .setValue(p.probability,        juce::dontSendNotification);
    inspPitchSlider  .setValue((double)p.pitchOffset,juce::dontSendNotification);
    inspCutoffSlider .setValue(p.cutoffMod,          juce::dontSendNotification);
    inspStartOffSlider.setValue(p.startOffsetFrac,   juce::dontSendNotification);

    const juce::String chName = (inspectorCh < (int)channels.size())
                                ? channels[(size_t)inspectorCh].name : "Ch";
    inspectorLabel.setText("Step " + juce::String(inspectorStep + 1) + "  \xe2\x80\x94  " + chName,
                           juce::dontSendNotification);
}

void ChannelRackComponent::layoutInspector()
{
    const int baseY = HEADER_HEIGHT + (int)channels.size() * ROW_HEIGHT + 50;
    const int iy    = baseY + 4;

    inspectorLabel.setBounds(10, iy - 4, 240, 16);

    // Four sliders arranged in two rows, labels provided by drawInspector
    const int slW  = (getWidth() - 30) / 2;
    const int slH  = 18;
    const int col2 = 15 + slW + 8;

    inspVelSlider    .setBounds(15,   iy + 22, slW, slH);
    inspGateSlider   .setBounds(col2, iy + 22, slW, slH);
    inspProbSlider   .setBounds(15,   iy + 48, slW, slH);
    inspPitchSlider  .setBounds(col2, iy + 48, slW, slH);
    inspCutoffSlider .setBounds(15,   iy + 74, slW, slH);
    inspStartOffSlider.setBounds(col2, iy + 74, slW, slH);
    inspResetBtn     .setBounds(getWidth() - 70, iy, 60, 20);
}

void ChannelRackComponent::drawInspector(juce::Graphics& g)
{
    if (inspectorCh < 0) return;

    const int baseY = HEADER_HEIGHT + (int)channels.size() * ROW_HEIGHT + 50;

    // Background
    g.setColour(juce::Colour(0xff1a1a22));
    g.fillRect(0, baseY, getWidth(), INSPECTOR_HEIGHT);
    g.setColour(juce::Colour(0xff3a9ad9).withAlpha(0.4f));
    g.fillRect(0, baseY, 3, INSPECTOR_HEIGHT);   // accent bar on left

    g.setColour(juce::Colour(0xff2a2a38));
    g.drawLine(0.0f, (float)baseY, (float)getWidth(), (float)baseY, 1.0f);

    // Small parameter labels above sliders
    const int iy  = baseY + 4;
    const int slW = (getWidth() - 30) / 2;
    const int col2 = 15 + slW + 8;

    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
    g.setColour(juce::Colour(0xff888892));
    g.drawText("VEL",      15,   iy + 12, 50, 10, juce::Justification::centredLeft);
    g.drawText("GATE",     col2, iy + 12, 50, 10, juce::Justification::centredLeft);
    g.drawText("PROB",     15,   iy + 38, 50, 10, juce::Justification::centredLeft);
    g.drawText("PITCH",    col2, iy + 38, 50, 10, juce::Justification::centredLeft);
    g.drawText("CUTOFF",   15,   iy + 64, 50, 10, juce::Justification::centredLeft);
    g.drawText("START",    col2, iy + 64, 50, 10, juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------

// M2.1 — Load pattern steps into the UI grid
void ChannelRackComponent::loadPattern(const Pattern& pat, int varIdx)
{
    const int vi = (varIdx >= 0) ? varIdx : activeVariation;

    // Sync step count
    stepCount = juce::jlimit(1, Pattern::kMaxSteps, pat.stepCount);
    stepCountSlider.setValue(stepCount, juce::dontSendNotification);

    // Sync step params store (always, so badges and inspector reflect the pattern)
    for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        for (int s = 0; s < Pattern::kMaxSteps; ++s)
            stepParamsStore[ch][s] = pat.variations[vi].stepParams[ch][s];
    if (inspectorCh >= 0 && inspectorStep >= 0) refreshInspector();

    // Copy step data, sample name, and per-pattern vol/pan/pitch for each channel
    for (int ch = 0; ch < (int)channels.size() && ch < Pattern::kMaxChannels; ++ch)
    {
        for (int s = 0; s < Pattern::kMaxSteps; ++s)
            channels[ch].steps[s] = pat.variations[vi].steps[ch][s];

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

        // Restore channel name and type (now per-pattern)
        channels[ch].name = pat.channelNames[ch];
        setChannelType(ch, pat.channelTypes[ch]);

        // Mixer routing
        channelRouting[ch] = pat.channelMixerRouting[ch];
    }

    repaint();
}

// M2.1 — Write UI grid state back into a Pattern
void ChannelRackComponent::saveToPattern(Pattern& pat, int varIdx) const
{
    const int vi = (varIdx >= 0) ? varIdx : activeVariation;
    pat.stepCount = stepCount;
    for (int ch = 0; ch < (int)channels.size() && ch < Pattern::kMaxChannels; ++ch)
    {
        for (int s = 0; s < Pattern::kMaxSteps; ++s)
        {
            pat.variations[vi].steps     [ch][s] = channels[ch].steps[s];
            pat.variations[vi].stepParams[ch][s] = stepParamsStore[ch][s];
        }

        pat.channelVolume[ch] = channels[ch].volume;
        pat.channelPan[ch]    = channels[ch].pan;
        pat.channelPitch[ch]  = channels[ch].pitch;
        pat.channelNames[ch]  = channels[ch].name;
        pat.channelTypes[ch]  = (ch < (int)channelTypes.size())
                                ? channelTypes[(size_t)ch] : ChannelType::Drum;
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
    g.fillAll(juce::Colour(0xff0f0f0f));
    drawHeader(g);
    drawChannelLabels(g);
    drawStepGrid(g);
    drawInspector(g);
}

void ChannelRackComponent::drawHeader(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff1c1c1e));
    g.fillRect(0, 0, getWidth(), HEADER_HEIGHT);

    const float stepW = (getWidth() - LABEL_WIDTH) / (float)stepCount;

    for (int i = 0; i < stepCount; ++i)
    {
        const int x = LABEL_WIDTH + (int)(i * stepW);

        if (i == currentPlayStep)
        {
            g.setColour(juce::Colour(0xffb0b0b8).withAlpha(0.25f));
            g.fillRect(x, 0, (int)stepW, HEADER_HEIGHT);
        }

        g.setColour(i % 4 == 0 ? juce::Colour(0xfff0f0f2) : juce::Colour(0xff888892));
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
            g.setColour(juce::Colour(0xffb0b0b8).withAlpha(0.15f));
        else
            g.setColour(i % 2 == 0 ? juce::Colour(0xff2c2c2e) : juce::Colour(0xff161618));
        g.fillRect(0, y, LABEL_WIDTH, ROW_HEIGHT);

        // Channel name (top half, left area)
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        g.drawText(channels[i].name, 8, y + 2, 68, 18,
                   juce::Justification::centredLeft);

        // Mixer routing badge
        g.setColour(juce::Colour(0xff888892));
        g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
        g.drawText(juce::String::fromUTF8("\xe2\x86\x92") + "T" + juce::String(channelRouting[i] + 1),
                   64, y + 2, 30, 18, juce::Justification::centredLeft);

        // Sample name / type badge (bottom half, left area)
        const bool isMelodic  = (i < (int)channelTypes.size() &&
                                  channelTypes[(size_t)i] == ChannelType::Melodic);
        const bool hasPlugin  = channelHasPlugin[i];

        if (hasPlugin)
        {
            // M8 — VST/AU plugin badge (replaces sample name)
            g.setColour(juce::Colour(0xff6868c8).withAlpha(0.18f));
            g.fillRoundedRectangle(6.0f, (float)(y + 23), 30.0f, 14.0f, 3.0f);
            g.setColour(juce::Colour(0xff9898f8));
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            g.drawText("VST", 6, y + 23, 30, 14, juce::Justification::centred);
        }
        else if (isMelodic)
        {
            g.setColour(juce::Colour(0xffb0b0b8).withAlpha(0.12f));
            g.fillRoundedRectangle(6.0f, (float)(y + 23), 38.0f, 14.0f, 3.0f);
            g.setColour(juce::Colour(0xffb0b0b8));
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            g.drawText("MELODIC", 6, y + 23, 38, 14, juce::Justification::centred);
        }
        else
        {
            g.setColour(juce::Colour(0xffc8c8d0));
            g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
            g.drawText(channels[(size_t)i].sampleName, 8, y + 24, 86, 16,
                       juce::Justification::centredLeft);
        }

        // Small labels left of sliders
        g.setFont(juce::Font(juce::FontOptions().withHeight(8.0f)));
        g.setColour(juce::Colour(0xff888892));
        g.drawText("VOL", 76, y + 4,  20, 10, juce::Justification::centredRight);
        g.drawText("PAN", 76, y + 22, 20, 10, juce::Justification::centredRight);
        g.setColour(juce::Colour(0xffa0a0b0));
        g.drawText("PCH", 76, y + 40, 20, 10, juce::Justification::centredRight);

        // Separator line
        g.setColour(juce::Colour(0xff282828));
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

            const bool isSelected = (ch == inspectorCh && s == inspectorStep);
            const bool hasCustom  = !stepParamsStore[ch][s].isDefault();

            if (channels[ch].steps[s])
            {
                juce::Colour c = isCurrentStep
                    ? juce::Colour(0xff8fffb0)
                    : (s % 4 == 0 ? juce::Colour(0xffe8e8f0) : juce::Colour(0xff9898a0));
                g.setColour(c);
                g.fillRoundedRectangle(x + 1, y + 2, w, h, 4.0f);

                // Subtle inner highlight on top
                g.setColour(juce::Colours::white.withAlpha(0.12f));
                g.drawLine((float)(x + 3), (float)(y + 3),
                           (float)(x + w - 1), (float)(y + 3), 1.0f);
            }
            else
            {
                juce::Colour c = isCurrentStep ? juce::Colour(0xff1e1e28)
                                               : juce::Colour(0xff111118);
                g.setColour(c);
                g.fillRoundedRectangle(x + 1, y + 2, w, h, 4.0f);

                if (!isCurrentStep)
                {
                    g.setColour(juce::Colour(0xff2a2a30));
                    g.drawRoundedRectangle(x + 1, y + 2, w, h, 4.0f, 1.0f);
                }
            }

            // Custom step params badge — small filled circle in bottom-right corner
            if (hasCustom)
            {
                g.setColour(juce::Colour(0xff3a9ad9).withAlpha(0.85f));
                g.fillEllipse((float)(x + w - 5), (float)(y + h - 3), 4.0f, 4.0f);
            }

            // Selected step highlight border
            if (isSelected)
            {
                g.setColour(juce::Colour(0xff3a9ad9).withAlpha(0.9f));
                g.drawRoundedRectangle((float)(x + 1), (float)(y + 2), (float)w, (float)h, 4.0f, 1.5f);
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
        const bool hasPlug = channelHasPlugin[ch];

        juce::PopupMenu menu;
        menu.addItem(1, "Rename");
        menu.addSeparator();
        menu.addItem(2, isMelodic ? "Switch to Drum" : "Switch to Melodic");
        if (isMelodic)
            menu.addItem(3, "Open Piano Roll");
        menu.addSeparator();
        menu.addItem(4, "Open Synth Editor");   // M13
        menu.addSeparator();
        menu.addItem(5, "Load VST/AU Plugin...");   // M8
        if (hasPlug)
        {
            menu.addItem(6, "Open Plugin Editor");
            menu.addItem(7, "Remove Plugin");
        }
        menu.addSeparator();
        {
            juce::PopupMenu routeMenu;
            const int curRoute = channelRouting[ch];
            for (int t = 0; t < 8; ++t)
                routeMenu.addItem(100 + t, "Track " + juce::String(t + 1),
                                  true, t == curRoute);
            menu.addSubMenu("Route to Mixer Track", routeMenu);
        }
        menu.addSeparator();
        menu.addItem(8, "Delete Channel", (int)channels.size() > 1);

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
                    if (onOpenSynthEditor) onOpenSynthEditor(ch);
                }
                else if (result == 5)
                {
                    if (onLoadPlugin) onLoadPlugin(ch);       // M8
                }
                else if (result == 6)
                {
                    if (onOpenPluginEditor) onOpenPluginEditor(ch);
                }
                else if (result == 7)
                {
                    if (onRemovePlugin) onRemovePlugin(ch);
                }
                else if (result == 8)
                {
                    if (onDeleteChannel) onDeleteChannel(ch);
                }
                else if (result >= 100 && result < 108)
                {
                    const int newTrack = result - 100;
                    channelRouting[ch] = newTrack;
                    if (onChannelRoutingChanged) onChannelRoutingChanged(ch, newTrack);
                    repaint();
                }
            });
        return;
    }

    // Right-click on step grid → open step inspector
    if (e.mods.isRightButtonDown() && x >= stepAreaX)
    {
        const int s = juce::jlimit(0, stepCount - 1, (int)((x - stepAreaX) / stepW));
        if (ch >= 0 && ch < (int)channels.size() && s >= 0 && s < stepCount)
        {
            if (inspectorCh == ch && inspectorStep == s)
                closeInspector();   // right-click same step → toggle off
            else
                openInspector(ch, s);
        }
        return;
    }

    // Left-click on step grid (right-click is reserved for context menus)
    if (x < stepAreaX || e.mods.isRightButtonDown()) return;

    const int s = (int)((x - stepAreaX) / stepW);

    // M3: only Drum channels use the step grid; Melodic channels use the piano roll
    if (ch >= 0 && ch < (int)channels.size() && s >= 0 && s < stepCount
        && channelTypes[(size_t)ch] == ChannelType::Drum)
    {
        // Begin a new undo transaction for this click/drag gesture
        if (onStepDragBegin) onStepDragBegin();

        const bool oldState = channels[(size_t)ch].steps[(size_t)s];
        const bool newState = !oldState;
        channels[(size_t)ch].steps[(size_t)s] = newState;
        if (onStepToggled) onStepToggled(ch, s, newState, oldState);
        repaint();

        // Arm drag-paint so mouseDrag continues the gesture on adjacent steps
        dragPaintChannel_ = ch;
        dragPaintState_   = newState;
        lastDragStep_     = s;
    }
    // Melodic channel: double-click opens piano roll (handled in mouseDoubleClick)
}

void ChannelRackComponent::mouseDrag(const juce::MouseEvent& e)
{
    // Only active when a step-grid paint gesture was started in mouseDown
    if (dragPaintChannel_ < 0 || e.mods.isRightButtonDown()) return;

    const int stepAreaX = LABEL_WIDTH;
    const float stepW   = (getWidth() - stepAreaX) / (float)stepCount;
    const int x         = e.getPosition().getX();

    if (x < stepAreaX) return;

    // Clamp so dragging past the grid edges doesn't go out of bounds
    const int s = juce::jlimit(0, stepCount - 1, (int)((x - stepAreaX) / stepW));

    if (s == lastDragStep_) return;   // already painted this step in this gesture
    lastDragStep_ = s;

    // Only apply if this step's current state differs from the paint state —
    // dragging back over an already-painted step leaves it unchanged.
    const bool currentState = channels[(size_t)dragPaintChannel_].steps[(size_t)s];
    if (currentState != dragPaintState_)
    {
        const bool oldState = currentState;
        channels[(size_t)dragPaintChannel_].steps[(size_t)s] = dragPaintState_;
        if (onStepToggled) onStepToggled(dragPaintChannel_, s, dragPaintState_, oldState);
        repaint();
    }
}

void ChannelRackComponent::mouseUp(const juce::MouseEvent&)
{
    if (dragPaintChannel_ < 0) return;

    // Close the undo transaction so the next action starts fresh
    if (onStepDragEnd) onStepDragEnd();

    dragPaintChannel_ = -1;
    lastDragStep_     = -1;
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
    addChannelBtn  .setBounds(10,  controlsY, 140, 30);
    clearStepsBtn  .setBounds(160, controlsY, 110, 30);
    stepCountSlider.setBounds(280, controlsY, 100, 30);

    // Variation buttons A/B/C/D — right of step count slider
    const int varBtnW = 24, varBtnH = 24;
    const int varBtnY = controlsY + (30 - varBtnH) / 2;
    const int varBtnX = 388;
    varBtnA.setBounds(varBtnX + 0 * (varBtnW + 2), varBtnY, varBtnW, varBtnH);
    varBtnB.setBounds(varBtnX + 1 * (varBtnW + 2), varBtnY, varBtnW, varBtnH);
    varBtnC.setBounds(varBtnX + 2 * (varBtnW + 2), varBtnY, varBtnW, varBtnH);
    varBtnD.setBounds(varBtnX + 3 * (varBtnW + 2), varBtnY, varBtnW, varBtnH);

    // Inspector position update (only if open)
    if (inspectorCh >= 0) layoutInspector();

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
