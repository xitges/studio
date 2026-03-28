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
                             public juce::DragAndDropTarget,
                             public juce::Timer
{
public:
    ChannelRackComponent();
    ~ChannelRackComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp  (const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;   // M1.5 rename
    void timerCallback() override;

    // OS file drag & drop
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void fileDragMove(const juce::StringArray&, int, int y) override;

    // M15 — internal drag from SampleBrowserComponent
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details) override;

    // Callbacks → MainComponent → AudioEngine
    std::function<void(int ch, bool muted)>   onMuteChanged;
    std::function<void(int ch, bool soloed)>  onSoloChanged;
    std::function<void(int ch, float volume)> onVolumeChanged;  // M1.1
    std::function<void(int ch, float pan)>    onPanChanged;     // M1.1
    std::function<void(int ch, float pitch)>  onPitchChanged;   // M1.2
    std::function<void(int ch, juce::File)>   onSampleDropped;
    std::function<void(int ch, int step, bool newState, bool oldState)> onStepToggled; // M6
    std::function<void()>                     onStepDragBegin;  // mouseDown on step → start undo txn
    std::function<void()>                     onStepDragEnd;    // mouseUp  on step → close undo txn
    std::function<void()>                     onClearAllSteps;
    std::function<void(int ch)>               onOpenPianoRoll;      // M3
    std::function<void(int newStepCount)>     onStepCountChanged;   // M3
    std::function<void(float swing)>          onSwingChanged;        // Groove swing
    std::function<void(int ch, ChannelType)>  onChannelTypeChanged; // M3
    std::function<void(int ch)>               onOpenSynthEditor;    // M13
    std::function<void(int ch)>               onDeleteChannel;      // delete channel
    std::function<void(const juce::String&)>  onAddChannel;         // add new channel
    std::function<void(int stepZeroBased)>    onPatternStartStepChanged;
    std::function<int()>                      getCurrentStep;

    // M8 — VST/AU plugin actions
    std::function<void(int ch)> onLoadPlugin;
    std::function<void(int ch)> onOpenPluginEditor;
    std::function<void(int ch)> onRemovePlugin;

    // M-Phase3 — mixer routing
    std::function<void(int ch, int trackIdx)> onChannelRoutingChanged;

    // Pattern Variation (A/B/C/D)
    int activeVariation = 0;  // 0=A 1=B 2=C 3=D
    std::function<void(int, int)> onVariationChanged;  // fires when user clicks A/B/C/D tab (prevIdx, newIdx)

    void setChannelHasPlugin(int ch, bool has)
    {
        if (ch >= 0 && ch < 16) { channelHasPlugin[(size_t)ch] = has; repaint(); }
    }

    bool getChannelHasPlugin(int ch) const
    {
        return (ch >= 0 && ch < 16) && channelHasPlugin[(size_t)ch];
    }

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

    // Set a single step in the UI without reloading the whole pattern
    void setStep(int channel, int step, bool val)
    {
        if (channel >= 0 && channel < (int)channels.size()
            && step >= 0 && step < stepCount)
        {
            channels[(size_t)channel].steps[(size_t)step] = val;
            repaint();
        }
    }

    int getStepCount() const { return stepCount; }
    void setStepCountFromExternal(int newCount)
    {
        stepCountSlider.setValue(juce::jlimit(1, Pattern::kMaxSteps, newCount), juce::sendNotification);
    }
    void setPatternStartStep(int stepZeroBased)
    {
        patternStartStep = juce::jlimit(0, juce::jmax(0, stepCount - 1), stepZeroBased);
        repaint();
    }
    int getPatternStartStep() const { return patternStartStep; }

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
        return HEADER_HEIGHT + (int)channels.size() * ROW_HEIGHT + 50 + INSPECTOR_HEIGHT;
    }

    // Step inspector — call from MainComponent to push model state to inspector
    void setStepParams(int ch, int step, const StepParams& p)
    {
        if (ch < 0 || ch >= Pattern::kMaxChannels || step < 0 || step >= Pattern::kMaxSteps) return;
        stepParamsStore[ch][step] = p;
        if (inspectorCh == ch && inspectorStep == step) refreshInspector();
        repaint();   // badge update
    }

    const StepParams& getStepParams(int ch, int step) const
    {
        return stepParamsStore[ch][step];
    }

    // Load all step params from a pattern (call alongside loadPattern)
    void loadStepParams(const Pattern& pat, int varIdx = -1)
    {
        const int vi = (varIdx >= 0) ? varIdx : activeVariation;
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            for (int s = 0; s < Pattern::kMaxSteps; ++s)
                stepParamsStore[ch][s] = pat.variations[vi].stepParams[ch][s];
        if (inspectorCh >= 0 && inspectorStep >= 0) refreshInspector();
        repaint();
    }

    // Save all step params back into a pattern variation
    void saveStepParams(Pattern& pat, int varIdx = -1) const
    {
        const int vi = (varIdx >= 0) ? varIdx : activeVariation;
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            for (int s = 0; s < Pattern::kMaxSteps; ++s)
                pat.variations[vi].stepParams[ch][s] = stepParamsStore[ch][s];

    }

    // Set swing slider from pattern data
    void setSwingAmount(float swing)
    {
        swingSlider.setValue((double)swing, juce::dontSendNotification);
    }

    // Callback fired when user edits step params in the inspector
    std::function<void(int ch, int step, const StepParams&)> onStepParamsChanged;

    // M2.1 — Pattern load / save (-1 = use activeVariation)
    void loadPattern(const Pattern& pat, int varIdx = -1);
    void saveToPattern(Pattern& pat, int varIdx = -1) const;

    // Channel count / name management
    int          getChannelCount() const { return (int)channels.size(); }
    juce::String getChannelName(int ch) const
    {
        return (ch >= 0 && ch < (int)channels.size()) ? channels[(size_t)ch].name : "";
    }
    void resetToChannelCount(int count, const juce::String* names);

    static constexpr int ROW_HEIGHT      = 58;
    static constexpr int LABEL_WIDTH     = 250;
    static constexpr int HEADER_HEIGHT   = 30;
    static constexpr int INSPECTOR_HEIGHT = 140; // height of step inspector strip (4 rows)

private:
    std::vector<ChannelRow> channels;
    std::array<ChannelType, 16> channelTypes    = {};   // M3: mirrors project.channelTypes
    bool                        channelHasPlugin[16] = {}; // M8
    int                         channelRouting[16]   = {}; // mirrors pattern.channelMixerRouting
    juce::TextButton addChannelBtn { "+ Add Channel" };
    juce::TextButton clearStepsBtn { "Clear Steps" };
    juce::Slider     stepCountSlider;
    juce::Slider     swingSlider;        // Groove swing 0..1
    juce::Label      swingLabel;

    // Variation A/B/C/D buttons
    juce::TextButton varBtnA { "A" }, varBtnB { "B" }, varBtnC { "C" }, varBtnD { "D" };
    void updateVariationButtonStates();

    int dragHoverChannel = -1;
    int currentPlayStep  = -1;
    int stepCount        = 16;
    int patternStartStep = 0;

    // Drag-to-paint state — reset in mouseUp
    int  dragPaintChannel_ = -1;   // channel row locked at mouseDown; -1 = not painting
    bool dragPaintState_   = false; // ON or OFF — determined by the initial click
    int  lastDragStep_     = -1;   // last step index processed (dedup)

    // --- Per-step params store (UI-side mirror of project model) ---
    StepParams stepParamsStore[Pattern::kMaxChannels][Pattern::kMaxSteps] {};

    // --- Step Inspector ---
    int inspectorCh   = -1;   // currently inspected channel (-1 = none)
    int inspectorStep = -1;   // currently inspected step    (-1 = none)

    // Inspector controls (shown when a step is selected via right-click)
    juce::Label  inspectorLabel;
    juce::Slider inspVelSlider;        // velocity multiplier 0..2
    juce::Slider inspGateSlider;       // gate multiplier 0..2
    juce::Slider inspProbSlider;       // probability 0..1
    juce::Slider inspPitchSlider;      // pitch offset -12..+12 st
    juce::Slider inspCutoffSlider;     // cutoff mod -3..+3 octaves (Phase 2)
    juce::Slider inspStartOffSlider;   // sample start offset 0..1 (Phase 2)
    juce::Slider inspTimingSlider;     // timing offset -0.5..+0.5 (Groove)
    juce::TextButton inspResetBtn { "Reset" };

    void openInspector(int ch, int step);   // select a step for inspection
    void closeInspector();
    void refreshInspector();                // sync inspector controls from stepParamsStore
    void drawInspector(juce::Graphics& g);  // paint the inspector background/labels
    void layoutInspector();                 // position inspector controls

    void addChannel(const juce::String& name);
    void drawStepGrid(juce::Graphics& g);
    void drawChannelLabels(juce::Graphics& g);
    void drawHeader(juce::Graphics& g);
    int  getChannelIndexAt(int y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelRackComponent)
};
