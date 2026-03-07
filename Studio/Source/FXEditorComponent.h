/*
  ==============================================================================

    FXEditorComponent.h  — M14 FX chain editor
    Floating window per mixer track: Compressor, Delay, Reverb

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <functional>
#include "ProjectModel.h"

// ---------------------------------------------------------------------------
// Content panel
// ---------------------------------------------------------------------------
class FXEditorPanel : public juce::Component
{
public:
    std::function<void()> onParamsChanged;

    FXEditorPanel()
    {
        auto makeLabel = [this](juce::Label& lbl, const juce::String& text)
        {
            lbl.setText(text, juce::dontSendNotification);
            lbl.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            lbl.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
            addAndMakeVisible(lbl);
        };

        auto makeSlider = [this](juce::Slider& s, double lo, double hi, double step, double val)
        {
            s.setRange(lo, hi, step);
            s.setValue(val, juce::dontSendNotification);
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
            s.setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
            s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff16213e));
            s.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff0f3460));
            s.setColour(juce::Slider::thumbColourId,             juce::Colour(0xff9b59b6));
            s.setColour(juce::Slider::trackColourId,             juce::Colour(0xff9b59b6));
            addAndMakeVisible(s);
        };

        auto makeToggle = [this](juce::TextButton& btn, const juce::String& text)
        {
            btn.setButtonText(text);
            btn.setClickingTogglesState(true);
            btn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2c2c54));
            btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff8e44ad));
            btn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
            btn.onClick = [this] { notify(); };
            addAndMakeVisible(btn);
        };

        // ---- Compressor
        makeToggle(compEnableBtn, "Enable");
        makeSlider(compThreshSl,  -60.0, 0.0,   1.0,  -12.0); compThreshSl .onValueChange = [this] { notify(); };
        makeSlider(compRatioSl,    1.0,  20.0,  0.5,   4.0);  compRatioSl  .onValueChange = [this] { notify(); };
        makeSlider(compAttackSl,   1.0,  500.0, 1.0,   10.0); compAttackSl .onValueChange = [this] { notify(); };
        makeSlider(compReleaseSl,  5.0,  2000.0,5.0,   100.0);compReleaseSl.onValueChange = [this] { notify(); };
        makeLabel(compThreshLbl,  "Threshold (dB)");
        makeLabel(compRatioLbl,   "Ratio");
        makeLabel(compAttackLbl,  "Attack (ms)");
        makeLabel(compReleaseLbl, "Release (ms)");

        // ---- Delay
        makeToggle(delayEnableBtn, "Enable");
        makeSlider(delayBeatsSl,   0.125, 2.0, 0.125, 0.5);  delayBeatsSl   .onValueChange = [this] { notify(); };
        makeSlider(delayFeedbackSl,0.0,   0.95, 0.01, 0.3);  delayFeedbackSl.onValueChange = [this] { notify(); };
        makeSlider(delayMixSl,     0.0,   1.0,  0.01, 0.25); delayMixSl     .onValueChange = [this] { notify(); };
        makeLabel(delayBeatsLbl,   "Time (beats)");
        makeLabel(delayFeedbackLbl,"Feedback");
        makeLabel(delayMixLbl,     "Mix");

        // ---- Reverb
        makeToggle(reverbEnableBtn, "Enable");
        makeSlider(reverbRoomSl,  0.0, 1.0, 0.01, 0.5);  reverbRoomSl .onValueChange = [this] { notify(); };
        makeSlider(reverbDampSl,  0.0, 1.0, 0.01, 0.5);  reverbDampSl .onValueChange = [this] { notify(); };
        makeSlider(reverbWetSl,   0.0, 1.0, 0.01, 0.25); reverbWetSl  .onValueChange = [this] { notify(); };
        makeSlider(reverbWidthSl, 0.0, 1.0, 0.01, 1.0);  reverbWidthSl.onValueChange = [this] { notify(); };
        makeLabel(reverbRoomLbl,  "Room Size");
        makeLabel(reverbDampLbl,  "Damping");
        makeLabel(reverbWetLbl,   "Wet Mix");
        makeLabel(reverbWidthLbl, "Width");

        setSize(440, 420);
    }

    void loadParams(const FXParams& p)
    {
        compEnableBtn.setToggleState(p.compEnabled, juce::dontSendNotification);
        compThreshSl .setValue(p.compThreshDB,  juce::dontSendNotification);
        compRatioSl  .setValue(p.compRatio,     juce::dontSendNotification);
        compAttackSl .setValue(p.compAttackMs,  juce::dontSendNotification);
        compReleaseSl.setValue(p.compReleaseMs, juce::dontSendNotification);

        delayEnableBtn .setToggleState(p.delayEnabled,  juce::dontSendNotification);
        delayBeatsSl   .setValue(p.delayBeats,    juce::dontSendNotification);
        delayFeedbackSl.setValue(p.delayFeedback, juce::dontSendNotification);
        delayMixSl     .setValue(p.delayMix,      juce::dontSendNotification);

        reverbEnableBtn.setToggleState(p.reverbEnabled, juce::dontSendNotification);
        reverbRoomSl .setValue(p.reverbRoom,  juce::dontSendNotification);
        reverbDampSl .setValue(p.reverbDamp,  juce::dontSendNotification);
        reverbWetSl  .setValue(p.reverbWet,   juce::dontSendNotification);
        reverbWidthSl.setValue(p.reverbWidth, juce::dontSendNotification);
    }

    void applyToParams(FXParams& p) const
    {
        p.compEnabled   = compEnableBtn.getToggleState();
        p.compThreshDB  = (float)compThreshSl .getValue();
        p.compRatio     = (float)compRatioSl  .getValue();
        p.compAttackMs  = (float)compAttackSl .getValue();
        p.compReleaseMs = (float)compReleaseSl.getValue();

        p.delayEnabled  = delayEnableBtn.getToggleState();
        p.delayBeats    = (float)delayBeatsSl   .getValue();
        p.delayFeedback = (float)delayFeedbackSl.getValue();
        p.delayMix      = (float)delayMixSl     .getValue();

        p.reverbEnabled = reverbEnableBtn.getToggleState();
        p.reverbRoom    = (float)reverbRoomSl .getValue();
        p.reverbDamp    = (float)reverbDampSl .getValue();
        p.reverbWet     = (float)reverbWetSl  .getValue();
        p.reverbWidth   = (float)reverbWidthSl.getValue();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a2e));

        auto sectionHeader = [&](const juce::String& text, int y)
        {
            g.setColour(juce::Colour(0xff9b59b6));
            g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f).withStyle("Bold")));
            g.drawText(text, 10, y, 420, 14, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff9b59b6).withAlpha(0.4f));
            g.drawLine(10.0f, (float)(y + 14), 430.0f, (float)(y + 14), 1.0f);
        };

        sectionHeader("COMPRESSOR", 8);
        sectionHeader("DELAY",      160);
        sectionHeader("REVERB",     282);
    }

    void resized() override
    {
        const int lblW = 100, slW = 250, h = 22, pad = 4, btnW = 70;

        // Compressor
        int y = 26;
        compEnableBtn.setBounds(10, y, btnW, h);
        y += h + pad;
        compThreshLbl .setBounds(10, y, lblW, h); compThreshSl .setBounds(110, y, slW, h); y += h + pad;
        compRatioLbl  .setBounds(10, y, lblW, h); compRatioSl  .setBounds(110, y, slW, h); y += h + pad;
        compAttackLbl .setBounds(10, y, lblW, h); compAttackSl .setBounds(110, y, slW, h); y += h + pad;
        compReleaseLbl.setBounds(10, y, lblW, h); compReleaseSl.setBounds(110, y, slW, h);

        // Delay
        y = 178;
        delayEnableBtn .setBounds(10, y, btnW, h);
        y += h + pad;
        delayBeatsLbl   .setBounds(10, y, lblW, h); delayBeatsSl   .setBounds(110, y, slW, h); y += h + pad;
        delayFeedbackLbl.setBounds(10, y, lblW, h); delayFeedbackSl.setBounds(110, y, slW, h); y += h + pad;
        delayMixLbl     .setBounds(10, y, lblW, h); delayMixSl     .setBounds(110, y, slW, h);

        // Reverb
        y = 300;
        reverbEnableBtn.setBounds(10, y, btnW, h);
        y += h + pad;
        reverbRoomLbl .setBounds(10, y, lblW, h); reverbRoomSl .setBounds(110, y, slW, h); y += h + pad;
        reverbDampLbl .setBounds(10, y, lblW, h); reverbDampSl .setBounds(110, y, slW, h); y += h + pad;
        reverbWetLbl  .setBounds(10, y, lblW, h); reverbWetSl  .setBounds(110, y, slW, h); y += h + pad;
        reverbWidthLbl.setBounds(10, y, lblW, h); reverbWidthSl.setBounds(110, y, slW, h);
    }

private:
    void notify() { if (onParamsChanged) onParamsChanged(); }

    // Compressor
    juce::TextButton compEnableBtn;
    juce::Slider compThreshSl, compRatioSl, compAttackSl, compReleaseSl;
    juce::Label  compThreshLbl, compRatioLbl, compAttackLbl, compReleaseLbl;

    // Delay
    juce::TextButton delayEnableBtn;
    juce::Slider delayBeatsSl, delayFeedbackSl, delayMixSl;
    juce::Label  delayBeatsLbl, delayFeedbackLbl, delayMixLbl;

    // Reverb
    juce::TextButton reverbEnableBtn;
    juce::Slider reverbRoomSl, reverbDampSl, reverbWetSl, reverbWidthSl;
    juce::Label  reverbRoomLbl, reverbDampLbl, reverbWetLbl, reverbWidthLbl;
};

// ---------------------------------------------------------------------------
// Floating DocumentWindow
// ---------------------------------------------------------------------------
class FXEditorWindow : public juce::DocumentWindow
{
public:
    FXEditorPanel panel;

    FXEditorWindow()
        : juce::DocumentWindow("FX Editor",
                               juce::Colour(0xff16213e),
                               juce::DocumentWindow::closeButton)
    {
        setContentNonOwned(&panel, false);
        setResizable(false, false);
        setSize(440, 420);
    }

    void setTrackName(const juce::String& name)
    {
        setName("FX — " + name);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FXEditorWindow)
};
