/*
  ==============================================================================

    AutoTuneEditorComponent.h
    Created: 22 Mar 2026
    Author:  홍준영

    Auto-Tune editor panel + floating window.
    Header-only inline implementation.

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include <functional>
#include "ProjectModel.h"

// ---------------------------------------------------------------------------
// AutoTuneEditorPanel
// ---------------------------------------------------------------------------
class AutoTuneEditorPanel : public juce::Component
{
public:
    std::function<void()> onParamsChanged;

    AutoTuneEditorPanel()
    {
        // --- Enable toggle ---
        enableBtn.setButtonText("Enable");
        enableBtn.setClickingTogglesState(true);
        enableBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2c2c3a));
        enableBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff7b3fa0));
        enableBtn.onClick = [this] { fireChanged(); };
        addAndMakeVisible(enableBtn);

        // --- Key (tonic) ---
        keyLabel.setText("Key", juce::dontSendNotification);
        keyLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        keyLabel.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        addAndMakeVisible(keyLabel);

        for (int i = 0; i < 12; ++i)
        {
            static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            keyBox.addItem(noteNames[i], i + 1);
        }
        keyBox.setSelectedId(1, juce::dontSendNotification); // C
        keyBox.onChange = [this] { fireChanged(); };
        addAndMakeVisible(keyBox);

        // --- Scale ---
        scaleLabel.setText("Scale", juce::dontSendNotification);
        scaleLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        scaleLabel.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        addAndMakeVisible(scaleLabel);

        scaleBox.addItem("Major", 1);
        scaleBox.addItem("Minor", 2);
        scaleBox.setSelectedId(1, juce::dontSendNotification);
        scaleBox.onChange = [this] { fireChanged(); };
        addAndMakeVisible(scaleBox);

        // --- Retune Speed ---
        makeSlider(retuneSlider, 0.0, 1.0, 0.01, 0.0);
        makeLabel(retuneLabel, "Retune Speed");

        // --- Mix ---
        makeSlider(mixSlider, 0.0, 1.0, 0.01, 1.0);
        makeLabel(mixLabel, "Mix (Dry/Wet)");

        // --- Formant Preserve ---
        formantBtn.setButtonText("Formant Preserve");
        formantBtn.setClickingTogglesState(true);
        formantBtn.setToggleState(true, juce::dontSendNotification);
        formantBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2c2c3a));
        formantBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a6a3a));
        formantBtn.onClick = [this] { fireChanged(); };
        addAndMakeVisible(formantBtn);

        // --- Preset buttons ---
        presetHyperPop.setButtonText("Hyper-Pop");
        presetHyperPop.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a1a6a));
        presetHyperPop.onClick = [this] { applyPreset(0.0f, 1.0f, true); };
        addAndMakeVisible(presetHyperPop);

        presetSubtle.setButtonText("Subtle");
        presetSubtle.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a4a3a));
        presetSubtle.onClick = [this] { applyPreset(0.7f, 0.8f, true); };
        addAndMakeVisible(presetSubtle);

        presetRobotic.setButtonText("Robotic");
        presetRobotic.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a1a));
        presetRobotic.onClick = [this] { applyPreset(0.0f, 1.0f, false); };
        addAndMakeVisible(presetRobotic);

        // --- Pitch display ---
        pitchDisplay.setText("--", juce::dontSendNotification);
        pitchDisplay.setColour(juce::Label::textColourId, juce::Colour(0xffbb88ff));
        pitchDisplay.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
        pitchDisplay.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(pitchDisplay);

        pitchDisplayLabel.setText("Detected Pitch", juce::dontSendNotification);
        pitchDisplayLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        pitchDisplayLabel.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        pitchDisplayLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(pitchDisplayLabel);
    }

    void loadParams(const AutoTuneParams& p)
    {
        enableBtn.setToggleState(p.enabled, juce::dontSendNotification);
        keyBox.setSelectedId(p.keyTonic + 1, juce::dontSendNotification);
        scaleBox.setSelectedId(p.scaleType + 1, juce::dontSendNotification);
        retuneSlider.setValue(p.retuneSpeed, juce::dontSendNotification);
        mixSlider.setValue(p.mix, juce::dontSendNotification);
        formantBtn.setToggleState(p.formantPreserve, juce::dontSendNotification);
    }

    void applyToParams(AutoTuneParams& p) const
    {
        p.enabled         = enableBtn.getToggleState();
        p.keyTonic        = keyBox.getSelectedId() - 1;
        p.scaleType       = scaleBox.getSelectedId() - 1;
        p.retuneSpeed     = (float)retuneSlider.getValue();
        p.mix             = (float)mixSlider.getValue();
        p.formantPreserve = formantBtn.getToggleState();
    }

    void setDetectedPitch(float hz, float targetHz, float rms = 0.0f)
    {
        if (hz <= 0.0f || std::isnan(hz) || std::isinf(hz))
        {
            if (rms < 0.001f)
                pitchDisplay.setText("No signal", juce::dontSendNotification);
            else
                pitchDisplay.setText("-- (level: " + juce::String(rms, 3) + ")", juce::dontSendNotification);
            return;
        }

        // Convert to note name
        const int midi = (int)std::round(12.0f * std::log2(hz / 440.0f) + 69.0f);
        static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        const int octave = midi / 12 - 1;
        const int pc = ((midi % 12) + 12) % 12;

        juce::String text = juce::String(names[pc]) + juce::String(octave)
                          + "  (" + juce::String((int)hz) + " Hz)";

        if (targetHz > 0.0f && !std::isnan(targetHz) && !std::isinf(targetHz))
        {
            const int tMidi = (int)std::round(12.0f * std::log2(targetHz / 440.0f) + 69.0f);
            const int tOctave = tMidi / 12 - 1;
            const int tPc = ((tMidi % 12) + 12) % 12;
            text += "  ->  " + juce::String(names[tPc]) + juce::String(tOctave);
        }

        pitchDisplay.setText(text, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a2e));

        // Section headers
        g.setColour(juce::Colour(0xff2a2a4a));
        g.fillRoundedRectangle(8.0f, 8.0f, (float)getWidth() - 16.0f, 36.0f, 6.0f);

        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
        g.drawText("Auto-Tune", 16, 12, 200, 28, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        area.removeFromTop(44); // header

        // Enable
        enableBtn.setBounds(area.removeFromTop(28).reduced(0, 2));
        area.removeFromTop(6);

        // Key + Scale row
        {
            auto row = area.removeFromTop(28);
            keyLabel.setBounds(row.removeFromLeft(30));
            keyBox.setBounds(row.removeFromLeft(70).reduced(0, 2));
            row.removeFromLeft(10);
            scaleLabel.setBounds(row.removeFromLeft(38));
            scaleBox.setBounds(row.removeFromLeft(80).reduced(0, 2));
        }
        area.removeFromTop(8);

        // Retune Speed
        retuneLabel.setBounds(area.removeFromTop(16));
        retuneSlider.setBounds(area.removeFromTop(24).reduced(0, 2));
        area.removeFromTop(6);

        // Mix
        mixLabel.setBounds(area.removeFromTop(16));
        mixSlider.setBounds(area.removeFromTop(24).reduced(0, 2));
        area.removeFromTop(8);

        // Formant
        formantBtn.setBounds(area.removeFromTop(28).reduced(0, 2));
        area.removeFromTop(10);

        // Presets row
        {
            auto row = area.removeFromTop(28);
            const int w = row.getWidth() / 3;
            presetHyperPop.setBounds(row.removeFromLeft(w).reduced(2));
            presetSubtle.setBounds(row.removeFromLeft(w).reduced(2));
            presetRobotic.setBounds(row.reduced(2));
        }
        area.removeFromTop(10);

        // Pitch display
        pitchDisplayLabel.setBounds(area.removeFromTop(14));
        pitchDisplay.setBounds(area.removeFromTop(22));
    }

private:
    juce::TextButton enableBtn;
    juce::Label      keyLabel;
    juce::ComboBox   keyBox;
    juce::Label      scaleLabel;
    juce::ComboBox   scaleBox;
    juce::Label      retuneLabel;
    juce::Slider     retuneSlider;
    juce::Label      mixLabel;
    juce::Slider     mixSlider;
    juce::TextButton formantBtn;

    juce::TextButton presetHyperPop;
    juce::TextButton presetSubtle;
    juce::TextButton presetRobotic;

    juce::Label      pitchDisplay;
    juce::Label      pitchDisplayLabel;

    void makeSlider(juce::Slider& s, double lo, double hi, double step, double val)
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
        s.onValueChange = [this] { fireChanged(); };
        addAndMakeVisible(s);
    }

    void makeLabel(juce::Label& lbl, const juce::String& text)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        lbl.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        addAndMakeVisible(lbl);
    }

    void applyPreset(float retune, float mix, bool formant)
    {
        retuneSlider.setValue(retune, juce::dontSendNotification);
        mixSlider.setValue(mix, juce::dontSendNotification);
        formantBtn.setToggleState(formant, juce::dontSendNotification);
        fireChanged();
    }

    void fireChanged() { if (onParamsChanged) onParamsChanged(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoTuneEditorPanel)
};

// ---------------------------------------------------------------------------
// AutoTuneEditorWindow
// ---------------------------------------------------------------------------
class AutoTuneEditorWindow : public juce::DocumentWindow
{
public:
    AutoTuneEditorPanel panel;

    AutoTuneEditorWindow()
        : juce::DocumentWindow("Auto-Tune",
                               juce::Colour(0xff1a1a2e),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(false);
        setContentNonOwned(&panel, false);
        setResizable(false, false);
        setSize(360, 380);
    }

    void setTrackName(const juce::String& name)
    {
        setName("Auto-Tune - " + name);
    }

    void closeButtonPressed() override { setVisible(false); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoTuneEditorWindow)
};
