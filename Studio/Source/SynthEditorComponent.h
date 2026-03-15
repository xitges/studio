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
class SynthEditorContent : public juce::Component
{
public:
    struct WaveformPreview : public juce::Component
    {
        void setWaveformData(SynthPreview::WaveformData d)
        {
            data          = std::move(d);
            zoomLevel_    = 1.0f;   // reset view on new data
            scrollOffset_ = 0.0f;
            pathDirty_    = true;
            repaint();
        }

        void resized() override
        {
            pathDirty_ = true;  // geometry changed — rebuild paths at new pixel mapping
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (!e.mods.isRightButtonDown())
            {
                dragStartX_      = e.x;
                dragStartScroll_ = scrollOffset_;
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (zoomLevel_ <= 1.0f) return;   // nothing to pan at full view
            const float delta = (float)(e.x - dragStartX_)
                                / ((float)juce::jmax(1, getWidth()) * zoomLevel_);
            // Drag right → waveform shifts right → earlier content visible (offset decreases)
            scrollOffset_ = juce::jlimit(0.0f, 1.0f - 1.0f / zoomLevel_,
                                         dragStartScroll_ - delta);
            pathDirty_    = true;
            repaint();
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }

        void mouseWheelMove(const juce::MouseEvent& e,
                            const juce::MouseWheelDetails& w) override
        {
            // Ctrl/Cmd + scroll = zoom only; plain scroll is handled by drag
            if (!(e.mods.isCommandDown() || e.mods.isCtrlDown())) return;

            const float oldZoom    = zoomLevel_;
            zoomLevel_             = juce::jlimit(1.0f, kMaxZoom,
                                                  zoomLevel_ * std::exp(w.deltaY * 0.35f));
            const float mouseXFrac = (float)e.x / (float)juce::jmax(1, getWidth());
            const float anchor     = scrollOffset_ + mouseXFrac / oldZoom;
            scrollOffset_          = juce::jlimit(0.0f, 1.0f - 1.0f / zoomLevel_,
                                                  anchor - mouseXFrac / zoomLevel_);
            pathDirty_             = true;
            repaint();
        }

        void mouseDoubleClick(const juce::MouseEvent&) override
        {
            zoomLevel_    = 1.0f;
            scrollOffset_ = 0.0f;
            pathDirty_    = true;
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            const float W = (float)getWidth();
            const float H = (float)getHeight();

            // Background + grid
            g.fillAll(juce::Colour(0xff10182d));
            g.setColour(juce::Colour(0xff203050));
            for (float pct : { 0.25f, 0.5f, 0.75f })
                g.drawLine(0.0f, pct * H, W, pct * H, 0.5f);
            g.setColour(juce::Colour(0xff3498db).withAlpha(0.20f));
            g.drawLine(0.0f, H * 0.5f, W, H * 0.5f, 1.0f);

            const int n = (int)data.minVals.size();
            if (n > 0)
            {
                // Visible data range — clamped to prevent out-of-bounds access
                const int visStart = juce::jlimit(0, n - 1,
                                                  (int)(scrollOffset_ * (float)n));
                const int visEnd   = juce::jlimit(visStart + 1, n,
                                                  (int)((scrollOffset_ + 1.0f / zoomLevel_) * (float)n));
                const int visCount = visEnd - visStart;

                if (visCount > 0)
                {
                    if (pathDirty_)
                        rebuildPaths(visStart, visCount, n, W, H);

                    // Draw cached phase paths
                    const juce::PathStrokeType bar(1.0f);
                    g.setColour(juce::Colour(0xff44dd88).withAlpha(0.88f)); g.strokePath(attackPath_,  bar);
                    g.setColour(juce::Colour(0xffddaa33).withAlpha(0.88f)); g.strokePath(decayPath_,   bar);
                    g.setColour(juce::Colour(0xff3498db).withAlpha(0.88f)); g.strokePath(sustainPath_, bar);
                    g.setColour(juce::Colour(0xffdd4444).withAlpha(0.88f)); g.strokePath(releasePath_, bar);

                    // Draw cached boundary lines
                    const juce::PathStrokeType thin(1.0f);
                    g.setColour(juce::Colour(0xffddaa33).withAlpha(0.55f)); g.strokePath(boundAttack_,  thin);
                    g.setColour(juce::Colour(0xff3498db).withAlpha(0.55f)); g.strokePath(boundDecay_,   thin);
                    g.setColour(juce::Colour(0xffdd4444).withAlpha(0.55f)); g.strokePath(boundRelease_, thin);
                }

                // Zoom level indicator (top-right corner)
                if (zoomLevel_ > 1.05f)
                {
                    g.setColour(juce::Colours::white.withAlpha(0.45f));
                    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
                    g.drawText(juce::String(zoomLevel_, 1) + "x",
                               (int)W - 34, 4, 30, 12, juce::Justification::centredRight);
                }
            }

            // Border
            g.setColour(juce::Colour(0xff4aa3df));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
        }

    private:
        // Rebuild all cached paths from the current visible range.
        // Called only when pathDirty_ is true, then clears the flag.
        void rebuildPaths(int visStart, int visCount, int n, float W, float H)
        {
            attackPath_.clear();  decayPath_.clear();
            sustainPath_.clear(); releasePath_.clear();
            boundAttack_.clear(); boundDecay_.clear(); boundRelease_.clear();

            const float midY  = H * 0.5f;
            const float scale = juce::jmax(1.0f, H * 0.45f);
            const float denom = (float)juce::jmax(1, visCount - 1);

            for (int i = 0; i < visCount; ++i)
            {
                const int   di    = visStart + i;
                const float xFrac = (float)di / (float)juce::jmax(1, n - 1);
                const float x     = juce::jmap((float)i, 0.0f, denom, 0.0f, W);
                const float y1    = midY - data.maxVals[(size_t)di] * scale;
                const float y2    = midY - data.minVals[(size_t)di] * scale;

                juce::Path& p = (xFrac < data.attackFrac)  ? attackPath_
                              : (xFrac < data.decayFrac)   ? decayPath_
                              : (xFrac < data.releaseFrac) ? sustainPath_
                                                           : releasePath_;
                p.startNewSubPath(x, y1);
                p.lineTo(x, y2);
            }

            // Phase boundary lines — only rendered if within visible scroll range
            const float visStartFrac = scrollOffset_;
            const float visEndFrac   = scrollOffset_ + 1.0f / zoomLevel_;

            auto addBoundary = [&](float frac, juce::Path& bp)
            {
                if (frac <= 0.0f || frac >= 1.0f) return;
                if (frac < visStartFrac || frac > visEndFrac) return;
                const float x = ((frac - visStartFrac) * zoomLevel_) * W;
                bp.startNewSubPath(x, 0.0f);
                bp.lineTo(x, H);
            };
            addBoundary(data.attackFrac,  boundAttack_);
            addBoundary(data.decayFrac,   boundDecay_);
            addBoundary(data.releaseFrac, boundRelease_);

            pathDirty_ = false;
        }

        SynthPreview::WaveformData data;
        float      zoomLevel_      = 1.0f;
        float      scrollOffset_   = 0.0f;
        bool       pathDirty_      = true;
        int        dragStartX_     = 0;
        float      dragStartScroll_= 0.0f;

        juce::Path attackPath_, decayPath_, sustainPath_, releasePath_;
        juce::Path boundAttack_, boundDecay_, boundRelease_;

        static constexpr float kMaxZoom = 16.0f;
    };

    std::function<void()> onParamsChanged;
    std::function<void(const SynthParams&, int)> onPreviewRequested;
    std::function<void(const SynthParams&)> onSavePresetRequested;
    std::function<void(const juce::String&)> onRenamePresetRequested;
    std::function<void(const juce::String&)> onDeletePresetRequested;

    SynthEditorContent()
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
                onPreviewRequested(makeCurrentParams(), getSelectedPreviewMidiNote());
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
        waveBox.addItem("Pulse",    5);
        waveBox.addItem("Noise",    6);
        waveBox.addItem("Supersaw", 7);
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
        lfoTargetBox.addItem("Cutoff",     1);
        lfoTargetBox.addItem("Pitch",      2);
        lfoTargetBox.addItem("Amplitude",  3);
        lfoTargetBox.addItem("PulseWidth", 4);
        lfoTargetBox.setSelectedId(1, juce::dontSendNotification);
        lfoTargetBox.onChange = [this] { notify(); };
        addAndMakeVisible(lfoTargetBox);
        makeLabel(lfoRateLbl,   "LFO Rate (Hz)");
        makeLabel(lfoDepthLbl,  "LFO Depth");
        makeLabel(lfoTargetLbl, "LFO Target");

        // LFO extras
        lfoWaveformBox.addItem("Sine",      1);
        lfoWaveformBox.addItem("Triangle",  2);
        lfoWaveformBox.addItem("Saw",       3);
        lfoWaveformBox.addItem("Square",    4);
        lfoWaveformBox.addItem("S&H",       5);
        lfoWaveformBox.setSelectedId(1, juce::dontSendNotification);
        lfoWaveformBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff16213e));
        lfoWaveformBox.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffb0b0d8));
        lfoWaveformBox.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff3498db).withAlpha(0.6f));
        lfoWaveformBox.onChange = [this] { notify(); };
        addAndMakeVisible(lfoWaveformBox);
        makeLabel(lfoWaveformLbl, "LFO Shape");

        lfoFreeRunBtn.setButtonText("Free-Run");
        lfoFreeRunBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2c2c54));
        lfoFreeRunBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3498db));
        lfoFreeRunBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
        lfoFreeRunBtn.setClickingTogglesState(true);
        lfoFreeRunBtn.onClick = [this] { notify(); };
        addAndMakeVisible(lfoFreeRunBtn);

        makeSlider(lfoFadeInSl, 0.0, 2000.0, 1.0, 0.0); lfoFadeInSl.onValueChange = [this] { notify(); };
        makeLabel(lfoFadeInLbl, "LFO Fade (ms)");

        // Pulse width
        makeSlider(pulseWidthSl, 0.05, 0.95, 0.01, 0.5); pulseWidthSl.onValueChange = [this] { notify(); };
        makeLabel(pulseWidthLbl, "Pulse Width");

        // Filter extras
        filterTypeBox.addItem("Low-pass",  1);
        filterTypeBox.addItem("High-pass", 2);
        filterTypeBox.addItem("Band-pass", 3);
        filterTypeBox.setSelectedId(1, juce::dontSendNotification);
        filterTypeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff16213e));
        filterTypeBox.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffb0b0d8));
        filterTypeBox.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff3498db).withAlpha(0.6f));
        filterTypeBox.onChange = [this] { notify(); };
        addAndMakeVisible(filterTypeBox);
        makeLabel(filterTypeLbl, "Filter Type");

        makeSlider(filterDriveSl, 0.0, 1.0, 0.01, 0.0); filterDriveSl.onValueChange = [this] { notify(); };
        makeLabel(filterDriveLbl, "Drive");

        makeSlider(filterEnvAmountSl, -1.0, 1.0, 0.01, 0.5); filterEnvAmountSl.onValueChange = [this] { notify(); };
        makeLabel(filterEnvAmountLbl, "Env Depth");

        // Unison
        unisonVoicesSl.setRange(1.0, 8.0, 1.0);
        unisonVoicesSl.setValue(1.0, juce::dontSendNotification);
        unisonVoicesSl.setSliderStyle(juce::Slider::IncDecButtons);
        unisonVoicesSl.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 18);
        unisonVoicesSl.setColour(juce::Slider::textBoxTextColourId,       juce::Colours::white);
        unisonVoicesSl.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff16213e));
        unisonVoicesSl.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xff0f3460));
        unisonVoicesSl.onValueChange = [this] { notify(); };
        addAndMakeVisible(unisonVoicesSl);
        makeLabel(unisonVoicesLbl, "Voices");

        makeSlider(unisonDetuneSl, 0.0, 100.0, 0.1, 0.0); unisonDetuneSl.onValueChange = [this] { notify(); };
        makeLabel(unisonDetuneLbl, "Detune (ct)");

        makeSlider(unisonSpreadSl, 0.0, 1.0, 0.01, 0.5); unisonSpreadSl.onValueChange = [this] { notify(); };
        makeLabel(unisonSpreadLbl, "Spread");

        makeSlider(driftDepthSl, 0.0, 1.0, 0.01, 0.3); driftDepthSl.onValueChange = [this] { notify(); };
        makeLabel(driftDepthLbl, "Drift");

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
        pulseWidthSl.setValue(p.pulseWidth,   juce::dontSendNotification);
        unisonVoicesSl.setValue(p.unisonVoices, juce::dontSendNotification);
        unisonDetuneSl.setValue(p.unisonDetune, juce::dontSendNotification);
        unisonSpreadSl.setValue(p.unisonSpread, juce::dontSendNotification);
        driftDepthSl.setValue(p.driftDepth, juce::dontSendNotification);
        attackSl.setValue (p.attack,    juce::dontSendNotification);
        decaySl.setValue  (p.decay,     juce::dontSendNotification);
        sustainSl.setValue(p.sustain,   juce::dontSendNotification);
        releaseSl.setValue(p.release,   juce::dontSendNotification);
        cutoffSl.setValue   (p.cutoff,    juce::dontSendNotification);
        resonanceSl.setValue(p.resonance, juce::dontSendNotification);
        filterTypeBox.setSelectedId(p.filterType + 1, juce::dontSendNotification);
        filterDriveSl.setValue(p.filterDrive, juce::dontSendNotification);
        filterEnvAmountSl.setValue(p.filterEnvAmount, juce::dontSendNotification);
        lfoRateSl.setValue (p.lfoRate,  juce::dontSendNotification);
        lfoDepthSl.setValue(p.lfoDepth, juce::dontSendNotification);
        lfoTargetBox.setSelectedId(p.lfoTarget + 1, juce::dontSendNotification);
        lfoWaveformBox.setSelectedId(p.lfoWaveform + 1, juce::dontSendNotification);
        lfoFreeRunBtn.setToggleState(p.lfoFreeRun, juce::dontSendNotification);
        lfoFadeInSl.setValue(p.lfoFadeIn, juce::dontSendNotification);
        ddspEnableBtn.setToggleState(p.ddspAuto.enabled, juce::dontSendNotification);
        ddspAmountSl.setValue(p.ddspAuto.amount, juce::dontSendNotification);
        ddspBrightnessSl.setValue(p.ddspAuto.brightness, juce::dontSendNotification);
        ddspMotionSl.setValue(p.ddspAuto.motion, juce::dontSendNotification);
        updateWaveformPreview();
    }

    void applyToParams(SynthParams& p) const
    {
        p.enabled      = enableBtn.getToggleState();
        p.waveform     = waveBox.getSelectedId() - 1;
        p.pulseWidth   = (float)pulseWidthSl.getValue();
        p.unisonVoices = (int)unisonVoicesSl.getValue();
        p.unisonDetune = (float)unisonDetuneSl.getValue();
        p.unisonSpread = (float)unisonSpreadSl.getValue();
        p.driftDepth   = (float)driftDepthSl.getValue();
        p.attack       = (float)attackSl.getValue();
        p.decay        = (float)decaySl.getValue();
        p.sustain      = (float)sustainSl.getValue();
        p.release      = (float)releaseSl.getValue();
        p.cutoff       = (float)cutoffSl.getValue();
        p.resonance    = (float)resonanceSl.getValue();
        p.filterType        = filterTypeBox.getSelectedId() - 1;
        p.filterDrive       = (float)filterDriveSl.getValue();
        p.filterEnvAmount   = (float)filterEnvAmountSl.getValue();
        p.lfoRate      = (float)lfoRateSl.getValue();
        p.lfoDepth     = (float)lfoDepthSl.getValue();
        p.lfoTarget    = lfoTargetBox.getSelectedId() - 1;
        p.lfoWaveform  = lfoWaveformBox.getSelectedId() - 1;
        p.lfoFreeRun   = lfoFreeRunBtn.getToggleState();
        p.lfoFadeIn    = (float)lfoFadeInSl.getValue();
        p.ddspAuto.enabled    = ddspEnableBtn.getToggleState();
        p.ddspAuto.amount     = (float)ddspAmountSl.getValue();
        p.ddspAuto.brightness = (float)ddspBrightnessSl.getValue();
        p.ddspAuto.motion     = (float)ddspMotionSl.getValue();
        p.presetName = getSelectedPresetName();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a2e));

        auto sectionHeader = [&](const juce::String& text, int y)
        {
            g.setColour(juce::Colour(0xff3498db));
            g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
            g.drawText(text, 10, y, getWidth() - 20, 14, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff3498db).withAlpha(0.4f));
            g.drawLine(10.0f, (float)(y + 14), (float)(getWidth() - 10), (float)(y + 14), 1.0f);
        };

        sectionHeader("NOTE AUTO PATCH", yHeaderAutoPatch_);
        sectionHeader("UNISON",          yHeaderUnison_);
        sectionHeader("ADSR ENVELOPE",   yHeaderADSR_);
        sectionHeader("FILTER",          yHeaderFilter_);
        sectionHeader("LFO",             yHeaderLFO_);
    }

    void resized() override
    {
        constexpr int kRowH  = 24;   // button / combobox rows
        constexpr int kSlH   = 22;   // slider rows
        constexpr int kGap   = 4;    // between rows within a group
        constexpr int kGroup = 10;   // between groups / sections
        constexpr int kHdrH  = 18;   // section header (text + underline)
        constexpr int kLblW  = 92;   // label column width

        auto area = getLocalBounds().reduced(10, 8);
        const int slX  = area.getX() + kLblW + 3;   // slider/control left edge
        const int slW  = area.getWidth() - kLblW - 3;  // slider width to right margin

        // Helper: place a label + slider/control row and advance area
        auto placeSliderRow = [&](juce::Label& lbl, juce::Component& ctrl, int ctrlW)
        {
            auto row = area.removeFromTop(kSlH);
            lbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            ctrl.setBounds(slX, row.getY(), ctrlW, kSlH);
            area.removeFromTop(kGap);
        };

        // ---- Preset section -------------------------------------------------
        {
            auto row = area.removeFromTop(kRowH);
            presetLbl    .setBounds(row.removeFromLeft(48).withTrimmedTop(5));
            savePresetBtn.setBounds(row.removeFromRight(118));
            presetBox    .setBounds(row.reduced(3, 0));
            area.removeFromTop(kGap);
        }
        {
            auto row = area.removeFromTop(kRowH);
            renamePresetBtn.setBounds(row.removeFromLeft(110));
            row.removeFromLeft(4);
            deletePresetBtn.setBounds(row.removeFromLeft(110));
            area.removeFromTop(kGap);
        }
        {
            auto row = area.removeFromTop(kRowH);
            testNoteLbl.setBounds(row.removeFromLeft(62).withTrimmedTop(5));
            testNoteBox.setBounds(row.removeFromLeft(88));
            row.removeFromLeft(4);
            testBtn    .setBounds(row.removeFromLeft(124));
            area.removeFromTop(kGroup);
        }

        // ---- Oscillator section ---------------------------------------------
        {
            auto row = area.removeFromTop(kRowH);
            enableBtn.setBounds(row.removeFromLeft(120));
            row.removeFromLeft(8);
            waveLbl.setBounds(row.removeFromLeft(72).withTrimmedTop(5));
            waveBox.setBounds(row.removeFromLeft(120));
            area.removeFromTop(kGap);
        }
        placeSliderRow(pulseWidthLbl, pulseWidthSl, slW);
        previewLbl     .setBounds(area.removeFromTop(14));
        area.removeFromTop(2);
        waveformPreview.setBounds(area.removeFromTop(88));
        area.removeFromTop(kGroup);

        // ---- Auto Patch section ---------------------------------------------
        yHeaderAutoPatch_ = area.getY();
        area.removeFromTop(kHdrH + kGap);
        ddspEnableBtn.setBounds(area.removeFromTop(kRowH).removeFromLeft(130));
        area.removeFromTop(kGap);
        placeSliderRow(ddspAmountLbl,     ddspAmountSl,     slW);
        placeSliderRow(ddspBrightnessLbl, ddspBrightnessSl, slW);
        {
            auto row = area.removeFromTop(kSlH);
            ddspMotionLbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            ddspMotionSl .setBounds(slX, row.getY(), slW, kSlH);
            area.removeFromTop(kGroup);
        }

        // ---- Unison section -------------------------------------------------
        yHeaderUnison_ = area.getY();
        area.removeFromTop(kHdrH + kGap);
        {
            auto row = area.removeFromTop(kSlH);
            unisonVoicesLbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            unisonVoicesSl .setBounds(slX, row.getY(), 80, kSlH);
            area.removeFromTop(kGap);
        }
        placeSliderRow(unisonDetuneLbl, unisonDetuneSl, slW);
        placeSliderRow(unisonSpreadLbl, unisonSpreadSl, slW);
        {
            auto row = area.removeFromTop(kSlH);
            driftDepthLbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            driftDepthSl .setBounds(slX, row.getY(), slW, kSlH);
            area.removeFromTop(kGroup);
        }

        // ---- ADSR section ---------------------------------------------------
        yHeaderADSR_ = area.getY();
        area.removeFromTop(kHdrH + kGap);
        placeSliderRow(attackLbl,  attackSl,  slW);
        placeSliderRow(decayLbl,   decaySl,   slW);
        placeSliderRow(sustainLbl, sustainSl, slW);
        {
            auto row = area.removeFromTop(kSlH);
            releaseLbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            releaseSl .setBounds(slX, row.getY(), slW, kSlH);
            area.removeFromTop(kGroup);
        }

        // ---- Filter section -------------------------------------------------
        yHeaderFilter_ = area.getY();
        area.removeFromTop(kHdrH + kGap);
        {
            auto row = area.removeFromTop(kSlH);
            filterTypeLbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            filterTypeBox.setBounds(slX, row.getY(), 110, kSlH);
            area.removeFromTop(kGap);
        }
        placeSliderRow(cutoffLbl,     cutoffSl,     slW);
        placeSliderRow(resonanceLbl,  resonanceSl,  slW);
        placeSliderRow(filterDriveLbl,      filterDriveSl,      slW);
        {
            auto row = area.removeFromTop(kSlH);
            filterEnvAmountLbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            filterEnvAmountSl .setBounds(slX, row.getY(), slW, kSlH);
            area.removeFromTop(kGroup);
        }

        // ---- LFO section ----------------------------------------------------
        yHeaderLFO_ = area.getY();
        area.removeFromTop(kHdrH + kGap);
        {
            // LFO shape + free-run toggle on same row
            auto row = area.removeFromTop(kSlH);
            lfoWaveformLbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            lfoWaveformBox.setBounds(slX, row.getY(), 95, kSlH);
            lfoFreeRunBtn .setBounds(slX + 99, row.getY(), 80, kSlH);
            area.removeFromTop(kGap);
        }
        placeSliderRow(lfoFadeInLbl, lfoFadeInSl, slW);
        placeSliderRow(lfoRateLbl,   lfoRateSl,   slW);
        placeSliderRow(lfoDepthLbl,  lfoDepthSl,  slW);
        {
            auto row = area.removeFromTop(kSlH);
            lfoTargetLbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            lfoTargetBox.setBounds(slX, row.getY(), 110, kSlH);
        }
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
        const int w = juce::jmax(64, waveformPreview.getWidth());
        waveformPreview.setWaveformData(SynthPreview::renderWaveformData(makeCurrentParams(), w));
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

    // LFO extras
    juce::ComboBox   lfoWaveformBox;
    juce::Label      lfoWaveformLbl;
    juce::TextButton lfoFreeRunBtn;
    juce::Slider     lfoFadeInSl;
    juce::Label      lfoFadeInLbl;

    // Oscillator extras
    juce::Slider   pulseWidthSl;
    juce::Label    pulseWidthLbl;

    // Filter extras
    juce::ComboBox filterTypeBox;
    juce::Label    filterTypeLbl;
    juce::Slider   filterDriveSl;
    juce::Label    filterDriveLbl;
    juce::Slider   filterEnvAmountSl;
    juce::Label    filterEnvAmountLbl;

    // Unison
    juce::Slider   unisonVoicesSl, unisonDetuneSl, unisonSpreadSl;
    juce::Label    unisonVoicesLbl, unisonDetuneLbl, unisonSpreadLbl;
    juce::Slider   driftDepthSl;
    juce::Label    driftDepthLbl;

    std::vector<SynthPresets::Preset> availablePresets;
    int factoryPresetCount = 0;

    // Section header y positions — set in resized(), read in paint()
    int yHeaderAutoPatch_ = 0;
    int yHeaderUnison_    = 0;
    int yHeaderADSR_      = 0;
    int yHeaderFilter_    = 0;
    int yHeaderLFO_       = 0;
};

// ---------------------------------------------------------------------------
// SynthEditorPanel — thin Viewport wrapper around SynthEditorContent.
// Exposes the same public API as before so MainComponent needs no changes.
// ---------------------------------------------------------------------------
class SynthEditorPanel : public juce::Component
{
public:
    std::function<void()>                            onParamsChanged;
    std::function<void(const SynthParams&, int)>     onPreviewRequested;
    std::function<void(const SynthParams&)>          onSavePresetRequested;
    std::function<void(const juce::String&)>         onRenamePresetRequested;
    std::function<void(const juce::String&)>         onDeletePresetRequested;

    SynthEditorPanel()
    {
        // Wire content callbacks through to this panel's public lambdas.
        // Lambdas capture `this` by pointer, so they read the current
        // std::function value at call time — assigned later by MainComponent.
        content_.onParamsChanged         = [this] { if (onParamsChanged)         onParamsChanged(); };
        content_.onPreviewRequested      = [this](const SynthParams& p, int n)   { if (onPreviewRequested)      onPreviewRequested(p, n); };
        content_.onSavePresetRequested   = [this](const SynthParams& p)          { if (onSavePresetRequested)   onSavePresetRequested(p); };
        content_.onRenamePresetRequested = [this](const juce::String& s)         { if (onRenamePresetRequested) onRenamePresetRequested(s); };
        content_.onDeletePresetRequested = [this](const juce::String& s)         { if (onDeletePresetRequested) onDeletePresetRequested(s); };

        viewport_.setViewedComponent(&content_, false);
        viewport_.setScrollBarsShown(true, false);   // vertical only
        viewport_.setScrollBarThickness(10);
        addAndMakeVisible(viewport_);
    }

    void resized() override
    {
        viewport_.setBounds(getLocalBounds());
        // Content fills the full panel width minus the vertical scrollbar.
        const int contentW = juce::jmax(1, getWidth() - viewport_.getScrollBarThickness());
        content_.setSize(contentW, kContentH);
    }

    // ---- Forwarded API ----
    void loadParams(const SynthParams& p)         { content_.loadParams(p); }
    void applyToParams(SynthParams& p)      const { content_.applyToParams(p); }
    void setAvailablePresets(const std::vector<SynthPresets::Preset>& presets,
                             const juce::String& selectName = {})
    {
        content_.setAvailablePresets(presets, selectName);
    }

private:
    static constexpr int kContentH = 1110;   // tall enough for all controls
    juce::Viewport       viewport_;
    SynthEditorContent   content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditorPanel)
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
        setResizable(true, false);
        setSize(470, 750);
    }

    void setChannelName(const juce::String& name)
    {
        setName("Synth Editor - " + name);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthEditorWindow)
};
