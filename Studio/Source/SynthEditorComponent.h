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
#include "SynthEngine.h"

// ---------------------------------------------------------------------------
// Content panel
// ---------------------------------------------------------------------------
class SynthEditorPanel : public juce::Component
{
public:
    struct WaveformPreview : public juce::Component
    {
        void setWaveform(std::vector<float> samples)
        {
            waveform = std::move(samples);
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff10182d));
            g.setColour(juce::Colour(0xff203050));
            for (float pct : { 0.25f, 0.5f, 0.75f })
            {
                const float y = pct * (float)getHeight();
                g.drawLine(0.0f, y, (float)getWidth(), y, 0.5f);
            }

            g.setColour(juce::Colour(0xff3498db).withAlpha(0.25f));
            g.drawLine(0.0f, getHeight() * 0.5f, (float)getWidth(), getHeight() * 0.5f, 1.0f);

            if (waveform.empty())
                return;

            juce::Path path;
            const float midY = getHeight() * 0.5f;
            const float scaleY = juce::jmax(1.0f, getHeight() * 0.42f);
            for (size_t i = 0; i < waveform.size(); ++i)
            {
                const float x = juce::jmap((float)i, 0.0f, (float)juce::jmax<size_t>(1, waveform.size() - 1),
                                           0.0f, (float)getWidth());
                const float y = midY - waveform[i] * scaleY;
                if (i == 0)
                    path.startNewSubPath(x, y);
                else
                    path.lineTo(x, y);
            }

            g.setColour(juce::Colour(0xff8fd3ff));
            g.strokePath(path, juce::PathStrokeType(2.0f));
            g.setColour(juce::Colour(0xff4aa3df));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
        }

    private:
        std::vector<float> waveform;
    };

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

        // ---- Preset selector -----------------------------------------------
        presetLbl.setText("Preset", juce::dontSendNotification);
        presetLbl.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        presetLbl.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        addAndMakeVisible(presetLbl);

        // Populate from database
        {
            const auto presets = SynthPresets::getAll();
            for (int i = 0; i < (int)presets.size(); ++i)
                presetBox.addItem(presets[(size_t)i].name, i + 1);
        }
        presetBox.setTextWhenNothingSelected("-- Select Preset --");
        presetBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff16213e));
        presetBox.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffb0b0d8));
        presetBox.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff3498db).withAlpha(0.6f));
        presetBox.onChange = [this]
        {
            const int id = presetBox.getSelectedId();
            if (id <= 0) return;
            const auto presets = SynthPresets::getAll();
            const int idx = id - 1;
            if (idx < (int)presets.size())
            {
                loadParams(presets[(size_t)idx].params);
                notify();   // propagates to AudioEngine + ProjectModel
            }
        };
        addAndMakeVisible(presetBox);

        // ---- Enable toggle -----------------------------------------------
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
        previewLbl.setText("Wave Preview", juce::dontSendNotification);
        previewLbl.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        previewLbl.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        addAndMakeVisible(previewLbl);
        addAndMakeVisible(waveformPreview);

        ddspEnableBtn.setButtonText("Auto Patch");
        ddspEnableBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2c2c54));
        ddspEnableBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff8e44ad));
        ddspEnableBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
        ddspEnableBtn.setClickingTogglesState(true);
        ddspEnableBtn.onClick = [this] { notify(); };
        addAndMakeVisible(ddspEnableBtn);

        makeSlider(ddspAmountSl,     0.0, 1.0,  0.01, 0.0);  ddspAmountSl.onValueChange     = [this] { notify(); };
        makeSlider(ddspBrightnessSl, 0.0, 1.0,  0.01, 0.5);  ddspBrightnessSl.onValueChange = [this] { notify(); };
        makeSlider(ddspMotionSl,     0.0, 1.0,  0.01, 0.25); ddspMotionSl.onValueChange     = [this] { notify(); };
        makeLabel(ddspAmountLbl,     "Blend");
        makeLabel(ddspBrightnessLbl, "Brightness");
        makeLabel(ddspMotionLbl,     "Motion");

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

        updateWaveformPreview();
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
        ddspEnableBtn.setToggleState(p.ddspAuto.enabled, juce::dontSendNotification);
        ddspAmountSl.setValue(p.ddspAuto.amount, juce::dontSendNotification);
        ddspBrightnessSl.setValue(p.ddspAuto.brightness, juce::dontSendNotification);
        ddspMotionSl.setValue(p.ddspAuto.motion, juce::dontSendNotification);
        updateWaveformPreview();
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
        p.ddspAuto.enabled    = ddspEnableBtn.getToggleState();
        p.ddspAuto.amount     = (float)ddspAmountSl.getValue();
        p.ddspAuto.brightness = (float)ddspBrightnessSl.getValue();
        p.ddspAuto.motion     = (float)ddspMotionSl.getValue();
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

        sectionHeader("NOTE AUTO PATCH", 172);
        sectionHeader("ADSR ENVELOPE",   282);
        sectionHeader("FILTER",          392);
        sectionHeader("LFO",             452);
    }

    void resized() override
    {
        constexpr int lblW = 90, slW = 250, h = 22, pad = 6;

        // Row 0 — Preset selector
        int y = 10;
        presetLbl.setBounds(10,  y + 4, 46, 18);
        presetBox.setBounds(60,  y,     300, 26);
        y += 36;

        // Row 1 — Enable + Waveform
        enableBtn.setBounds(10,  y, 120, 26);
        waveLbl.setBounds  (150, y + 4, 70, 18);
        waveBox.setBounds  (220, y, 120, 26);
        y += 36;

        previewLbl.setBounds(10, y + 4, 80, 18);
        waveformPreview.setBounds(10, y + 24, 390, 88);

        y = 190;
        ddspEnableBtn.setBounds(10, y, 120, 26);
        y += 34;
        ddspAmountLbl    .setBounds(10, y, lblW, h); ddspAmountSl    .setBounds(100, y, slW, h); y += h + pad;
        ddspBrightnessLbl.setBounds(10, y, lblW, h); ddspBrightnessSl.setBounds(100, y, slW, h); y += h + pad;
        ddspMotionLbl    .setBounds(10, y, lblW, h); ddspMotionSl    .setBounds(100, y, slW, h);

        y = 300;
        attackLbl .setBounds(10, y, lblW, h); attackSl .setBounds(100, y, slW, h); y += h + pad;
        decayLbl  .setBounds(10, y, lblW, h); decaySl  .setBounds(100, y, slW, h); y += h + pad;
        sustainLbl.setBounds(10, y, lblW, h); sustainSl.setBounds(100, y, slW, h); y += h + pad;
        releaseLbl.setBounds(10, y, lblW, h); releaseSl.setBounds(100, y, slW, h);

        // Filter
        y = 410;
        cutoffLbl   .setBounds(10, y, lblW, h); cutoffSl   .setBounds(100, y, slW, h); y += h + pad;
        resonanceLbl.setBounds(10, y, lblW, h); resonanceSl.setBounds(100, y, slW, h);

        // LFO
        y = 470;
        lfoRateLbl  .setBounds(10, y, lblW, h); lfoRateSl  .setBounds(100, y, slW, h); y += h + pad;
        lfoDepthLbl .setBounds(10, y, lblW, h); lfoDepthSl .setBounds(100, y, slW, h); y += h + pad;
        lfoTargetLbl.setBounds(10, y, lblW, h); lfoTargetBox.setBounds(100, y, 100, h);
    }

private:
    SynthParams makeCurrentParams() const
    {
        SynthParams params;
        applyToParams(params);
        return params;
    }

    void updateWaveformPreview()
    {
        waveformPreview.setWaveform(SynthPreview::renderWaveform(makeCurrentParams(), 256));
    }

    void notify()
    {
        updateWaveformPreview();
        if (onParamsChanged) onParamsChanged();
    }

    // Preset selector
    juce::ComboBox presetBox;
    juce::Label    presetLbl;

    juce::TextButton enableBtn;
    juce::TextButton ddspEnableBtn;
    juce::ComboBox   waveBox;
    juce::Label      waveLbl;
    juce::Label      previewLbl;
    WaveformPreview  waveformPreview;

    juce::Slider attackSl, decaySl, sustainSl, releaseSl;
    juce::Label  attackLbl, decayLbl, sustainLbl, releaseLbl;

    juce::Slider ddspAmountSl, ddspBrightnessSl, ddspMotionSl;
    juce::Label  ddspAmountLbl, ddspBrightnessLbl, ddspMotionLbl;

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
        setSize(430, 610);
    }

    void setChannelName(const juce::String& name)
    {
        setName("Synth Editor - " + name);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditorWindow)
};
