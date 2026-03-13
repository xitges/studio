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
    std::function<void(const SynthParams&, int)> onPreviewRequested;
    std::function<void(const SynthParams&)> onSavePresetRequested;
    std::function<void(const juce::String&)> onRenamePresetRequested;
    std::function<void(const juce::String&)> onDeletePresetRequested;

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

        presetBox.setTextWhenNothingSelected("-- Select Preset --");
        presetBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff16213e));
        presetBox.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffb0b0d8));
        presetBox.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff3498db).withAlpha(0.6f));
        presetBox.onChange = [this]
        {
            const int id = presetBox.getSelectedId();
            updatePresetActionState();
            if (id <= 0) return;
            const int idx = id - 1;
            if (idx < (int)availablePresets.size())
            {
                loadParams(availablePresets[(size_t)idx].params);
                notify();   // propagates to AudioEngine + ProjectModel
            }
        };
        addAndMakeVisible(presetBox);

        savePresetBtn.setButtonText("Save Preset");
        savePresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6a4f));
        savePresetBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff40916c));
        savePresetBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        savePresetBtn.onClick = [this]
        {
            if (onSavePresetRequested)
                onSavePresetRequested(makeCurrentParams());
        };
        addAndMakeVisible(savePresetBtn);

        renamePresetBtn.setButtonText("Rename");
        renamePresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7f5539));
        renamePresetBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff9c6644));
        renamePresetBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        renamePresetBtn.onClick = [this]
        {
            if (onRenamePresetRequested)
            {
                const auto presetName = getSelectedPresetName();
                if (presetName.isNotEmpty() && isSelectedPresetCustom())
                    onRenamePresetRequested(presetName);
            }
        };
        addAndMakeVisible(renamePresetBtn);

        deletePresetBtn.setButtonText("Delete");
        deletePresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff7f1d1d));
        deletePresetBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb91c1c));
        deletePresetBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        deletePresetBtn.onClick = [this]
        {
            if (onDeletePresetRequested)
            {
                const auto presetName = getSelectedPresetName();
                if (presetName.isNotEmpty() && isSelectedPresetCustom())
                    onDeletePresetRequested(presetName);
            }
        };
        addAndMakeVisible(deletePresetBtn);

        testNoteLbl.setText("Test Note", juce::dontSendNotification);
        testNoteLbl.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        testNoteLbl.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        addAndMakeVisible(testNoteLbl);

        testNoteBox.addItem("C2", 1);
        testNoteBox.addItem("C3", 2);
        testNoteBox.addItem("C4", 3);
        testNoteBox.addItem("C5", 4);
        testNoteBox.setSelectedId(3, juce::dontSendNotification);
        testNoteBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff16213e));
        testNoteBox.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffb0b0d8));
        testNoteBox.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff3498db).withAlpha(0.6f));
        addAndMakeVisible(testNoteBox);

        testBtn.setButtonText("Test Sound");
        testBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff204a87));
        testBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3498db));
        testBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        testBtn.onClick = [this]
        {
            if (onPreviewRequested)
            {
                auto params = makeCurrentParams();
                params.enabled = true;
                onPreviewRequested(params, getSelectedPreviewMidiNote());
            }
        };
        addAndMakeVisible(testBtn);

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

        setAvailablePresets(SynthPresets::getAll());
        updateWaveformPreview();
    }

    void setAvailablePresets(const std::vector<SynthPresets::Preset>& presets,
                             const juce::String& selectPresetName = {})
    {
        availablePresets = presets;
        factoryPresetCount = (int)SynthPresets::getAll().size();
        presetBox.clear(juce::dontSendNotification);
        for (int i = 0; i < (int)availablePresets.size(); ++i)
            presetBox.addItem(availablePresets[(size_t)i].name, i + 1);

        if (selectPresetName.isNotEmpty())
        {
            for (int i = 0; i < (int)availablePresets.size(); ++i)
            {
                if (availablePresets[(size_t)i].name == selectPresetName)
                {
                    presetBox.setSelectedId(i + 1, juce::dontSendNotification);
                    updatePresetActionState();
                    return;
                }
            }
        }

        presetBox.setSelectedId(0, juce::dontSendNotification);
        updatePresetActionState();
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
            g.drawLine(10.0f, (float)(y + 14), 450.0f, (float)(y + 14), 1.0f);
        };

        sectionHeader("NOTE AUTO PATCH", 244);
        sectionHeader("ADSR ENVELOPE",   354);
        sectionHeader("FILTER",          464);
        sectionHeader("LFO",             524);
    }

    void resized() override
    {
        constexpr int lblW = 90, slW = 250, h = 22, pad = 6;

        // Row 0 — Preset selector
        int y = 10;
        presetLbl.setBounds(10,  y + 4, 46, 18);
        presetBox.setBounds(60,  y,     240, 26);
        savePresetBtn.setBounds(312, y, 118, 26);
        y += 36;

        renamePresetBtn.setBounds(60, y, 110, 26);
        deletePresetBtn.setBounds(180, y, 110, 26);
        y += 36;

        testNoteLbl.setBounds(10, y + 4, 60, 18);
        testNoteBox.setBounds(74, y, 88, 26);
        testBtn.setBounds(176, y, 124, 26);
        y += 36;

        // Row 1 — Enable + Waveform
        enableBtn.setBounds(10,  y, 120, 26);
        waveLbl.setBounds  (150, y + 4, 70, 18);
        waveBox.setBounds  (220, y, 120, 26);
        y += 36;

        previewLbl.setBounds(10, y + 4, 80, 18);
        waveformPreview.setBounds(10, y + 24, 390, 88);

        y = 262;
        ddspEnableBtn.setBounds(10, y, 120, 26);
        y += 34;
        ddspAmountLbl    .setBounds(10, y, lblW, h); ddspAmountSl    .setBounds(100, y, slW, h); y += h + pad;
        ddspBrightnessLbl.setBounds(10, y, lblW, h); ddspBrightnessSl.setBounds(100, y, slW, h); y += h + pad;
        ddspMotionLbl    .setBounds(10, y, lblW, h); ddspMotionSl    .setBounds(100, y, slW, h);

        y = 372;
        attackLbl .setBounds(10, y, lblW, h); attackSl .setBounds(100, y, slW, h); y += h + pad;
        decayLbl  .setBounds(10, y, lblW, h); decaySl  .setBounds(100, y, slW, h); y += h + pad;
        sustainLbl.setBounds(10, y, lblW, h); sustainSl.setBounds(100, y, slW, h); y += h + pad;
        releaseLbl.setBounds(10, y, lblW, h); releaseSl.setBounds(100, y, slW, h);

        // Filter
        y = 482;
        cutoffLbl   .setBounds(10, y, lblW, h); cutoffSl   .setBounds(100, y, slW, h); y += h + pad;
        resonanceLbl.setBounds(10, y, lblW, h); resonanceSl.setBounds(100, y, slW, h);

        // LFO
        y = 542;
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

    juce::String getSelectedPresetName() const
    {
        const int idx = presetBox.getSelectedId() - 1;
        return (idx >= 0 && idx < (int)availablePresets.size())
            ? availablePresets[(size_t)idx].name
            : juce::String{};
    }

    bool isSelectedPresetCustom() const
    {
        const int idx = presetBox.getSelectedId() - 1;
        return idx >= factoryPresetCount && idx < (int)availablePresets.size();
    }

    void updatePresetActionState()
    {
        const bool canEditSelected = isSelectedPresetCustom();
        renamePresetBtn.setEnabled(canEditSelected);
        deletePresetBtn.setEnabled(canEditSelected);
        renamePresetBtn.setAlpha(canEditSelected ? 1.0f : 0.45f);
        deletePresetBtn.setAlpha(canEditSelected ? 1.0f : 0.45f);
    }

    int getSelectedPreviewMidiNote() const
    {
        switch (testNoteBox.getSelectedId())
        {
            case 1: return 36;
            case 2: return 48;
            case 4: return 72;
            default: return 60;
        }
    }

    // Preset selector
    juce::ComboBox presetBox;
    juce::Label    presetLbl;
    juce::TextButton savePresetBtn;
    juce::TextButton renamePresetBtn;
    juce::TextButton deletePresetBtn;
    juce::ComboBox testNoteBox;
    juce::Label    testNoteLbl;
    juce::TextButton testBtn;

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
    std::vector<SynthPresets::Preset> availablePresets;
    int factoryPresetCount = 0;
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
        setSize(470, 690);
    }

    void setChannelName(const juce::String& name)
    {
        setName("Synth Editor - " + name);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditorWindow)
};
