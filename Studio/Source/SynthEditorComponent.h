/*
  ==============================================================================

    SynthEditorComponent.h  — M13 Synth parameter editor
    Floating window per channel: waveform, ADSR, filter, LFO

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <functional>
#include "ProjectModel.h"

// ---------------------------------------------------------------------------
// Content panel
// ---------------------------------------------------------------------------
class SynthEditorPanel : public juce::Component
{
public:
    std::function<void()> onParamsChanged;

    SynthEditorPanel()
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
            s.setColour(juce::Slider::thumbColourId,             juce::Colour(0xff3498db));
            s.setColour(juce::Slider::trackColourId,             juce::Colour(0xff3498db));
            addAndMakeVisible(s);
        };

        // Enable toggle
        enableBtn.setButtonText("Enable Synth");
        enableBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2c2c54));
        enableBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff27ae60));
        enableBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
        enableBtn.setClickingTogglesState(true);
        enableBtn.onClick = [this] { notify(); };
        addAndMakeVisible(enableBtn);

        // Waveform
        waveBox.addItem("Sine",     1);
        waveBox.addItem("Saw",      2);
        waveBox.addItem("Square",   3);
        waveBox.addItem("Triangle", 4);
        waveBox.setSelectedId(2, juce::dontSendNotification);
        waveBox.onChange = [this] { notify(); };
        addAndMakeVisible(waveBox);
        makeLabel(waveLbl, "Waveform");

        // ADSR
        makeSlider(attackSl,  1.0,  2000.0, 1.0,  5.0);   attackSl.onValueChange  = [this] { notify(); };
        makeSlider(decaySl,   1.0,  2000.0, 1.0,  80.0);   decaySl.onValueChange   = [this] { notify(); };
        makeSlider(sustainSl, 0.0,  1.0,    0.01, 0.6);    sustainSl.onValueChange = [this] { notify(); };
        makeSlider(releaseSl, 1.0,  4000.0, 1.0,  200.0);  releaseSl.onValueChange = [this] { notify(); };
        makeLabel(attackLbl,  "Attack (ms)");
        makeLabel(decayLbl,   "Decay (ms)");
        makeLabel(sustainLbl, "Sustain");
        makeLabel(releaseLbl, "Release (ms)");

        // Filter
        makeSlider(cutoffSl,    20.0, 20000.0, 1.0,  4000.0); cutoffSl.onValueChange    = [this] { notify(); };
        makeSlider(resonanceSl, 0.0,  1.0,     0.01, 0.3);    resonanceSl.onValueChange = [this] { notify(); };
        makeLabel(cutoffLbl,    "Cutoff (Hz)");
        makeLabel(resonanceLbl, "Resonance");

        // LFO
        makeSlider(lfoRateSl,  0.1, 20.0, 0.1, 2.0);   lfoRateSl.onValueChange  = [this] { notify(); };
        makeSlider(lfoDepthSl, 0.0, 1.0,  0.01, 0.0);   lfoDepthSl.onValueChange = [this] { notify(); };
        lfoTargetBox.addItem("Cutoff", 1);
        lfoTargetBox.addItem("Pitch",  2);
        lfoTargetBox.setSelectedId(1, juce::dontSendNotification);
        lfoTargetBox.onChange = [this] { notify(); };
        addAndMakeVisible(lfoTargetBox);
        makeLabel(lfoRateLbl,   "LFO Rate (Hz)");
        makeLabel(lfoDepthLbl,  "LFO Depth");
        makeLabel(lfoTargetLbl, "LFO Target");
    }

    void loadParams(const SynthParams& p)
    {
        enableBtn.setToggleState(p.enabled,   juce::dontSendNotification);
        waveBox.setSelectedId(p.waveform + 1, juce::dontSendNotification);
        attackSl.setValue (p.attack,    juce::dontSendNotification);
        decaySl.setValue  (p.decay,     juce::dontSendNotification);
        sustainSl.setValue(p.sustain,   juce::dontSendNotification);
        releaseSl.setValue(p.release,   juce::dontSendNotification);
        cutoffSl.setValue   (p.cutoff,    juce::dontSendNotification);
        resonanceSl.setValue(p.resonance, juce::dontSendNotification);
        lfoRateSl.setValue (p.lfoRate,  juce::dontSendNotification);
        lfoDepthSl.setValue(p.lfoDepth, juce::dontSendNotification);
        lfoTargetBox.setSelectedId(p.lfoTarget + 1, juce::dontSendNotification);
    }

    void applyToParams(SynthParams& p) const
    {
        p.enabled   = enableBtn.getToggleState();
        p.waveform  = waveBox.getSelectedId() - 1;
        p.attack    = (float)attackSl.getValue();
        p.decay     = (float)decaySl.getValue();
        p.sustain   = (float)sustainSl.getValue();
        p.release   = (float)releaseSl.getValue();
        p.cutoff    = (float)cutoffSl.getValue();
        p.resonance = (float)resonanceSl.getValue();
        p.lfoRate   = (float)lfoRateSl.getValue();
        p.lfoDepth  = (float)lfoDepthSl.getValue();
        p.lfoTarget = lfoTargetBox.getSelectedId() - 1;
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a2e));

        auto sectionHeader = [&](const juce::String& text, int y)
        {
            g.setColour(juce::Colour(0xff3498db));
            g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
            g.drawText(text, 10, y, 400, 14, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff3498db).withAlpha(0.4f));
            g.drawLine(10.0f, (float)(y + 14), 410.0f, (float)(y + 14), 1.0f);
        };

        sectionHeader("ADSR ENVELOPE", 52);
        sectionHeader("FILTER",        162);
        sectionHeader("LFO",           222);
    }

    void resized() override
    {
        // Row layout helper
        int y = 10;
        enableBtn.setBounds(10, y, 120, 26);
        waveLbl.setBounds(150, y + 4, 70, 18);
        waveBox.setBounds (220, y, 120, 26);
        y += 40;

        // ADSR (starts at y=50, section header at y=52)
        y = 70;
        const int lblW = 90, slW = 250, h = 22, pad = 6;
        attackLbl .setBounds(10, y,      lblW, h); attackSl .setBounds(100, y,      slW, h); y += h + pad;
        decayLbl  .setBounds(10, y,      lblW, h); decaySl  .setBounds(100, y,      slW, h); y += h + pad;
        sustainLbl.setBounds(10, y,      lblW, h); sustainSl.setBounds(100, y,      slW, h); y += h + pad;
        releaseLbl.setBounds(10, y,      lblW, h); releaseSl.setBounds(100, y,      slW, h);

        // Filter
        y = 180;
        cutoffLbl   .setBounds(10, y,  lblW, h); cutoffSl   .setBounds(100, y,  slW, h); y += h + pad;
        resonanceLbl.setBounds(10, y,  lblW, h); resonanceSl.setBounds(100, y,  slW, h);

        // LFO
        y = 240;
        lfoRateLbl  .setBounds(10, y,  lblW, h); lfoRateSl  .setBounds(100, y,  slW, h); y += h + pad;
        lfoDepthLbl .setBounds(10, y,  lblW, h); lfoDepthSl .setBounds(100, y,  slW, h); y += h + pad;
        lfoTargetLbl.setBounds(10, y,  lblW, h); lfoTargetBox.setBounds(100, y, 100, h);
    }

private:
    void notify() { if (onParamsChanged) onParamsChanged(); }

    juce::TextButton enableBtn;
    juce::ComboBox   waveBox;
    juce::Label      waveLbl;

    juce::Slider attackSl, decaySl, sustainSl, releaseSl;
    juce::Label  attackLbl, decayLbl, sustainLbl, releaseLbl;

    juce::Slider cutoffSl, resonanceSl;
    juce::Label  cutoffLbl, resonanceLbl;

    juce::Slider   lfoRateSl, lfoDepthSl;
    juce::ComboBox lfoTargetBox;
    juce::Label    lfoRateLbl, lfoDepthLbl, lfoTargetLbl;
};

// ---------------------------------------------------------------------------
// Floating DocumentWindow wrapping SynthEditorPanel
// ---------------------------------------------------------------------------
class SynthEditorWindow : public juce::DocumentWindow
{
public:
    SynthEditorPanel panel;

    SynthEditorWindow()
        : juce::DocumentWindow("Synth Editor",
                               juce::Colour(0xff16213e),
                               juce::DocumentWindow::closeButton)
    {
        setContentNonOwned(&panel, false);
        setResizable(false, false);
        setSize(430, 370);
    }

    void setChannelName(const juce::String& name)
    {
        setName("Synth Editor - " + name);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditorWindow)
};
