/*
  ==============================================================================
    MixerComponent.h  —  M5 Mixer
    8 insert strips + master. Inline implementation.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "ProjectModel.h"

class MixerComponent : public juce::Component
{
public:
    static constexpr int numTracks  = 8;
    static constexpr int stripW     = 100;   // phase-5: 64px per insert strip
    static constexpr int masterW    = 160;  // phase-5: 120px master

    // Phase-5: per-strip colour palette (matches channel rack)
    static juce::Colour stripColor(int t)
    {
        static const juce::uint32 pal[] = {
            0xffe89ba0, // Soft Coral
            0xffe89ba0, // Soft Coral
            0xffe89ba0, // Soft Coral
            0xffe89ba0, // Soft Coral
            0xffe89ba0, // Soft Coral
            0xffe89ba0, // Soft Coral
            0xffe89ba0, // Soft Coral
            0xffe89ba0, // Soft Coral
        };
        return t < numTracks ? juce::Colour(pal[t]) : juce::Colour(0xffd8412a);
    }

    // Phase-5: meter levels (set from AudioEngine tick)
    float meterLevel[numTracks + 1] = {};
    void setMeterLevel(int t, float lvl)
    {
        if (t >= 0 && t <= numTracks && meterLevel[t] != lvl)
        {
            meterLevel[t] = lvl;
            repaint();
        }
    }

    // Phase-5: FX active state (set from project after load)
    bool fxActive[numTracks][3] = {};  // [t][0=cmp, 1=dly, 2=rvb]
    void setFxActive(int t, int slot, bool on)
    {
        if (t >= 0 && t < numTracks && slot >= 0 && slot < 3)
        {
            fxActive[t][slot] = on;
            repaint();
        }
    }

    // Callbacks → MainComponent → AudioEngine
    std::function<void(int track, float vol)>   onTrackVolumeChanged;
    std::function<void(int track, float pan)>   onTrackPanChanged;
    std::function<void(int track, bool muted)>  onTrackMuteChanged;
    std::function<void(int track, bool soloed)> onTrackSoloChanged;
    std::function<void(float)>                  onMasterVolumeChanged;
    std::function<void(float)>                  onMasterPanChanged;
    std::function<void(int track)>                     onFXButtonClicked;       // M14 open FX editor
    std::function<void(int track)>                     onEQButtonClicked;       // DynEQ (0-7=tracks, 8=master)
    std::function<void(int track)>                     onAutoTuneButtonClicked; // Auto-Tune
    std::function<void(int track, int slot, bool on)>  onFXSlotToggled;         // CMP/DLY/RVB toggle

    MixerComponent()
    {
        for (int t = 0; t <= numTracks; ++t)   // 0..7 = inserts, 8 = master
            buildStrip(t);
        setOpaque(true);
    }

    // Sync UI from project data after load / new
    void loadFromProject(const Project& proj)
    {
        for (int t = 0; t < numTracks && t < (int)proj.mixerTracks.size(); ++t)
        {
            const auto& mt = proj.mixerTracks[(size_t)t];
            faders[t]   ->setValue(mt.volume, juce::dontSendNotification);
            pans[t]     ->setValue(mt.pan,    juce::dontSendNotification);
            muteBtns[t] ->setToggleState(mt.muted,  juce::dontSendNotification);
            soloBtns[t] ->setToggleState(mt.soloed, juce::dontSendNotification);
            labels[t]   ->setText(mt.name, juce::dontSendNotification);
            syncMuteColour(t);
            syncSoloColour(t);
            // Phase-5: sync FX active states + toggle button states
            if (t < (int)proj.fxParams.size())
            {
                fxActive[t][0] = proj.fxParams[(size_t)t].compEnabled;
                fxActive[t][1] = proj.fxParams[(size_t)t].delayEnabled;
                fxActive[t][2] = proj.fxParams[(size_t)t].reverbEnabled;
                if (cmpBtns[t]) cmpBtns[t]->setToggleState(fxActive[t][0], juce::dontSendNotification);
                if (dlyBtns[t]) dlyBtns[t]->setToggleState(fxActive[t][1], juce::dontSendNotification);
                if (rvbBtns[t]) rvbBtns[t]->setToggleState(fxActive[t][2], juce::dontSendNotification);
            }
        }
        // Master (index numTracks)
        faders[numTracks]->setValue(proj.masterVolume, juce::dontSendNotification);
        pans[numTracks]  ->setValue(proj.masterPan,    juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        using LF = StudioLookAndFeel;
        const int H = getHeight();

        {
            juce::ColourGradient bg(juce::Colour(0xffffffff), 0.0f, 0.0f,
                                    juce::Colour(0xffe0e0e0), 0.0f, (float)H, false);
            g.setGradientFill(bg);
            g.fillAll();
        }

        // Strip borders + colour tabs + labels + meters + dB readouts
        for (int t = 0; t <= numTracks; ++t)
        {
            const bool isMaster = (t == numTracks);
            const int  sx  = (t < numTracks) ? t * stripW : numTracks * stripW;
            const int  sw  = isMaster ? masterW : stripW;
            const juce::Colour col = stripColor(t);

            // Strip bg
            {
                juce::ColourGradient sg(juce::Colour(0xffffffff).brighter(0.04f), 0.0f, 0.0f,
                                        juce::Colour(0xffe0e0e0), 0.0f, (float)H, false);
                g.setGradientFill(sg);
                g.fillRect(sx, 0, sw, H);
            }

            // Master: accent border + glow
            if (isMaster)
            {
                g.setColour(juce::Colour(LF::kAccent).withAlpha(0.6f));
                g.drawRect((float)sx, 0.0f, (float)sw, (float)H, 1.2f);
                g.setColour(juce::Colour(LF::kAccent).withAlpha(0.08f));
                g.fillRect(sx, 0, sw, H);
            }
            else
            {
                // Insert strip border
                g.setColour(juce::Colour(LF::kPanelRim));
                g.drawRect(sx, 0, sw, H, 1);
            }

            // Colour tab (5px high, full width, at top)
            g.setColour(col);
            g.fillRect(sx + 1, 1, sw - 2, 5);
            g.setColour(col.withAlpha(0.5f));
            g.fillRect(sx + 1, 5, sw - 2, 3);

            // MX01 label (7px mono, below tab)
            if (!isMaster)
            {
                g.setFont(LF::monoFont(7.0f, juce::Font::bold));
                g.setColour(juce::Colour(LF::kTextFaint));
                g.drawText("MX" + juce::String(t + 1).paddedLeft('0', 2),
                           sx + 2, 10, sw - 4, 10, juce::Justification::centred);
            }
            else
            {
                g.setFont(LF::monoFont(7.0f, juce::Font::bold));
                g.setColour(juce::Colour(LF::kAccent));
                g.drawText("MASTER", sx + 2, 10, sw - 4, 10, juce::Justification::centred);
            }

            // 22-segment channel meter
            {
                static constexpr int kSeg = 22;
                const float lvl  = meterLevel[t];
                // Meter area: right 8px of strip, leaving room for fader
                const int mW   = 7;
                const int mX   = sx + sw - mW - 2;
                const int mTop = H - 14 - kSeg * 4;  // dB readout is 14px at bottom
                const int mH   = kSeg * 4;

                g.setColour(juce::Colour(LF::kDisplayBg));
                g.fillRect(mX, mTop, mW, mH);
                g.setColour(juce::Colour(0x80000000));
                g.drawRect((float)mX, (float)mTop, (float)mW, (float)mH, 0.8f);

                for (int seg = 0; seg < kSeg; ++seg)
                {
                    const float t01 = (float)seg / (kSeg - 1.0f);
                    const bool  act = t01 < lvl;
                    juce::Colour sc = t01 < 0.70f
                        ? juce::Colour(LF::kLedGreen)
                        : (t01 < 0.88f ? juce::Colour(LF::kLedAmber)
                                       : juce::Colour(LF::kLedRed));
                    g.setColour(act ? sc : juce::Colour(0x40000000));
                    const int sy2 = mTop + mH - (seg + 1) * 4 + 1;
                    g.fillRect(mX + 1, sy2, mW - 2, 3);
                }
            }

            // dB readout (bottom 14px, VT323 display style)
            {
                const float fvol = (float)faders[t]->getValue();
                juce::String dbStr;
                if (t < numTracks && muteBtns[t] && muteBtns[t]->getToggleState())
                    dbStr = "-INF";
                else if (fvol < 0.01f)
                    dbStr = "-60";
                else
                    dbStr = juce::String(20.0f * std::log10(fvol), 1);

                const int rx = sx + 1;
                const int rw = sw - 2;
                g.setColour(juce::Colour(LF::kDisplayBg));
                g.fillRect(rx, H - 14, rw, 13);
                g.setColour(juce::Colour(0x80000000));
                g.drawRect((float)rx, (float)(H - 14), (float)rw, 13.0f, 0.7f);
                g.setFont(LF::displayFont(isMaster ? 13.0f : 11.0f));
                g.setColour(juce::Colour(LF::kDisplayFg));
                g.drawText(dbStr, rx, H - 14, rw, 13, juce::Justification::centred);
            }
        }

        // Separator before master
        const int masterX = numTracks * stripW;
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine((float)masterX - 0.5f, 0.0f, (float)masterX - 0.5f, (float)H, 1.5f);

        // CMP / DLY / RVB active indicators (painted on top of button positions)
        for (int t = 0; t < numTracks; ++t)
        {
            if (cmpBtns[t])
            {
                auto r = cmpBtns[t]->getBounds();
                if (fxActive[t][0])
                {
                    g.setColour(juce::Colour(LF::kLedGreen).withAlpha(0.25f));
                    g.fillRect(r);
                }
            }
            if (dlyBtns[t])
            {
                auto r = dlyBtns[t]->getBounds();
                if (fxActive[t][1])
                {
                    g.setColour(juce::Colour(LF::kLedGreen).withAlpha(0.25f));
                    g.fillRect(r);
                }
            }
            if (rvbBtns[t])
            {
                auto r = rvbBtns[t]->getBounds();
                if (fxActive[t][2])
                {
                    g.setColour(juce::Colour(LF::kLedGreen).withAlpha(0.25f));
                    g.fillRect(r);
                }
            }
        }
    }

    void resized() override
    {
        for (int t = 0; t <= numTracks; ++t)
        {
            const int x = (t < numTracks) ? t * stripW
                                           : numTracks * stripW;
            const int w = (t < numTracks) ? stripW : masterW;
            layoutStrip(t, { x, 0, w, getHeight() });
        }
    }

private:
    // Per-strip controls (index 0..7 = inserts, 8 = master)
    std::unique_ptr<juce::Label>      labels    [numTracks + 1];
    std::unique_ptr<juce::Slider>     faders    [numTracks + 1];
    std::unique_ptr<juce::Slider>     pans      [numTracks + 1];
    std::unique_ptr<juce::TextButton> muteBtns  [numTracks + 1];
    std::unique_ptr<juce::TextButton> soloBtns  [numTracks + 1];
    std::unique_ptr<juce::TextButton> fxBtns    [numTracks];     // M14 FX open button
    std::unique_ptr<juce::TextButton> atBtns    [numTracks];     // Auto-Tune button
    std::unique_ptr<juce::TextButton> eqBtns    [numTracks + 1]; // DynEQ (inserts + master)
    std::unique_ptr<juce::TextButton> cmpBtns   [numTracks];     // Phase-5 CMP toggle
    std::unique_ptr<juce::TextButton> dlyBtns   [numTracks];     // Phase-5 DLY toggle
    std::unique_ptr<juce::TextButton> rvbBtns   [numTracks];     // Phase-5 RVB toggle
    std::unique_ptr<juce::Label>      routeLabel[numTracks];     // "ch: 0,8" info

    void buildStrip(int t)
    {
        const bool isMaster = (t == numTracks);

        labels[t] = std::make_unique<juce::Label>();
        labels[t]->setText(isMaster ? "Master" : ("T" + juce::String(t + 1)),
                           juce::dontSendNotification);
        labels[t]->setColour(juce::Label::textColourId,
                              isMaster ? juce::Colour(0xffb0b0b8) : juce::Colour(0xfff0f0f2));
        labels[t]->setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        labels[t]->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*labels[t]);

        faders[t] = std::make_unique<juce::Slider>(juce::Slider::LinearVertical,
                                                    juce::Slider::NoTextBox);
        faders[t]->setRange(0.0, 1.5, 0.01);
        faders[t]->setValue(1.0, juce::dontSendNotification);
        faders[t]->setColour(juce::Slider::thumbColourId,
                              isMaster ? juce::Colour(0xffe0e0e8) : juce::Colour(0xffb0b0b8));
        faders[t]->setColour(juce::Slider::trackColourId,       juce::Colour(0xffb0b0b8));
        faders[t]->setColour(juce::Slider::backgroundColourId,  juce::Colour(0xff2c2c2e));
        faders[t]->onValueChange = [this, t, isMaster]
        {
            const float v = (float)faders[t]->getValue();
            if (isMaster) { if (onMasterVolumeChanged) onMasterVolumeChanged(v); }
            else          { if (onTrackVolumeChanged)  onTrackVolumeChanged(t, v); }
        };
        addAndMakeVisible(*faders[t]);

        pans[t] = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                  juce::Slider::NoTextBox);
        pans[t]->setRange(-1.0, 1.0, 0.01);
        pans[t]->setValue(0.0, juce::dontSendNotification);
        pans[t]->setColour(juce::Slider::thumbColourId,       juce::Colour(0xffe67e22));
        pans[t]->setColour(juce::Slider::trackColourId,       juce::Colour(0xffe67e22));
        pans[t]->setColour(juce::Slider::backgroundColourId,  juce::Colour(0xff2c2c2e));
        pans[t]->onValueChange = [this, t, isMaster]
        {
            const float p = (float)pans[t]->getValue();
            if (isMaster) { if (onMasterPanChanged)  onMasterPanChanged(p); }
            else          { if (onTrackPanChanged)   onTrackPanChanged(t, p); }
        };
        addAndMakeVisible(*pans[t]);

        if (!isMaster)
        {
            muteBtns[t] = std::make_unique<juce::TextButton>("M");
            muteBtns[t]->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3c));
            muteBtns[t]->setClickingTogglesState(true);
            muteBtns[t]->onClick = [this, t]
            {
                const bool m = muteBtns[t]->getToggleState();
                syncMuteColour(t);
                if (onTrackMuteChanged) onTrackMuteChanged(t, m);
            };
            addAndMakeVisible(*muteBtns[t]);

            soloBtns[t] = std::make_unique<juce::TextButton>("S");
            soloBtns[t]->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3c));
            soloBtns[t]->setClickingTogglesState(true);
            soloBtns[t]->onClick = [this, t]
            {
                const bool s = soloBtns[t]->getToggleState();
                syncSoloColour(t);
                if (onTrackSoloChanged) onTrackSoloChanged(t, s);
            };
            addAndMakeVisible(*soloBtns[t]);

            // M14 — FX button
            fxBtns[t] = std::make_unique<juce::TextButton>("FX");
            fxBtns[t]->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3c));
            fxBtns[t]->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            fxBtns[t]->onClick = [this, t]
            {
                if (onFXButtonClicked) onFXButtonClicked(t);
            };
            addAndMakeVisible(*fxBtns[t]);

            // Auto-Tune button
            atBtns[t] = std::make_unique<juce::TextButton>("AT");
            atBtns[t]->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a2a4a));
            atBtns[t]->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffbb88ff));
            atBtns[t]->onClick = [this, t] { if (onAutoTuneButtonClicked) onAutoTuneButtonClicked(t); };
            addAndMakeVisible(*atBtns[t]);

            // CMP / DLY / RVB toggle buttons (Phase-5)
            auto makeFxToggle = [this](std::unique_ptr<juce::TextButton>& btn,
                                       const juce::String& lbl, int trk, int slot)
            {
                btn = std::make_unique<juce::TextButton>(lbl);
                btn->setClickingTogglesState(true);
                btn->setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff252527));
                btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1a3a1a));
                btn->setColour(juce::TextButton::textColourOffId,  juce::Colour(0xff888898));
                btn->setColour(juce::TextButton::textColourOnId,   juce::Colour(0xffb9ff66));
                btn->onClick = [this, trk, slot, &btn2 = btn]
                {
                    fxActive[trk][slot] = btn2->getToggleState();
                    if (onFXSlotToggled) onFXSlotToggled(trk, slot, fxActive[trk][slot]);
                    repaint();
                };
                addAndMakeVisible(*btn);
            };
            makeFxToggle(cmpBtns[t], "CMP", t, 0);
            makeFxToggle(dlyBtns[t], "DLY", t, 1);
            makeFxToggle(rvbBtns[t], "RVB", t, 2);

            // Dynamic EQ button
            eqBtns[t] = std::make_unique<juce::TextButton>("EQ");
            eqBtns[t]->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a4a));
            eqBtns[t]->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff8888ff));
            eqBtns[t]->onClick = [this, t] { if (onEQButtonClicked) onEQButtonClicked(t); };
            addAndMakeVisible(*eqBtns[t]);

            routeLabel[t] = std::make_unique<juce::Label>();
            routeLabel[t]->setFont(juce::Font(juce::FontOptions().withHeight(8.5f)));
            routeLabel[t]->setColour(juce::Label::textColourId, juce::Colour(0xff7f8c8d));
            routeLabel[t]->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*routeLabel[t]);
        }
        else
        {
            // Master strip EQ button (index = numTracks)
            eqBtns[t] = std::make_unique<juce::TextButton>("EQ");
            eqBtns[t]->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a4a));
            eqBtns[t]->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff8888ff));
            eqBtns[t]->onClick = [this, t] { if (onEQButtonClicked) onEQButtonClicked(t); };
            addAndMakeVisible(*eqBtns[t]);
        }
    }

    void layoutStrip(int t, juce::Rectangle<int> area)
    {
        const bool isMaster = (t == numTracks);
        const int  H        = area.getHeight();
        area = area.reduced(2, 1);

        // Top: name label (below painted color-tab + MX label, ~20px)
        area.removeFromTop(20);
        labels[t]->setBounds(area.removeFromTop(13).reduced(1, 0));

        // Source routing tag
        if (!isMaster && routeLabel[t])
            routeLabel[t]->setBounds(area.removeFromTop(10).reduced(1, 0));

        // PAN
        pans[t]->setBounds(area.removeFromTop(12).reduced(1, 0));

        // CMP / DLY / RVB row
        if (!isMaster && cmpBtns[t] && dlyBtns[t] && rvbBtns[t])
        {
            auto row = area.removeFromTop(14).reduced(1, 0);
            const int bw = row.getWidth() / 3;
            cmpBtns[t]->setBounds(row.removeFromLeft(bw).reduced(0, 0));
            dlyBtns[t]->setBounds(row.removeFromLeft(bw).reduced(0, 0));
            rvbBtns[t]->setBounds(row.reduced(0, 0));
        }

        // DYN EQ button
        if (eqBtns[t])
            eqBtns[t]->setBounds(area.removeFromTop(14).reduced(1, 0));

        // M / S row
        if (!isMaster && muteBtns[t] && soloBtns[t])
        {
            auto row = area.removeFromTop(14).reduced(1, 0);
            muteBtns[t]->setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(0, 0));
            soloBtns[t]->setBounds(row.reduced(0, 0));
        }

        // Fader — leave bottom (14px dB readout + 22-seg meter) painted in paint()
        // meter height = kSeg*4 = 88, dB = 14 → 102px reserved at bottom
        const int bottomPainted = juce::jmin(102, H / 2);
        area.removeFromBottom(bottomPainted);
        // Fader: shrink width to leave 9px on right for painted meter
        auto faderArea = area;
        faderArea.setWidth(faderArea.getWidth() - 9);
        faders[t]->setBounds(faderArea.reduced(1, 0));

        // Hide unused old buttons outside visible area
        if (!isMaster && fxBtns[t]) fxBtns[t]->setBounds(0, 0, 0, 0);
        if (!isMaster && atBtns[t]) atBtns[t]->setBounds(0, 0, 0, 0);
    }

    void syncMuteColour(int t)
    {
        if (!muteBtns[t]) return;
        muteBtns[t]->setColour(juce::TextButton::buttonColourId,
            muteBtns[t]->getToggleState() ? juce::Colour(0xff5a1414) : juce::Colour(0xff3a3a3c));
    }
    void syncSoloColour(int t)
    {
        if (!soloBtns[t]) return;
        soloBtns[t]->setColour(juce::TextButton::buttonColourId,
            soloBtns[t]->getToggleState() ? juce::Colour(0xff6b5000) : juce::Colour(0xff3a3a3c));
    }

public:
    // Update channel routing labels (called from MainComponent after routing changes).
    // Accepts a plain C array of 16 ints (Pattern::channelMixerRouting).
    void updateRoutingLabels(const int* routing)
    {
        if (routing == nullptr) return;
        for (int t = 0; t < numTracks; ++t)
        {
            if (!routeLabel[t]) continue;
            juce::String s;
            for (int ch = 0; ch < 16; ++ch)
                if (routing[ch] == t) s += (s.isEmpty() ? "" : ",") + juce::String(ch + 1);
            routeLabel[t]->setText(s.isEmpty() ? "-" : ("ch:" + s),
                                   juce::dontSendNotification);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerComponent)
};
