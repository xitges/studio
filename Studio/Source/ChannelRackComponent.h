#pragma once
#include <JuceHeader.h>
#include <functional>
#include "ProjectModel.h"

struct ChannelRow
{
    juce::String name;
    std::array<bool, Pattern::kMaxSteps> steps {};
    bool muted  = false;
    bool soloed = false;
    juce::String sampleName = "Drop Sample";

    // M1.1 — Volume & Pan
    float volume = 0.8f;
    float pan    = 0.0f;

    // M1.2 — Pitch (semitones)
    float pitch  = 0.0f;

    std::unique_ptr<juce::TextButton> muteBtn;
    std::unique_ptr<juce::TextButton> soloBtn;

    // M1.1 sliders
    std::unique_ptr<juce::Slider> volSlider;
    std::unique_ptr<juce::Slider> panSlider;

    // M1.2 slider
    std::unique_ptr<juce::Slider> pitchSlider;
};


class ChannelRackComponent : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             public juce::Timer
{
public:
    ChannelRackComponent();
    ~ChannelRackComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;   // M1.5 rename
    void timerCallback() override;

    // Drag & drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void fileDragMove(const juce::StringArray&, int, int y) override;

    // Callbacks → MainComponent → AudioEngine
    std::function<void(int ch, bool muted)>   onMuteChanged;
    std::function<void(int ch, bool soloed)>  onSoloChanged;
    std::function<void(int ch, float volume)> onVolumeChanged;  // M1.1
    std::function<void(int ch, float pan)>    onPanChanged;     // M1.1
    std::function<void(int ch, float pitch)>  onPitchChanged;   // M1.2
    std::function<void(int ch, juce::File)>   onSampleDropped;
    std::function<void(int ch, int step, bool newState, bool oldState)> onStepToggled; // M6
    std::function<void(int ch)>               onOpenPianoRoll;      // M3
    std::function<void(int ch, ChannelType)>  onChannelTypeChanged; // M3
    std::function<void(int ch)>               onOpenSynthEditor;    // M13
    std::function<int()>                      getCurrentStep;

    // M3 — reflect channel types from project
    void setChannelType(int ch, ChannelType t)
    {
        if (ch >= 0 && ch < (int)channelTypes.size())
        {
            channelTypes[(size_t)ch] = t;
            repaint();
        }
    }

    bool getStep(int channel, int step) const
    {
        if (channel < (int)channels.size() && step >= 0 && step < stepCount)
            return channels[(size_t)channel].steps[(size_t)step];
        return false;
    }

    int getStepCount() const { return stepCount; }

    void setPlaybackStep(int step)
    {
        if (currentPlayStep != step)
        {
            currentPlayStep = step;
            repaint();
        }
    }

    // M1.4 — expose needed height so Viewport can be sized correctly
    int getNeededHeight() const
    {
        return HEADER_HEIGHT + (int)channels.size() * ROW_HEIGHT + 50;
    }

    // M2.1 — Pattern load / save
    void loadPattern(const Pattern& pat);
    void saveToPattern(Pattern& pat) const;

    // Channel count / name management
    int          getChannelCount() const { return (int)channels.size(); }
    juce::String getChannelName(int ch) const
    {
        return (ch >= 0 && ch < (int)channels.size()) ? channels[(size_t)ch].name : "";
    }
    void resetToChannelCount(int count, const juce::String* names);

    static constexpr int ROW_HEIGHT    = 58;
    static constexpr int LABEL_WIDTH   = 250;
    static constexpr int HEADER_HEIGHT = 30;

private:
    std::vector<ChannelRow> channels;
    std::array<ChannelType, 16> channelTypes = {};  // M3: mirrors project.channelTypes
    juce::TextButton addChannelBtn { "+ Add Channel" };
    juce::Slider     stepCountSlider;

    int dragHoverChannel = -1;
    int currentPlayStep  = -1;
    int stepCount        = 16;

    void addChannel(const juce::String& name);
    void drawStepGrid(juce::Graphics& g);
    void drawChannelLabels(juce::Graphics& g);
    void drawHeader(juce::Graphics& g);
    int  getChannelIndexAt(int y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelRackComponent)
};
