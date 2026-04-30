#include "ChannelRackComponent.h"
#include "StudioLookAndFeel.h"

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
    addChannelBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
    addChannelBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kText));
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
    clearStepsBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kAccent).withAlpha(0.18f));
    clearStepsBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kAccent));
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
    stepCountSlider.setColour(juce::Slider::textBoxTextColourId,       juce::Colour(StudioLookAndFeel::kText));
    stepCountSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(StudioLookAndFeel::kChassis2));
    stepCountSlider.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(StudioLookAndFeel::kPanelRim));
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
        btn.setColour(juce::TextButton::buttonColourId,   juce::Colour(StudioLookAndFeel::kChassis));
        btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(StudioLookAndFeel::kAccent));
        btn.setColour(juce::TextButton::textColourOffId,  juce::Colour(StudioLookAndFeel::kTextDim));
        btn.setColour(juce::TextButton::textColourOnId,   juce::Colour(0xffffffff));
        btn.onClick = [this, idx]
        {
            const int prevVariation = activeVariation;
            activeVariation = idx;
            updateVariationButtonStates();
            if (onVariationChanged) onVariationChanged(prevVariation, idx);
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
    setupInspSlider(inspTimingSlider, -0.5,  0.5,  0.01, juce::Colour(0xff9977cc), "");

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
    inspTimingSlider.onValueChange = [this]
    {
        if (inspectorCh < 0 || inspectorStep < 0) return;
        stepParamsStore[inspectorCh][inspectorStep].timingOffset = (float)inspTimingSlider.getValue();
        if (onStepParamsChanged)
            onStepParamsChanged(inspectorCh, inspectorStep, stepParamsStore[inspectorCh][inspectorStep]);
        repaint();
    };

    // --- Swing slider setup ---
    addAndMakeVisible(swingSlider);
    swingSlider.setRange(0.0, 0.5, 0.01);
    swingSlider.setValue(0.0, juce::dontSendNotification);
    swingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    swingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 20);
    swingSlider.setNumDecimalPlacesToDisplay(2);
    swingSlider.setColour(juce::Slider::thumbColourId,             juce::Colour(0xff9977cc));
    swingSlider.setColour(juce::Slider::trackColourId,             juce::Colour(0xff9977cc).withAlpha(0.6f));
    swingSlider.setColour(juce::Slider::backgroundColourId,        juce::Colour(0xff1c1c1e));
    swingSlider.setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
    swingSlider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff141418));
    swingSlider.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff2a2a35));
    swingSlider.onValueChange = [this]
    {
        if (onSwingChanged) onSwingChanged((float)swingSlider.getValue());
    };

    addAndMakeVisible(swingLabel);
    swingLabel.setText("Swing", juce::dontSendNotification);
    swingLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    swingLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0b0b8));

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
    const juce::Colour activeCol   (StudioLookAndFeel::kAccent);
    const juce::Colour activeTxt   (juce::Colours::white);
    const juce::Colour inactiveCol (StudioLookAndFeel::kChassis);
    const juce::Colour inactiveTxt (StudioLookAndFeel::kTextDim);

    auto style = [&](juce::TextButton& btn, bool on)
    {
        btn.setColour(juce::TextButton::buttonColourId,  on ? activeCol   : inactiveCol);
        btn.setColour(juce::TextButton::textColourOffId, on ? activeTxt   : inactiveTxt);
    };
    style(varBtnA, activeVariation == 0);
    style(varBtnB, activeVariation == 1);
    style(varBtnC, activeVariation == 2);
    style(varBtnD, activeVariation == 3);
    repaint();
}

// ---------------------------------------------------------------------------

void ChannelRackComponent::addChannel(const juce::String& name)
{
    using LF = StudioLookAndFeel;

    // Phase-4 palette (matches RACK_COLORS from sequencer.jsx)
    static const juce::uint32 kPalette[] = {
        0xffd8412a, 0xffe89c2b, 0xfff0c14a, 0xff7ab87a,
        0xff3da356, 0xff5fa8d8, 0xff2cd4d4, 0xff7a8fe0,
        0xffb87ad6, 0xffe07ac8, 0xffa8a098, 0xffd8c8a8,
        0xffe89c2b, 0xff7ab87a, 0xff5fa8d8, 0xffb87ad6
    };

    ChannelRow row;
    row.name  = name;
    row.color = juce::Colour(kPalette[(int)channels.size() % 16]);

    const int ch = (int)channels.size();
    const juce::Colour col = row.color;

    // ---- Mute button
    row.muteBtn = std::make_unique<juce::TextButton>("M");
    row.muteBtn->setColour(juce::TextButton::buttonColourId,  juce::Colour(LF::kChassis2));
    row.muteBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(LF::kTextDim));
    row.muteBtn->onClick = [this, ch]
    {
        channels[ch].muted = !channels[ch].muted;
        channels[ch].muteBtn->setColour(juce::TextButton::buttonColourId,
            channels[ch].muted ? juce::Colour(LF::kLedAmber) : juce::Colour(LF::kChassis2));
        channels[ch].muteBtn->setColour(juce::TextButton::textColourOffId,
            channels[ch].muted ? juce::Colour(0xff000000) : juce::Colour(LF::kTextDim));
        if (onMuteChanged) onMuteChanged(ch, channels[ch].muted);
    };
    addAndMakeVisible(*row.muteBtn);

    // ---- Solo button
    row.soloBtn = std::make_unique<juce::TextButton>("S");
    row.soloBtn->setColour(juce::TextButton::buttonColourId,  juce::Colour(LF::kChassis2));
    row.soloBtn->setColour(juce::TextButton::textColourOffId, juce::Colour(LF::kTextDim));
    row.soloBtn->onClick = [this, ch]
    {
        channels[ch].soloed = !channels[ch].soloed;
        channels[ch].soloBtn->setColour(juce::TextButton::buttonColourId,
            channels[ch].soloed ? juce::Colour(LF::kLedGreen) : juce::Colour(LF::kChassis2));
        channels[ch].soloBtn->setColour(juce::TextButton::textColourOffId,
            channels[ch].soloed ? juce::Colour(0xff000000) : juce::Colour(LF::kTextDim));
        if (onSoloChanged) onSoloChanged(ch, channels[ch].soloed);
    };
    addAndMakeVisible(*row.soloBtn);

    // ---- Volume slider (mini bar, no text box)
    row.volSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                    juce::Slider::NoTextBox);
    row.volSlider->setRange(0.0, 1.0, 0.01);
    row.volSlider->setValue(row.volume, juce::dontSendNotification);
    row.volSlider->setColour(juce::Slider::thumbColourId,      col.brighter(0.5f));
    row.volSlider->setColour(juce::Slider::trackColourId,      col);
    row.volSlider->setColour(juce::Slider::backgroundColourId, juce::Colour(LF::kDisplayBg));
    row.volSlider->onValueChange = [this, ch]
    {
        channels[ch].volume = (float)channels[ch].volSlider->getValue();
        if (onVolumeChanged) onVolumeChanged(ch, channels[ch].volume);
        repaint();
    };
    addAndMakeVisible(*row.volSlider);

    // ---- Pan slider (mini bar bipolar, no text box)
    row.panSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                    juce::Slider::NoTextBox);
    row.panSlider->setRange(-1.0, 1.0, 0.01);
    row.panSlider->setValue(row.pan, juce::dontSendNotification);
    row.panSlider->setColour(juce::Slider::thumbColourId,      col.brighter(0.5f));
    row.panSlider->setColour(juce::Slider::trackColourId,      col);
    row.panSlider->setColour(juce::Slider::backgroundColourId, juce::Colour(LF::kDisplayBg));
    row.panSlider->onValueChange = [this, ch]
    {
        channels[ch].pan = (float)channels[ch].panSlider->getValue();
        if (onPanChanged) onPanChanged(ch, channels[ch].pan);
        repaint();
    };
    addAndMakeVisible(*row.panSlider);

    // ---- Pitch slider (mini bar bipolar, no text box)
    row.pitchSlider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                      juce::Slider::NoTextBox);
    row.pitchSlider->setRange(-24.0, 24.0, 0.5);
    row.pitchSlider->setValue(row.pitch, juce::dontSendNotification);
    row.pitchSlider->setColour(juce::Slider::thumbColourId,      col.brighter(0.5f));
    row.pitchSlider->setColour(juce::Slider::trackColourId,      col);
    row.pitchSlider->setColour(juce::Slider::backgroundColourId, juce::Colour(LF::kDisplayBg));
    row.pitchSlider->onValueChange = [this, ch]
    {
        channels[ch].pitch = (float)channels[ch].pitchSlider->getValue();
        if (onPitchChanged) onPitchChanged(ch, channels[ch].pitch);
        repaint();
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
    if (onInspectorOpened)
    {
        const juce::String chName = (ch < (int)channels.size()) ? channels[(size_t)ch].name : "Ch";
        onInspectorOpened(ch, step, chName, stepParamsStore[ch][step]);
    }
    repaint();
}

void ChannelRackComponent::closeInspector()
{
    inspectorCh = inspectorStep = -1;
    if (onInspectorClosed) onInspectorClosed();
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
    inspTimingSlider .setValue(p.timingOffset,        juce::dontSendNotification);

    const juce::String chName = (inspectorCh < (int)channels.size())
                                ? channels[(size_t)inspectorCh].name : "Ch";
    inspectorLabel.setText("Step " + juce::String(inspectorStep + 1) + juce::String::fromUTF8("  \xe2\x80\x94  ") + chName,
                           juce::dontSendNotification);
}

void ChannelRackComponent::layoutInspector()
{
    // Bottom inspector removed — StepInspectorStrip in MainComponent handles this now
}

void ChannelRackComponent::drawInspector(juce::Graphics&)
{
    // Bottom inspector removed — StepInspectorStrip in MainComponent handles this now
}

// ---------------------------------------------------------------------------

// M2.1 — Load pattern steps into the UI grid
void ChannelRackComponent::loadPattern(const Pattern& pat, int varIdx)
{
    const int vi = (varIdx >= 0) ? varIdx : activeVariation;

    // Sync step count
    stepCount = juce::jlimit(1, Pattern::kMaxSteps, pat.stepCount);
    stepCountSlider.setValue(stepCount, juce::dontSendNotification);
    patternStartStep = juce::jlimit(0, stepCount - 1, patternStartStep);

    // Sync swing
    swingSlider.setValue((double)pat.swingAmount, juce::dontSendNotification);

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
    pat.stepCount    = stepCount;
    pat.channelCount = (int)channels.size();
    pat.swingAmount  = (float)swingSlider.getValue();
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
    // Cream chassis background
    g.fillAll(juce::Colour(StudioLookAndFeel::kChassis2));
    drawHeader(g);
    drawChannelLabels(g);
    drawStepGrid(g);
    drawInspector(g);
}

void ChannelRackComponent::drawHeader(juce::Graphics& g)
{
    using LF = StudioLookAndFeel;
    const int W = getWidth();

    // === Title strip (y=0..30) ===
    {
        juce::ColourGradient gr(juce::Colour(LF::kChassis),  0.0f, 0.0f,
                                juce::Colour(LF::kChassis2), 0.0f, 30.0f, false);
        g.setGradientFill(gr);
        g.fillRect(0, 0, W, 30);
    }

    // Accent square
    g.setColour(juce::Colour(LF::kAccent));
    g.fillRoundedRectangle(10.0f, 11.0f, 7.0f, 7.0f, 2.0f);

    // "CHANNEL RACK" title
    g.setFont(LF::monoFont(9.5f, juce::Font::bold));
    g.setColour(juce::Colour(LF::kText));
    g.drawText("CHANNEL RACK", 22, 0, 130, 30, juce::Justification::centredLeft);

    // Tags
    auto drawTag = [&](int x, int cy, const juce::String& t) -> int
    {
        g.setFont(LF::monoFont(7.5f, juce::Font::bold));
        const int tw = g.getCurrentFont().getStringWidth(t) + 10;
        g.setColour(juce::Colour(0x18000000));
        g.fillRoundedRectangle((float)x, (float)(cy - 7), (float)tw, 14.0f, 2.0f);
        g.setColour(juce::Colour(LF::kTextFaint));
        g.drawText(t, x + 4, cy - 8, tw - 4, 16, juce::Justification::centredLeft);
        return tw + 4;
    };
    int tx = 158;
    tx += drawTag(tx, 15, juce::String((int)channels.size()) + "/16 CH");
    tx += drawTag(tx, 15, juce::String(stepCount) + juce::String::fromUTF8("\xc2\xb7") + "64 STEP");

    // "VARIATION" label (painted; buttons positioned by resized())
    g.setFont(LF::monoFont(7.0f, juce::Font::bold));
    g.setColour(juce::Colour(LF::kTextFaint));
    const int varLabelX = W - 4 * 26 - 8 - 60;
    g.drawText("VARIATION", varLabelX, 0, 56, 30, juce::Justification::centredRight);

    // Title strip separator
    g.setColour(juce::Colour(LF::kPanelRim));
    g.drawLine(0.0f, 30.0f, (float)W, 30.0f, 0.8f);

    // === Ruler strip (y=30..50) ===
    g.setColour(juce::Colour(LF::kPanel));
    g.fillRect(0, 30, W, 20);

    // Left side: header label
    g.setFont(LF::monoFont(7.0f, juce::Font::bold));
    g.setColour(juce::Colour(LF::kTextFaint));
    g.drawText("CH " + juce::String::fromUTF8("\xc2\xb7") + " NAME " + juce::String::fromUTF8("\xc2\xb7") + " BUS",
               8, 30, LABEL_WIDTH - 8, 20, juce::Justification::centredLeft);

    // Right side: step numbers
    const int stepAreaX = LABEL_WIDTH;
    const float stepW   = (W - stepAreaX) / (float)stepCount;

    // Pattern-start highlight
    {
        const int startX = stepAreaX + (int)(patternStartStep * stepW);
        g.setColour(juce::Colour(LF::kAccent).withAlpha(0.10f));
        g.fillRect(startX, 30, (int)stepW, 20);
        g.setColour(juce::Colour(LF::kAccent).withAlpha(0.55f));
        g.drawLine((float)startX, 30.0f, (float)startX, 50.0f, 1.0f);
    }

    for (int i = 0; i < stepCount; ++i)
    {
        const int x         = stepAreaX + (int)(i * stepW);
        const bool isCurrent = (i == currentPlayStep);
        const bool isQuarter = (i % 4 == 0);

        if (isCurrent)
        {
            g.setColour(juce::Colour(LF::kAccent).withAlpha(0.15f));
            g.fillRect(x, 30, (int)stepW, 20);
        }

        g.setFont(LF::monoFont(7.0f, isQuarter ? juce::Font::bold : juce::Font::plain));
        g.setColour(isCurrent ? juce::Colour(LF::kAccent)
                              : (isQuarter ? juce::Colour(LF::kText) : juce::Colour(LF::kTextFaint)));
        g.drawText(juce::String(i + 1).paddedLeft('0', 2),
                   x, 30, (int)stepW, 20, juce::Justification::centred);
    }

    // Ruler bottom separator + left-area right edge
    g.setColour(juce::Colour(LF::kPanelRim));
    g.drawLine(0.0f, 49.0f, (float)W, 49.0f, 0.8f);
    g.drawLine((float)LABEL_WIDTH, 30.0f, (float)LABEL_WIDTH, 50.0f, 0.8f);
}

void ChannelRackComponent::drawChannelLabels(juce::Graphics& g)
{
    using LF = StudioLookAndFeel;

    for (int i = 0; i < (int)channels.size(); ++i)
    {
        const int y          = HEADER_HEIGHT + i * ROW_HEIGHT;
        const bool isSelected = (i == selectedMidiChannel);
        const bool hasMidi    = (midiActivityMask_ >> i) & 1;
        const juce::Colour col = channels[i].color;

        // Row background
        {
            const juce::Colour bg = i % 2 == 0
                ? juce::Colour(LF::kPanel) : juce::Colour(LF::kChassis);
            g.setColour(isSelected ? juce::Colour(LF::kAccent).withAlpha(0.07f) : bg);
            g.fillRect(0, y, LABEL_WIDTH, ROW_HEIGHT);
        }

        // Selected: 2px accent left border
        if (isSelected)
        {
            g.setColour(juce::Colour(LF::kAccent));
            g.fillRect(0, y, 2, ROW_HEIGHT);
        }

        // Drag-hover tint
        if (i == dragHoverChannel && !isSelected)
        {
            g.setColour(juce::Colour(LF::kAccent).withAlpha(0.05f));
            g.fillRect(0, y, LABEL_WIDTH, ROW_HEIGHT);
        }

        // ---- Idx colour box (18×18, x=6, y+6) ----
        {
            g.setColour(col);
            g.fillRoundedRectangle(6.0f, (float)(y + 6), 18.0f, 18.0f, 3.0f);
            g.setColour(col.withAlpha(0.35f));
            g.drawRoundedRectangle(5.0f, (float)(y + 5), 20.0f, 20.0f, 4.0f, 0.8f);
            g.setFont(LF::monoFont(7.0f, juce::Font::bold));
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawText(juce::String(i + 1), 6, y + 6, 18, 18, juce::Justification::centred);
        }

        // ---- Row 1: name + type badge ----
        {
            const bool isMel  = (i < (int)channelTypes.size() && channelTypes[i] == ChannelType::Melodic);
            const bool hasPlug = channelHasPlugin[i];

            // Channel name (uppercased, 10px bold)
            g.setFont(LF::monoFont(9.5f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kText));
            g.drawText(channels[i].name.toUpperCase(), 28, y + 5, 90, 14,
                       juce::Justification::centredLeft, true);

            // DRUM/MEL/VST badge
            const juce::String badge = hasPlug ? "VST" : (isMel ? "MEL" : "DRM");
            const juce::Colour badgeBg   = (hasPlug || isMel)
                ? juce::Colour(LF::kAccent).withAlpha(0.12f)
                : juce::Colour(0x0f000000);
            const juce::Colour badgeCol  = (hasPlug || isMel)
                ? juce::Colour(LF::kAccent) : juce::Colour(LF::kTextFaint);
            const juce::Colour badgeBord = (hasPlug || isMel)
                ? juce::Colour(LF::kAccent).withAlpha(0.35f) : juce::Colour(0x22000000);

            g.setColour(badgeBg);
            g.fillRoundedRectangle(121.0f, (float)(y + 6), 30.0f, 12.0f, 2.0f);
            g.setColour(badgeBord);
            g.drawRoundedRectangle(121.0f, (float)(y + 6), 30.0f, 12.0f, 2.0f, 0.7f);
            g.setFont(LF::monoFont(7.0f, juce::Font::bold));
            g.setColour(badgeCol);
            g.drawText(badge, 121, y + 6, 30, 12, juce::Justification::centred);

            // MIDI activity dot
            if (hasMidi)
            {
                g.setColour(juce::Colour(LF::kLedGreen));
                g.fillEllipse(155.0f, (float)(y + 9), 6.0f, 6.0f);
                g.setColour(juce::Colour(LF::kLedGreen).withAlpha(0.3f));
                g.drawEllipse(154.0f, (float)(y + 8), 8.0f, 8.0f, 0.8f);
            }
        }

        // ---- Row 2: V / P / ♯ labels (y+24) ----
        {
            g.setFont(LF::monoFont(7.0f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kTextFaint));
            g.drawText("V",  28, y + 24, 8, 8, juce::Justification::centred);
            g.drawText("P",  74, y + 24, 8, 8, juce::Justification::centred);
            g.drawText(juce::String::fromUTF8("\xe2\x99\xaf"), 118, y + 24, 8, 8, juce::Justification::centred);
        }

        // ---- Row 3: M/S drawn visuals + routing + step count + actions ----
        // (M/S are actual JUCE buttons, but we paint routing/step-count labels)
        {
            g.setFont(LF::monoFont(7.0f));
            g.setColour(juce::Colour(LF::kTextFaint));
            g.drawText(juce::String::fromUTF8("\xe2\x86\x92") + " MX" + juce::String(channelRouting[i] + 1),
                       62, y + 38, 40, 11, juce::Justification::centredLeft);
            g.drawText(juce::String(stepCount) + "st",
                       104, y + 38, 22, 11, juce::Justification::centredLeft);
            // Decorative action labels
            g.setColour(juce::Colour(LF::kTextDim));
            g.setFont(LF::displayFont(12.0f));
            g.drawText(juce::String::fromUTF8("\xe2\x99\xaa"), 130, y + 35, 16, 16, juce::Justification::centred);  // ♪
            g.drawText(juce::String::fromUTF8("\xe2\x8b\xaf"), 148, y + 35, 16, 16, juce::Justification::centred);  // ⋯
        }

        // Row separator + right edge
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine(0.0f, (float)(y + ROW_HEIGHT - 1), (float)LABEL_WIDTH,
                   (float)(y + ROW_HEIGHT - 1), 0.6f);
        g.drawLine((float)(LABEL_WIDTH - 0.5f), (float)y,
                   (float)(LABEL_WIDTH - 0.5f), (float)(y + ROW_HEIGHT), 0.8f);
    }
}

void ChannelRackComponent::drawStepGrid(juce::Graphics& g)
{
    using LF = StudioLookAndFeel;
    const int   stepAreaX = LABEL_WIDTH;
    const float stepW     = (getWidth() - stepAreaX) / (float)stepCount;

    static constexpr int kCellH = 24;

    for (int ch = 0; ch < (int)channels.size(); ++ch)
    {
        const int y      = HEADER_HEIGHT + ch * ROW_HEIGHT;
        const int cellY  = y + (ROW_HEIGHT - kCellH) / 2;
        const juce::Colour col = channels[ch].color;

        // Row background (alternating cream)
        g.setColour(ch % 2 == 0 ? juce::Colour(LF::kChassis) : juce::Colour(LF::kChassis2));
        g.fillRect(stepAreaX, y, getWidth() - stepAreaX, ROW_HEIGHT);

        // Row separator
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine((float)stepAreaX, (float)(y + ROW_HEIGHT - 1),
                   (float)getWidth(), (float)(y + ROW_HEIGHT - 1), 0.6f);

        // 4-step group shading bands (alternating subtle tint behind cells)
        for (int grp = 0; grp < (stepCount + 3) / 4; ++grp)
        {
            if (grp % 2 == 0) continue;
            const int gx = stepAreaX + (int)(grp * 4 * stepW);
            const int gw = (int)(4 * stepW);
            g.setColour(juce::Colour(0x0b000000));
            g.fillRect(gx, y, gw, ROW_HEIGHT);
        }

        for (int s = 0; s < stepCount; ++s)
        {
            const int cellX      = stepAreaX + (int)(s * stepW) + 1;
            const int cellW      = (int)stepW - 3;
            const bool isStep    = channels[ch].steps[s];
            const bool isCurrent = (s == currentPlayStep);
            const bool isSelected = (ch == inspectorCh && s == inspectorStep);
            const bool hasCustom  = !stepParamsStore[ch][s].isDefault();
            const bool isHalfBar  = (s % 8 == 0);

            // 4-step group separator line at every group boundary
            if (s % 4 == 0 && s > 0)
            {
                g.setColour(juce::Colour(LF::kPanelRim).withAlpha(0.55f));
                g.drawLine((float)(cellX - 1), (float)(y + 3),
                           (float)(cellX - 1), (float)(y + ROW_HEIGHT - 3), 0.8f);
            }

            if (isStep)
            {
                // ON step — vivid channel colour gradient (high saturation)
                juce::ColourGradient gr(col.brighter(0.28f), 0.0f, (float)cellY,
                                        col.darker(0.22f),   0.0f, (float)(cellY + kCellH), false);
                g.setGradientFill(gr);
                g.fillRoundedRectangle((float)cellX, (float)cellY, (float)cellW, (float)kCellH, 3.0f);

                // Strong specular top edge (backlit key feel)
                g.setColour(juce::Colours::white.withAlpha(0.50f));
                g.drawLine((float)(cellX + 2), (float)(cellY + 1),
                           (float)(cellX + cellW - 2), (float)(cellY + 1), 1.5f);

                // Inner bottom shadow
                g.setColour(juce::Colours::black.withAlpha(0.22f));
                g.drawLine((float)(cellX + 2), (float)(cellY + kCellH - 2),
                           (float)(cellX + cellW - 2), (float)(cellY + kCellH - 2), 1.0f);

                // Velocity bar (bottom strip, bright white)
                const float vel = juce::jlimit(0.0f, 1.0f, stepParamsStore[ch][s].velocity);
                const float velH = juce::jmax(2.0f, vel * (float)(kCellH - 5));
                g.setColour(juce::Colours::white.withAlpha(0.70f));
                g.fillRoundedRectangle((float)(cellX + 1), (float)(cellY + kCellH - 1) - velH,
                                       (float)(cellW - 2), velH, 1.0f);

                // Glow halo if on current step
                if (isCurrent)
                {
                    g.setColour(col.withAlpha(0.42f));
                    g.fillRoundedRectangle((float)(cellX - 2), (float)(cellY - 2),
                                           (float)(cellW + 4), (float)(kCellH + 4), 5.0f);
                }
            }
            else
            {
                // OFF step — dark inset pad (half-bar boundary = darker)
                const float alpha = isHalfBar ? 0.20f : 0.11f;
                g.setColour(juce::Colour(LF::kDark).withAlpha(alpha));
                g.fillRoundedRectangle((float)cellX, (float)cellY, (float)cellW, (float)kCellH, 3.0f);

                // Subtle inset top highlight
                g.setColour(juce::Colours::white.withAlpha(0.06f));
                g.drawLine((float)(cellX + 2), (float)(cellY + 1),
                           (float)(cellX + cellW - 2), (float)(cellY + 1), 0.8f);

                // Border
                g.setColour(juce::Colour(0xff000000).withAlpha(isHalfBar ? 0.30f : 0.18f));
                g.drawRoundedRectangle((float)cellX, (float)cellY, (float)cellW, (float)kCellH, 3.0f, 0.8f);
            }

            // Custom step params badge — amber dot (top-right corner)
            if (hasCustom)
            {
                g.setColour(juce::Colour(LF::kLedAmber).withAlpha(0.9f));
                g.fillEllipse((float)(cellX + cellW - 5), (float)(cellY + 1), 4.0f, 4.0f);
            }

            // Current step outline (2px accent, over everything)
            if (isCurrent)
            {
                g.setColour(juce::Colour(LF::kAccent));
                g.drawRoundedRectangle((float)(cellX - 1), (float)(cellY - 1),
                                       (float)(cellW + 2), (float)(kCellH + 2), 3.0f, 1.8f);
            }

            // Inspector selection border
            if (isSelected)
            {
                g.setColour(juce::Colour(LF::kAccent).withAlpha(0.85f));
                g.drawRoundedRectangle((float)cellX, (float)cellY, (float)cellW, (float)kCellH, 3.0f, 1.5f);
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

    if (e.mods.isLeftButtonDown() && e.getPosition().getY() >= 0 && e.getPosition().getY() < HEADER_HEIGHT && x >= stepAreaX)
    {
        const int s = juce::jlimit(0, stepCount - 1, (int)((x - stepAreaX) / stepW));
        if (patternStartStep != s)
        {
            patternStartStep = s;
            if (onPatternStartStepChanged)
                onPatternStartStepChanged(s);
            repaint();
        }
        return;
    }

    if (y < 0) return;

    const int ch = y / ROW_HEIGHT;

    // Left-click on label area → select this channel as MIDI target
    if (e.mods.isLeftButtonDown() && x < stepAreaX
        && ch >= 0 && ch < (int)channels.size())
    {
        selectedMidiChannel = ch;
        repaint();
        if (onChannelSelected) onChannelSelected(ch);
        return;
    }

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

    if (x >= LABEL_WIDTH || y < 0) return;

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

    // Variation buttons A/B/C/D — in the title strip header (y=5, h=20)
    {
        const int varBtnW = 24, varBtnH = 20, varY = 5;
        const int varX = getWidth() - 4 * (varBtnW + 2) - 8;
        varBtnA.setBounds(varX + 0 * (varBtnW + 2), varY, varBtnW, varBtnH);
        varBtnB.setBounds(varX + 1 * (varBtnW + 2), varY, varBtnW, varBtnH);
        varBtnC.setBounds(varX + 2 * (varBtnW + 2), varY, varBtnW, varBtnH);
        varBtnD.setBounds(varX + 3 * (varBtnW + 2), varY, varBtnW, varBtnH);
    }

    // Bottom controls
    const int controlsY = HEADER_HEIGHT + (int)channels.size() * ROW_HEIGHT + 8;
    addChannelBtn  .setBounds(10,  controlsY, 140, 30);
    clearStepsBtn  .setBounds(160, controlsY, 110, 30);
    stepCountSlider.setBounds(280, controlsY, 100, 30);

    // Swing slider
    const int swingX = 390;
    swingLabel.setBounds(swingX,      controlsY + 2, 40, 26);
    swingSlider.setBounds(swingX + 38, controlsY + 2, 110, 26);

    // Inspector position update (only if open)
    if (inspectorCh >= 0) layoutInspector();

    // Per-channel controls layout (180px head)
    for (int i = 0; i < (int)channels.size(); ++i)
    {
        const int y = HEADER_HEIGHT + i * ROW_HEIGHT;

        // Mini bar sliders: V(row2 left) / P(row2 mid) / ♯(row2 right)
        if (channels[i].volSlider)
            channels[i].volSlider->setBounds(36, y + 23, 36, 6);
        if (channels[i].panSlider)
            channels[i].panSlider->setBounds(82, y + 23, 36, 6);
        if (channels[i].pitchSlider)
            channels[i].pitchSlider->setBounds(128, y + 23, 38, 6);

        // Mute / Solo — row 3 (y+37), 14×12
        if (channels[i].muteBtn)
            channels[i].muteBtn->setBounds(28, y + 37, 14, 12);
        if (channels[i].soloBtn)
            channels[i].soloBtn->setBounds(44, y + 37, 14, 12);
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
