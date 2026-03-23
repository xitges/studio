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
// AutoTuneEditorPanel — full control surface for the Auto-Tune engine
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
        enableBtn.onClick = [this] { markCustom(); fireChanged(); };
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
        keyBox.setSelectedId(1, juce::dontSendNotification);
        keyBox.onChange = [this] { markCustom(); fireChanged(); };
        addAndMakeVisible(keyBox);

        // --- Scale ---
        scaleLabel.setText("Scale", juce::dontSendNotification);
        scaleLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        scaleLabel.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        addAndMakeVisible(scaleLabel);

        scaleBox.addItem("Major", 1);
        scaleBox.addItem("Minor", 2);
        scaleBox.setSelectedId(1, juce::dontSendNotification);
        scaleBox.onChange = [this] { markCustom(); fireChanged(); };
        addAndMakeVisible(scaleBox);

        // --- All sliders ---
        makeSlider(retuneSlider,       0.0, 1.0, 0.01, 0.0);
        makeLabel(retuneLabel,         "Retune Speed");

        makeSlider(corrAmountSlider,   0.0, 1.0, 0.01, 1.0);
        makeLabel(corrAmountLabel,     "Correction Amount");

        makeSlider(humanizeSlider,     0.0, 1.0, 0.01, 0.0);
        makeLabel(humanizeLabel,       "Humanize");

        makeSlider(flexTuneSlider,     0.0, 1.0, 0.01, 0.0);
        makeLabel(flexTuneLabel,       "Flex Tune");

        makeSlider(transitionSlider,   0.0, 1.0, 0.01, 0.0);
        makeLabel(transitionLabel,     "Transition / Glide");

        makeSlider(vibratoSlider,      0.0, 1.0, 0.01, 0.5);
        makeLabel(vibratoLabel,        "Vibrato Preserve");

        makeSlider(formantSlider,      0.0, 1.0, 0.01, 1.0);
        makeLabel(formantLabel,        "Formant Preserve");

        makeSlider(mixSlider,          0.0, 1.0, 0.01, 1.0);
        makeLabel(mixLabel,            "Mix (Dry/Wet)");

        // --- Note mask toggles (12 chromatic notes) ---
        {
            static const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            for (int i = 0; i < 12; ++i)
            {
                noteMaskBtns[i].setButtonText(noteNames[i]);
                noteMaskBtns[i].setClickingTogglesState(true);
                noteMaskBtns[i].setToggleState(true, juce::dontSendNotification);
                noteMaskBtns[i].setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2c2c3a));
                noteMaskBtns[i].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4a3a6a));
                noteMaskBtns[i].onClick = [this] { markCustom(); fireChanged(); };
                addAndMakeVisible(noteMaskBtns[i]);
            }
        }

        useNoteMaskBtn.setButtonText("Custom Note Mask");
        useNoteMaskBtn.setClickingTogglesState(true);
        useNoteMaskBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2c2c3a));
        useNoteMaskBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a5a6a));
        useNoteMaskBtn.onClick = [this] { markCustom(); fireChanged(); };
        addAndMakeVisible(useNoteMaskBtn);

        // --- Preset selector ---
        presetBox.addItem("Subtle",      1);
        presetBox.addItem("Natural Pop",  2);
        presetBox.addItem("Modern Pop",   3);
        presetBox.addItem("Hard Tune",    4);
        presetBox.addItem("Hyper-Pop",    5);
        presetBox.addItem("Robotic",      6);
        presetBox.addItem("Custom",       7);
        presetBox.setSelectedId(3, juce::dontSendNotification);
        presetBox.onChange = [this] { applyPresetFromBox(); };
        addAndMakeVisible(presetBox);

        presetLabel.setText("Preset", juce::dontSendNotification);
        presetLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        presetLabel.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        addAndMakeVisible(presetLabel);

        // --- Pitch diagnostics display ---
        pitchDisplay.setText("--", juce::dontSendNotification);
        pitchDisplay.setColour(juce::Label::textColourId, juce::Colour(0xffbb88ff));
        pitchDisplay.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        pitchDisplay.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(pitchDisplay);

        pitchDisplayLabel.setText("Pitch Diagnostics", juce::dontSendNotification);
        pitchDisplayLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        pitchDisplayLabel.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        pitchDisplayLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(pitchDisplayLabel);

        // --- Confidence bar ---
        confLabel.setText("Conf: --", juce::dontSendNotification);
        confLabel.setColour(juce::Label::textColourId, juce::Colour(0xff77aa77));
        confLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        confLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(confLabel);
    }

    void loadParams(const AutoTuneParams& p)
    {
        enableBtn.setToggleState(p.enabled, juce::dontSendNotification);
        keyBox.setSelectedId(p.keyTonic + 1, juce::dontSendNotification);
        scaleBox.setSelectedId(p.scaleType + 1, juce::dontSendNotification);
        retuneSlider.setValue(p.retuneSpeed, juce::dontSendNotification);
        corrAmountSlider.setValue(p.correctionAmount, juce::dontSendNotification);
        humanizeSlider.setValue(p.humanize, juce::dontSendNotification);
        flexTuneSlider.setValue(p.flexTune, juce::dontSendNotification);
        transitionSlider.setValue(p.transitionSpeed, juce::dontSendNotification);
        vibratoSlider.setValue(p.vibratoPreserve, juce::dontSendNotification);
        formantSlider.setValue(p.formantAmount, juce::dontSendNotification);
        mixSlider.setValue(p.mix, juce::dontSendNotification);
        useNoteMaskBtn.setToggleState(p.useNoteMask, juce::dontSendNotification);
        for (int i = 0; i < 12; ++i)
            noteMaskBtns[i].setToggleState(p.noteMask[i], juce::dontSendNotification);
        presetBox.setSelectedId(p.presetIndex + 1, juce::dontSendNotification);
    }

    void applyToParams(AutoTuneParams& p) const
    {
        p.enabled          = enableBtn.getToggleState();
        p.keyTonic         = keyBox.getSelectedId() - 1;
        p.scaleType        = scaleBox.getSelectedId() - 1;
        p.retuneSpeed      = (float)retuneSlider.getValue();
        p.correctionAmount = (float)corrAmountSlider.getValue();
        p.humanize         = (float)humanizeSlider.getValue();
        p.flexTune         = (float)flexTuneSlider.getValue();
        p.transitionSpeed  = (float)transitionSlider.getValue();
        p.vibratoPreserve  = (float)vibratoSlider.getValue();
        p.formantAmount    = (float)formantSlider.getValue();
        p.formantPreserve  = p.formantAmount >= 0.5f; // legacy compat
        p.mix              = (float)mixSlider.getValue();
        p.useNoteMask      = useNoteMaskBtn.getToggleState();
        for (int i = 0; i < 12; ++i)
            p.noteMask[i] = noteMaskBtns[i].getToggleState();
        p.presetIndex      = presetBox.getSelectedId() - 1;
    }

    void setDetectedPitch(float hz, float targetHz, float rms = 0.0f,
                          float confidence = 0.0f, bool voiced = false,
                          float correctedHz = 0.0f)
    {
        static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

        if (hz <= 0.0f || std::isnan(hz) || std::isinf(hz))
        {
            if (rms < 0.001f)
                pitchDisplay.setText("No signal", juce::dontSendNotification);
            else
                pitchDisplay.setText(voiced ? "Voiced (no lock)" : "Unvoiced",
                                     juce::dontSendNotification);
            confLabel.setText("Conf: --", juce::dontSendNotification);
            return;
        }

        // Detected note
        const int midi = (int)std::round(12.0f * std::log2(hz / 440.0f) + 69.0f);
        const int octave = midi / 12 - 1;
        const int pc = ((midi % 12) + 12) % 12;

        juce::String text = juce::String(names[pc]) + juce::String(octave)
                          + " (" + juce::String((int)hz) + "Hz)";

        // Target note
        if (targetHz > 0.0f && !std::isnan(targetHz) && !std::isinf(targetHz))
        {
            const int tMidi = (int)std::round(12.0f * std::log2(targetHz / 440.0f) + 69.0f);
            const int tOctave = tMidi / 12 - 1;
            const int tPc = ((tMidi % 12) + 12) % 12;
            text += " -> " + juce::String(names[tPc]) + juce::String(tOctave);

            // Show cents deviation
            if (correctedHz > 0.0f)
            {
                float corrMidi = 12.0f * std::log2(correctedHz / 440.0f) + 69.0f;
                float targMidi = 12.0f * std::log2(targetHz / 440.0f) + 69.0f;
                int cents = (int)std::round((corrMidi - targMidi) * 100.0f);
                if (cents != 0)
                    text += " (" + juce::String(cents > 0 ? "+" : "") + juce::String(cents) + "c)";
            }
        }

        pitchDisplay.setText(text, juce::dontSendNotification);

        // Confidence display
        int confPct = (int)(confidence * 100.0f);
        confLabel.setText("Conf: " + juce::String(confPct) + "%"
                        + (voiced ? " [Voiced]" : " [Unvoiced]"),
                         juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a2e));

        // Header
        g.setColour(juce::Colour(0xff2a2a4a));
        g.fillRoundedRectangle(8.0f, 8.0f, (float)getWidth() - 16.0f, 36.0f, 6.0f);

        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
        g.drawText("Auto-Tune", 16, 12, 200, 28, juce::Justification::centredLeft);

        // Section dividers
        g.setColour(juce::Colour(0xff2a2a3a));
        const int noteY = noteMaskYStart_;
        if (noteY > 0)
        {
            g.drawHorizontalLine(noteY - 6, 12.0f, (float)getWidth() - 12.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        area.removeFromTop(44); // header

        // Enable + Preset row
        {
            auto row = area.removeFromTop(28);
            enableBtn.setBounds(row.removeFromLeft(70).reduced(0, 2));
            row.removeFromLeft(10);
            presetLabel.setBounds(row.removeFromLeft(42));
            presetBox.setBounds(row.reduced(0, 2));
        }
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

        // Sliders (2 per row to save vertical space)
        auto layoutSliderPair = [&](juce::Label& l1, juce::Slider& s1,
                                    juce::Label& l2, juce::Slider& s2)
        {
            auto lblRow = area.removeFromTop(14);
            auto halfW = lblRow.getWidth() / 2;
            l1.setBounds(lblRow.removeFromLeft(halfW));
            l2.setBounds(lblRow);

            auto sldRow = area.removeFromTop(22);
            halfW = sldRow.getWidth() / 2;
            s1.setBounds(sldRow.removeFromLeft(halfW).reduced(0, 1));
            s2.setBounds(sldRow.reduced(0, 1));
            area.removeFromTop(3);
        };

        auto layoutSliderSingle = [&](juce::Label& l, juce::Slider& s)
        {
            l.setBounds(area.removeFromTop(14));
            s.setBounds(area.removeFromTop(22).reduced(0, 1));
            area.removeFromTop(3);
        };

        layoutSliderPair(retuneLabel,   retuneSlider,
                         corrAmountLabel, corrAmountSlider);
        layoutSliderPair(humanizeLabel, humanizeSlider,
                         flexTuneLabel, flexTuneSlider);
        layoutSliderPair(transitionLabel, transitionSlider,
                         vibratoLabel,    vibratoSlider);
        layoutSliderPair(formantLabel, formantSlider,
                         mixLabel,     mixSlider);

        area.removeFromTop(4);

        // Note mask section
        noteMaskYStart_ = area.getY();
        useNoteMaskBtn.setBounds(area.removeFromTop(24).reduced(0, 2));
        area.removeFromTop(2);

        // 12 note buttons in 2 rows of 6
        {
            auto row1 = area.removeFromTop(22);
            auto row2 = area.removeFromTop(22);
            const int w = row1.getWidth() / 6;
            for (int i = 0; i < 6; ++i)
                noteMaskBtns[i].setBounds(row1.removeFromLeft(w).reduced(1));
            for (int i = 6; i < 12; ++i)
                noteMaskBtns[i].setBounds(row2.removeFromLeft(w).reduced(1));
        }
        area.removeFromTop(8);

        // Diagnostics
        pitchDisplayLabel.setBounds(area.removeFromTop(14));
        pitchDisplay.setBounds(area.removeFromTop(20));
        confLabel.setBounds(area.removeFromTop(16));
    }

private:
    // --- Controls ---
    juce::TextButton enableBtn;
    juce::Label      keyLabel;
    juce::ComboBox   keyBox;
    juce::Label      scaleLabel;
    juce::ComboBox   scaleBox;

    // Correction sliders
    juce::Label  retuneLabel;       juce::Slider retuneSlider;
    juce::Label  corrAmountLabel;   juce::Slider corrAmountSlider;
    juce::Label  humanizeLabel;     juce::Slider humanizeSlider;
    juce::Label  flexTuneLabel;     juce::Slider flexTuneSlider;
    juce::Label  transitionLabel;   juce::Slider transitionSlider;
    juce::Label  vibratoLabel;      juce::Slider vibratoSlider;
    juce::Label  formantLabel;      juce::Slider formantSlider;
    juce::Label  mixLabel;          juce::Slider mixSlider;

    // Note mask
    juce::TextButton noteMaskBtns[12];
    juce::TextButton useNoteMaskBtn;

    // Preset
    juce::Label    presetLabel;
    juce::ComboBox presetBox;

    // Diagnostics
    juce::Label pitchDisplay;
    juce::Label pitchDisplayLabel;
    juce::Label confLabel;

    int noteMaskYStart_ = 0;

    void makeSlider(juce::Slider& s, double lo, double hi, double step, double val)
    {
        s.setRange(lo, hi, step);
        s.setValue(val, juce::dontSendNotification);
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 16);
        s.setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff16213e));
        s.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff0f3460));
        s.setColour(juce::Slider::thumbColourId,             juce::Colour(0xff9b59b6));
        s.setColour(juce::Slider::trackColourId,             juce::Colour(0xff9b59b6));
        s.onValueChange = [this] { markCustom(); fireChanged(); };
        addAndMakeVisible(s);
    }

    void makeLabel(juce::Label& lbl, const juce::String& text)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        lbl.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        addAndMakeVisible(lbl);
    }

    // --- Preset definitions ---
    struct PresetValues
    {
        float retuneSpeed, correctionAmount, humanize, flexTune;
        float transitionSpeed, vibratoPreserve, formantAmount, mix;
    };

    static PresetValues getPreset(int idx)
    {
        switch (idx)
        {
            case 0: // Subtle
                return { 0.65f, 0.6f, 0.5f, 0.4f, 0.1f, 0.9f, 1.0f, 0.7f };
            case 1: // Natural Pop
                return { 0.4f,  0.8f, 0.3f, 0.2f, 0.05f, 0.7f, 1.0f, 0.9f };
            case 2: // Modern Pop
                return { 0.15f, 0.95f, 0.1f, 0.05f, 0.0f, 0.4f, 1.0f, 1.0f };
            case 3: // Hard Tune
                return { 0.0f,  1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.85f, 1.0f };
            case 4: // Hyper-Pop
                return { 0.0f,  1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f };
            case 5: // Robotic
                return { 0.0f,  1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
            default: // Custom — don't change values
                return { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
        }
    }

    void applyPresetFromBox()
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx < 0 || idx >= 6) return; // Custom or invalid — no change

        auto pv = getPreset(idx);
        if (pv.retuneSpeed < 0.0f) return; // Custom sentinel

        retuneSlider.setValue(pv.retuneSpeed, juce::dontSendNotification);
        corrAmountSlider.setValue(pv.correctionAmount, juce::dontSendNotification);
        humanizeSlider.setValue(pv.humanize, juce::dontSendNotification);
        flexTuneSlider.setValue(pv.flexTune, juce::dontSendNotification);
        transitionSlider.setValue(pv.transitionSpeed, juce::dontSendNotification);
        vibratoSlider.setValue(pv.vibratoPreserve, juce::dontSendNotification);
        formantSlider.setValue(pv.formantAmount, juce::dontSendNotification);
        mixSlider.setValue(pv.mix, juce::dontSendNotification);

        fireChanged();
    }

    void markCustom()
    {
        // When user manually changes a slider, switch preset to Custom
        // (unless we're in the middle of loading a preset)
        if (presetBox.getSelectedId() != 7) // 7 = Custom
            presetBox.setSelectedId(7, juce::dontSendNotification);
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
        setSize(420, 560);
    }

    void setTrackName(const juce::String& name)
    {
        setName("Auto-Tune - " + name);
    }

    void closeButtonPressed() override { setVisible(false); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoTuneEditorWindow)
};
