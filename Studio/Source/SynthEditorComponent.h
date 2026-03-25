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
class SynthEditorContent : public juce::Component,
                           private juce::Timer
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
    std::function<void()>      onStopPreviewRequested;   // stop editor preview immediately
    std::function<bool()>      onIsPreviewActive;        // true while preview voice is rendering
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

        testBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        testBtn.onClick = [this]
        {
            if (previewPlaying_)
            {
                // User wants to stop the current preview
                if (onStopPreviewRequested) onStopPreviewRequested();
                previewPlaying_ = false;
                stopTimer();
                updateTestButtonAppearance();
            }
            else
            {
                // Trigger preview and begin polling for natural end
                if (onPreviewRequested)
                    onPreviewRequested(makeCurrentParams(), getSelectedPreviewMidiNote());
                previewPlaying_ = true;
                startTimer(80);  // poll every 80 ms to detect when voice finishes naturally
                updateTestButtonAppearance();
            }
        };
        updateTestButtonAppearance();
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
        waveBox.addItem("Plucked",  8);
        waveBox.addItem("Wind",     9);
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
        // Filter mode selector (Ladder / SVF)
        filterModeBox.addItem("Ladder", 1);
        filterModeBox.addItem("SVF",    2);
        filterModeBox.setSelectedId(1, juce::dontSendNotification);
        filterModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff16213e));
        filterModeBox.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffb0b0d8));
        filterModeBox.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff3498db).withAlpha(0.6f));
        filterModeBox.onChange = [this]
        {
            // SVF exposes Notch; Ladder does not
            const bool isSVF = (filterModeBox.getSelectedId() == 2);
            filterTypeBox.clear(juce::dontSendNotification);
            filterTypeBox.addItem("Low-pass",  1);
            filterTypeBox.addItem("High-pass", 2);
            filterTypeBox.addItem("Band-pass", 3);
            if (isSVF) filterTypeBox.addItem("Notch", 4);
            // Clamp selection if Notch was active and mode switched back to Ladder
            if (!isSVF && filterTypeBox.getSelectedId() == 0)
                filterTypeBox.setSelectedId(1, juce::dontSendNotification);
            notify();
        };
        addAndMakeVisible(filterModeBox);
        makeLabel(filterModeLbl, "Engine");

        // Filter topology selector (LP / HP / BP / Notch)
        filterTypeBox.addItem("Low-pass",  1);
        filterTypeBox.addItem("High-pass", 2);
        filterTypeBox.addItem("Band-pass", 3);
        filterTypeBox.setSelectedId(1, juce::dontSendNotification);
        filterTypeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff16213e));
        filterTypeBox.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffb0b0d8));
        filterTypeBox.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff3498db).withAlpha(0.6f));
        filterTypeBox.onChange = [this] { notify(); };
        addAndMakeVisible(filterTypeBox);
        makeLabel(filterTypeLbl, "Topology");

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

        // ---- Physical modelling sliders ----------------------------------------
        auto pmNotify = [this] { notify(); };
        // Plucked (KS)
        makeSlider(pmDampingSl,    0.0, 0.8, 0.01, 0.15); pmDampingSl.onValueChange    = pmNotify;
        makeLabel(pmDampingLbl, "Damping");
        makeSlider(pmDecaySl,      0.990, 1.0, 0.0001, 0.9985); pmDecaySl.onValueChange = pmNotify;
        makeLabel(pmDecayLbl, "Decay");
        makeSlider(pmStiffnessSl,  0.0, 0.8, 0.01, 0.3); pmStiffnessSl.onValueChange  = pmNotify;
        makeLabel(pmStiffnessLbl, "Stiffness");
        makeSlider(pmBrightnessSl, 1000, 16000, 100, 8000); pmBrightnessSl.onValueChange = pmNotify;
        makeLabel(pmBrightnessLbl, "Brightness");
        makeSlider(pmPluckPosSl,   0.02, 0.5, 0.01, 0.10); pmPluckPosSl.onValueChange = pmNotify;
        makeLabel(pmPluckPosLbl, "Pluck Pos");
        makeSlider(pmBodyFreqSl,   60, 400, 1, 180); pmBodyFreqSl.onValueChange = pmNotify;
        makeLabel(pmBodyFreqLbl, "Body Freq");
        makeSlider(pmBodyAmtSl,    0.0, 0.6, 0.01, 0.25); pmBodyAmtSl.onValueChange = pmNotify;
        makeLabel(pmBodyAmtLbl, "Body Amt");
        // Wind
        makeSlider(pmBreathSl,     0.1, 1.0, 0.01, 0.5); pmBreathSl.onValueChange    = pmNotify;
        makeLabel(pmBreathLbl, "Breath");
        makeSlider(pmReedSl,       0.1, 1.0, 0.01, 0.7); pmReedSl.onValueChange      = pmNotify;
        makeLabel(pmReedLbl, "Reed Stiff");
        makeSlider(pmBoreLossSl,   0.01, 0.5, 0.01, 0.15); pmBoreLossSl.onValueChange = pmNotify;
        makeLabel(pmBoreLossLbl, "Bore Loss");
        makeSlider(pmWindNoiseSl,  0.0, 0.2, 0.005, 0.03); pmWindNoiseSl.onValueChange = pmNotify;
        makeLabel(pmWindNoiseLbl, "Breath Noise");

        setAvailablePresets(SynthPresets::getAll());
        updateWaveformPreview();

        // ---- Source type toggle (Synth / Sampler) ----------------------------
        synthSourceBtn .setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3498db));
        samplerSourceBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2c3e50));
        synthSourceBtn.onClick = [this]
        {
            currentSourceType_ = ChannelSourceType::Synth;
            updateSourceVisibility();
            notify();
        };
        samplerSourceBtn.onClick = [this]
        {
            currentSourceType_ = ChannelSourceType::Sampler;
            // Reset to sampler-neutral processing so the loaded sample is heard
            // without synth-oriented defaults (filter sweep, resonance, etc.).
            // The user can shape ADSR/filter/LFO intentionally from this clean start.
            loadParams(samplerNeutralSynthParams());
            updateSourceVisibility();
            notify();
        };
        addAndMakeVisible(synthSourceBtn);
        addAndMakeVisible(samplerSourceBtn);

        // ---- Sampler-specific controls ----------------------------------------
        auto makeSamplerLabel = [this](juce::Label& lbl, const juce::String& text)
        {
            lbl.setText(text, juce::dontSendNotification);
            lbl.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
            lbl.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
            addAndMakeVisible(lbl);
        };
        auto makeSamplerSlider = [this](juce::Slider& s, double lo, double hi, double step, double val)
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
            s.onValueChange = [this] { notify(); };
            addAndMakeVisible(s);
        };

        makeSamplerLabel(samplerFileLbl, "Sample File");
        samplerFileNameLbl.setText("(none)", juce::dontSendNotification);
        samplerFileNameLbl.setColour(juce::Label::textColourId, juce::Colour(0xff8888aa));
        samplerFileNameLbl.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        addAndMakeVisible(samplerFileNameLbl);

        samplerBrowseBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2c3e50));
        samplerBrowseBtn.onClick = [this]
        {
            chooser_ = std::make_unique<juce::FileChooser>("Select sample...",
                juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                "*.wav;*.aiff;*.aif;*.flac;*.mp3");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc)
                {
                    auto results = fc.getResults();
                    if (results.isEmpty()) return;
                    currentSamplerParams_.samplePath = results[0].getFullPathName();
                    samplerFileNameLbl.setText(results[0].getFileName(), juce::dontSendNotification);
                    notify();
                });
        };
        addAndMakeVisible(samplerBrowseBtn);

        samplerRootNoteSl.textFromValueFunction = [](double v)
        {
            static const char* kNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            const int midi = juce::jlimit(0, 127, (int)v);
            return juce::String(kNames[midi % 12]) + juce::String(midi / 12 - 1);
        };
        makeSamplerSlider(samplerRootNoteSl, 0,    127,  1,    60.0);
        makeSamplerLabel (samplerRootNoteLbl, "Root Note");

        makeSamplerSlider(samplerFineTuneSl, -100, 100,  0.1,  0.0);
        makeSamplerLabel (samplerFineTuneLbl, "Fine Tune (ct)");

        makeSamplerSlider(samplerGainSl,     0.0,  2.0,  0.01, 1.0);
        makeSamplerLabel (samplerGainLbl,    "Gain");

        samplerLoopBtn.setClickingTogglesState(true);
        samplerLoopBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2c3e50));
        samplerLoopBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3498db));
        samplerLoopBtn.onClick = [this] { notify(); };
        addAndMakeVisible(samplerLoopBtn);

        samplerOneShotBtn.setClickingTogglesState(true);
        samplerOneShotBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2c3e50));
        samplerOneShotBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3498db));
        samplerOneShotBtn.onClick = [this] { notify(); };
        addAndMakeVisible(samplerOneShotBtn);

        updateSourceVisibility();
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
        // Load filter mode first so the filterTypeBox item list is correct
        filterModeBox.setSelectedId(p.filterMode + 1, juce::dontSendNotification);
        {
            const bool isSVF = (p.filterMode == 1);
            filterTypeBox.clear(juce::dontSendNotification);
            filterTypeBox.addItem("Low-pass",  1);
            filterTypeBox.addItem("High-pass", 2);
            filterTypeBox.addItem("Band-pass", 3);
            if (isSVF) filterTypeBox.addItem("Notch", 4);
        }
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
        p.filterMode        = filterModeBox.getSelectedId() - 1;
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

    void loadSamplerData(ChannelSourceType srcType, const SamplerParams& sp)
    {
        currentSourceType_   = srcType;
        currentSamplerParams_= sp;

        samplerRootNoteSl .setValue(sp.rootNote,       juce::dontSendNotification);
        samplerFineTuneSl .setValue(sp.fineTuneCents,  juce::dontSendNotification);
        samplerGainSl     .setValue(sp.gain,           juce::dontSendNotification);
        samplerLoopBtn    .setToggleState(sp.loopEnabled, juce::dontSendNotification);
        samplerOneShotBtn .setToggleState(sp.oneShot,     juce::dontSendNotification);
        samplerFileNameLbl.setText(sp.samplePath.isEmpty()
            ? "(none)" : juce::File(sp.samplePath).getFileName(),
            juce::dontSendNotification);
        updateSourceVisibility();
        updateWaveformPreview();
    }

    // Called by MainComponent after loading/changing the sample file.
    // The buffer is used to render a live waveform preview in sampler mode.
    void setSamplerPreviewBuffer(std::shared_ptr<const juce::AudioBuffer<float>> buf)
    {
        samplerPreviewBuffer_ = std::move(buf);
        updateWaveformPreview();
    }

    SamplerParams getSamplerParams() const
    {
        SamplerParams sp   = currentSamplerParams_;  // preserves samplePath
        sp.rootNote        = (int)samplerRootNoteSl.getValue();
        sp.fineTuneCents   = (float)samplerFineTuneSl.getValue();
        sp.gain            = (float)samplerGainSl.getValue();
        sp.loopEnabled     = samplerLoopBtn.getToggleState();
        sp.oneShot         = samplerOneShotBtn.getToggleState();
        return sp;
    }

    ChannelSourceType getSourceType() const { return currentSourceType_; }

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

        // Only show PM section header when waveform is Plucked or Wind
        const int wf = waveBox.getSelectedId() - 1;
        if (wf == 7 || wf == 8)
            sectionHeader("PHYSICAL MODEL", yHeaderPhysModel_);
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

        // ---- Source type row -------------------------------------------------
        {
            auto row = area.removeFromTop(kRowH);
            synthSourceBtn  .setBounds(row.removeFromLeft(68));
            row.removeFromLeft(4);
            samplerSourceBtn.setBounds(row.removeFromLeft(75));
            area.removeFromTop(kGap);
        }

        // ---- Oscillator / Sampler controls (shared region, 264px) ----------
        // Section is tall enough for sampler controls (rows ending at y=158)
        // plus the waveform preview (label at y=160, panel at y=176+88=264).
        // In synth mode, the preview sits at y=54; updateSourceVisibility()
        // moves it after the section is laid out.
        {
            const auto sec = area.removeFromTop(264);
            const int sx = sec.getX();
            const int sy = sec.getY();
            const int sw = sec.getWidth();
            oscillatorSectionY_ = sy;  // stored so updateSourceVisibility() can reposition preview

            // Row 1 (y=0): Enable button (always visible)
            enableBtn.setBounds(sx, sy, 120, kRowH);

            // Synth mode extras on row 1: waveform selector
            waveLbl.setBounds(sx + 128, sy + (kRowH - 14) / 2, 72, 14);
            waveBox.setBounds(sx + 200, sy, 120, kRowH);

            // Synth row 2 (y=28): Pulse Width slider
            pulseWidthLbl.setBounds(sx, sy + 28 + (kSlH - 14) / 2, kLblW, 14);
            pulseWidthSl .setBounds(slX, sy + 28, slW, kSlH);

            // Preview panel — default position (sampler mode: y=160/176).
            // updateSourceVisibility() repositions to y=54/70 in synth mode.
            previewLbl     .setBounds(sx, sy + 160, sw, 14);
            waveformPreview.setBounds(sx, sy + 176, sw, 88);

            // Sampler row 2 (y=28): file row
            samplerFileLbl    .setBounds(sx,  sy + 28 + (kRowH - 14) / 2, kLblW, 14);
            samplerFileNameLbl.setBounds(slX, sy + 28 + (kRowH - 14) / 2, slW - 76, 14);
            samplerBrowseBtn  .setBounds(slX + slW - 72, sy + 28, 72, kRowH);

            // Sampler row 3 (y=56): root note
            samplerRootNoteLbl.setBounds(sx,  sy + 56 + (kSlH - 14) / 2, kLblW, 14);
            samplerRootNoteSl .setBounds(slX, sy + 56, slW, kSlH);

            // Sampler row 4 (y=82): fine tune
            samplerFineTuneLbl.setBounds(sx,  sy + 82 + (kSlH - 14) / 2, kLblW, 14);
            samplerFineTuneSl .setBounds(slX, sy + 82, slW, kSlH);

            // Sampler row 5 (y=108): gain
            samplerGainLbl.setBounds(sx,  sy + 108 + (kSlH - 14) / 2, kLblW, 14);
            samplerGainSl .setBounds(slX, sy + 108, slW, kSlH);

            // Sampler row 6 (y=134): loop / one-shot toggles
            samplerLoopBtn   .setBounds(sx,      sy + 134, 70, kRowH);
            samplerOneShotBtn.setBounds(sx + 74, sy + 134, 80, kRowH);
        }
        area.removeFromTop(kGroup);

        updateSourceVisibility();

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
            filterModeLbl.setBounds(row.getX(), row.getY() + (kSlH - 14) / 2, kLblW, 14);
            filterModeBox.setBounds(slX, row.getY(), 84, kSlH);
            filterTypeLbl.setBounds(slX + 90, row.getY() + (kSlH - 14) / 2, 56, 14);
            filterTypeBox.setBounds(slX + 150, row.getY(), 110, kSlH);
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
            area.removeFromTop(kGroup);
        }

        // ---- Physical Model section -----------------------------------------
        yHeaderPhysModel_ = area.getY();
        area.removeFromTop(kHdrH + kGap);
        // Plucked params
        placeSliderRow(pmDampingLbl,    pmDampingSl,    slW);
        placeSliderRow(pmDecayLbl,      pmDecaySl,      slW);
        placeSliderRow(pmStiffnessLbl,  pmStiffnessSl,  slW);
        placeSliderRow(pmBrightnessLbl, pmBrightnessSl, slW);
        placeSliderRow(pmPluckPosLbl,   pmPluckPosSl,   slW);
        placeSliderRow(pmBodyFreqLbl,   pmBodyFreqSl,   slW);
        placeSliderRow(pmBodyAmtLbl,    pmBodyAmtSl,    slW);
        area.removeFromTop(kGap);
        // Wind params
        placeSliderRow(pmBreathLbl,     pmBreathSl,     slW);
        placeSliderRow(pmReedLbl,       pmReedSl,       slW);
        placeSliderRow(pmBoreLossLbl,   pmBoreLossSl,   slW);
        placeSliderRow(pmWindNoiseLbl,  pmWindNoiseSl,  slW);

      //  updatePhysModelVisibility();
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
        if (currentSourceType_ == ChannelSourceType::Sampler && samplerPreviewBuffer_)
        {
            waveformPreview.setWaveformData(
                SynthPreview::renderWaveformDataFromSampler(
                    makeCurrentParams(), getSamplerParams(),
                    samplerPreviewBuffer_, w));
        }
        else
        {
            waveformPreview.setWaveformData(
                SynthPreview::renderWaveformData(makeCurrentParams(), w));
        }
    }

    void updateSourceVisibility()
    {
        const bool isSynth   = (currentSourceType_ == ChannelSourceType::Synth);
        const bool isSampler = !isSynth;

        // Synth-only controls
        waveLbl        .setVisible(isSynth);
        waveBox        .setVisible(isSynth);
        pulseWidthLbl  .setVisible(isSynth);
        pulseWidthSl   .setVisible(isSynth);

        // Sampler-only controls
        samplerFileLbl    .setVisible(isSampler);
        samplerFileNameLbl.setVisible(isSampler);
        samplerBrowseBtn  .setVisible(isSampler);
        samplerRootNoteLbl.setVisible(isSampler);
        samplerRootNoteSl .setVisible(isSampler);
        samplerFineTuneLbl.setVisible(isSampler);
        samplerFineTuneSl .setVisible(isSampler);
        samplerGainLbl    .setVisible(isSampler);
        samplerGainSl     .setVisible(isSampler);
        samplerLoopBtn    .setVisible(isSampler);
        samplerOneShotBtn .setVisible(isSampler);

        // Waveform preview — always visible; repositioned based on mode.
        previewLbl     .setVisible(true);
        waveformPreview.setVisible(true);
        previewLbl.setText(isSampler ? "Sample Preview" : "Wave Preview",
                           juce::dontSendNotification);

        // Reposition preview panel.
        // In synth mode: after pulse-width row (y=54).
        // In sampler mode: after sampler controls (y=160).
        const auto contentBounds = getLocalBounds().reduced(10, 8);
        const int sx = contentBounds.getX();
        const int sw = contentBounds.getWidth();
        const int sy = oscillatorSectionY_;
        const int previewLblY    = isSynth ? sy + 54  : sy + 160;
        const int previewPanelY  = isSynth ? sy + 70  : sy + 176;
        previewLbl     .setBounds(sx, previewLblY,   sw, 14);
        waveformPreview.setBounds(sx, previewPanelY, sw, 88);

        // Toggle button highlight
        synthSourceBtn  .setColour(juce::TextButton::buttonColourId,
            isSynth   ? juce::Colour(0xff3498db) : juce::Colour(0xff2c3e50));
        samplerSourceBtn.setColour(juce::TextButton::buttonColourId,
            isSampler ? juce::Colour(0xff3498db) : juce::Colour(0xff2c3e50));
        repaint();
    }

    void notify()
    {
        updateWaveformPreview();
        if (onParamsChanged) onParamsChanged();
    }

    // Updates Test Sound button text and colour to reflect preview state.
    void updateTestButtonAppearance()
    {
        if (previewPlaying_)
        {
            testBtn.setButtonText("Stop Preview");
            testBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff8b2020));
            testBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffcc3333));
        }
        else
        {
            testBtn.setButtonText("Test Sound");
            testBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff204a87));
            testBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3498db));
        }
    }

    // Timer polling — auto-resets the button when the preview voice finishes naturally.
    void timerCallback() override
    {
        if (!previewPlaying_) { stopTimer(); return; }

        // Ask the engine if the voice is still alive
        const bool stillActive = onIsPreviewActive ? onIsPreviewActive() : false;
        if (!stillActive)
        {
            previewPlaying_ = false;
            stopTimer();
            updateTestButtonAppearance();
        }
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
    juce::ComboBox filterModeBox;
    juce::Label    filterModeLbl;
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

    // Physical modelling (Plucked / Wind)
    juce::Slider pmDampingSl, pmDecaySl, pmStiffnessSl, pmBrightnessSl;
    juce::Slider pmPluckPosSl, pmBodyFreqSl, pmBodyAmtSl;
    juce::Label  pmDampingLbl, pmDecayLbl, pmStiffnessLbl, pmBrightnessLbl;
    juce::Label  pmPluckPosLbl, pmBodyFreqLbl, pmBodyAmtLbl;
    juce::Slider pmBreathSl, pmReedSl, pmBoreLossSl, pmWindNoiseSl;
    juce::Label  pmBreathLbl, pmReedLbl, pmBoreLossLbl, pmWindNoiseLbl;
    int yHeaderPhysModel_ = 0;

    // Sampler Synth controls
    juce::TextButton synthSourceBtn   { "Synth" };
    juce::TextButton samplerSourceBtn { "Sampler" };

    juce::Label      samplerFileLbl;
    juce::Label      samplerFileNameLbl;
    juce::TextButton samplerBrowseBtn  { "Browse..." };

    juce::Label  samplerRootNoteLbl;
    juce::Slider samplerRootNoteSl;
    juce::Label  samplerFineTuneLbl;
    juce::Slider samplerFineTuneSl;
    juce::Label  samplerGainLbl;
    juce::Slider samplerGainSl;

    juce::TextButton samplerLoopBtn    { "Loop" };
    juce::TextButton samplerOneShotBtn { "One-shot" };

    ChannelSourceType currentSourceType_    = ChannelSourceType::Synth;
    SamplerParams     currentSamplerParams_ = {};
    bool              previewPlaying_       = false;  // true while editor preview is active

    // Sampler waveform preview — set by MainComponent after loading the sample buffer.
    std::shared_ptr<const juce::AudioBuffer<float>> samplerPreviewBuffer_;

    // Y-coordinate of the oscillator/sampler section top — set in resized(),
    // used by updateSourceVisibility() to reposition the preview panel.
    int oscillatorSectionY_ = 0;

    std::unique_ptr<juce::FileChooser> chooser_;

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
    std::function<void()>                            onStopPreviewRequested;
    std::function<bool()>                            onIsPreviewActive;
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
        content_.onStopPreviewRequested  = [this]                                { if (onStopPreviewRequested)  onStopPreviewRequested(); };
        content_.onIsPreviewActive       = [this]() -> bool                      { return onIsPreviewActive ? onIsPreviewActive() : false; };
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
    void loadSamplerData(ChannelSourceType srcType, const SamplerParams& sp)
                                                  { content_.loadSamplerData(srcType, sp); }
    void setSamplerPreviewBuffer(std::shared_ptr<const juce::AudioBuffer<float>> buf)
                                                  { content_.setSamplerPreviewBuffer(std::move(buf)); }
    SamplerParams     getSamplerParams() const     { return content_.getSamplerParams(); }
    ChannelSourceType getSourceType()    const     { return content_.getSourceType(); }
    void setAvailablePresets(const std::vector<SynthPresets::Preset>& presets,
                             const juce::String& selectName = {})
    {
        content_.setAvailablePresets(presets, selectName);
    }

private:
    static constexpr int kContentH = 1244;   // +106px for sampler waveform preview row
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
