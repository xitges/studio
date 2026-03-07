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
    static constexpr int stripW     = 72;
    static constexpr int masterW    = 82;

    // Callbacks → MainComponent → AudioEngine
    std::function<void(int track, float vol)>   onTrackVolumeChanged;
    std::function<void(int track, float pan)>   onTrackPanChanged;
    std::function<void(int track, bool muted)>  onTrackMuteChanged;
    std::function<void(int track, bool soloed)> onTrackSoloChanged;
    std::function<void(float)>                  onMasterVolumeChanged;
    std::function<void(float)>                  onMasterPanChanged;

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
        }
        // Master (index numTracks)
        faders[numTracks]->setValue(proj.masterVolume, juce::dontSendNotification);
        pans[numTracks]  ->setValue(proj.masterPan,    juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff111120));

        // Separator before master
        const int masterX = numTracks * stripW;
        g.setColour(juce::Colour(0xff3498db).withAlpha(0.4f));
        g.drawLine((float)masterX, 0.0f, (float)masterX, (float)getHeight(), 2.0f);

        // Strip borders
        g.setColour(juce::Colour(0xff0f1422));
        for (int t = 0; t <= numTracks; ++t)
        {
            const int x = (t < numTracks) ? t * stripW : masterX;
            const int w = (t < numTracks) ? stripW     : masterW;
            g.drawRect(x, 0, w, getHeight(), 1);
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
    std::unique_ptr<juce::Label>      routeLabel[numTracks];     // "ch: 0,8" info

    void buildStrip(int t)
    {
        const bool isMaster = (t == numTracks);

        labels[t] = std::make_unique<juce::Label>();
        labels[t]->setText(isMaster ? "Master" : ("T" + juce::String(t + 1)),
                           juce::dontSendNotification);
        labels[t]->setColour(juce::Label::textColourId,
                              isMaster ? juce::Colour(0xff3498db) : juce::Colours::white);
        labels[t]->setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        labels[t]->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(*labels[t]);

        faders[t] = std::make_unique<juce::Slider>(juce::Slider::LinearVertical,
                                                    juce::Slider::NoTextBox);
        faders[t]->setRange(0.0, 1.5, 0.01);
        faders[t]->setValue(1.0, juce::dontSendNotification);
        faders[t]->setColour(juce::Slider::thumbColourId,
                              isMaster ? juce::Colour(0xff3498db) : juce::Colour(0xff2ecc71));
        faders[t]->setColour(juce::Slider::trackColourId,       juce::Colour(0xff2c3e50));
        faders[t]->setColour(juce::Slider::backgroundColourId,  juce::Colour(0xff1a1a2e));
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
        pans[t]->setColour(juce::Slider::backgroundColourId,  juce::Colour(0xff1a1a2e));
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
            muteBtns[t]->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2c2c54));
            muteBtns[t]->setClickingTogglesState(true);
            muteBtns[t]->onClick = [this, t]
            {
                const bool m = muteBtns[t]->getToggleState();
                syncMuteColour(t);
                if (onTrackMuteChanged) onTrackMuteChanged(t, m);
            };
            addAndMakeVisible(*muteBtns[t]);

            soloBtns[t] = std::make_unique<juce::TextButton>("S");
            soloBtns[t]->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2c2c54));
            soloBtns[t]->setClickingTogglesState(true);
            soloBtns[t]->onClick = [this, t]
            {
                const bool s = soloBtns[t]->getToggleState();
                syncSoloColour(t);
                if (onTrackSoloChanged) onTrackSoloChanged(t, s);
            };
            addAndMakeVisible(*soloBtns[t]);

            routeLabel[t] = std::make_unique<juce::Label>();
            routeLabel[t]->setFont(juce::Font(juce::FontOptions().withHeight(8.5f)));
            routeLabel[t]->setColour(juce::Label::textColourId, juce::Colour(0xff7f8c8d));
            routeLabel[t]->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*routeLabel[t]);
        }
    }

    void layoutStrip(int t, juce::Rectangle<int> area)
    {
        const bool isMaster = (t == numTracks);
        area = area.reduced(3, 2);

        labels[t]->setBounds(area.removeFromTop(16));
        if (!isMaster && routeLabel[t])
            routeLabel[t]->setBounds(area.removeFromBottom(14));
        const auto btnRow = area.removeFromBottom(22);
        if (!isMaster && muteBtns[t] && soloBtns[t])
        {
            auto br = btnRow;
            muteBtns[t]->setBounds(br.removeFromLeft(br.getWidth() / 2).reduced(1));
            soloBtns[t]->setBounds(br.reduced(1));
        }
        pans[t]->setBounds(area.removeFromBottom(14));
        faders[t]->setBounds(area);
    }

    void syncMuteColour(int t)
    {
        if (!muteBtns[t]) return;
        muteBtns[t]->setColour(juce::TextButton::buttonColourId,
            muteBtns[t]->getToggleState() ? juce::Colour(0xffe74c3c) : juce::Colour(0xff2c2c54));
    }
    void syncSoloColour(int t)
    {
        if (!soloBtns[t]) return;
        soloBtns[t]->setColour(juce::TextButton::buttonColourId,
            soloBtns[t]->getToggleState() ? juce::Colour(0xfff39c12) : juce::Colour(0xff2c2c54));
    }

public:
    // Update channel routing labels (called from MainComponent after routing changes)
    void updateRoutingLabels(const std::array<int, 16>& routing)
    {
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
