#pragma once
#include <JuceHeader.h>
#include "ProjectModel.h"
#include "ProjectSerializer.h"
#include "StudioLookAndFeel.h"
#include "ToolbarComponent.h"
#include "ChannelRackComponent.h"
#include "Audio/AudioEngine.h"
#include "PlaylistComponent.h"
#include "PianoRollComponent.h"
#include "MixerComponent.h"
#include "SynthEditorComponent.h"
#include "FXEditorComponent.h"
#include "AutoTuneEditorComponent.h"
#include "LaunchpadComponent.h"
#include "SampleBrowserComponent.h"
#include "PluginBrowserComponent.h"
#include "DynamicEQComponent.h"
#include "TrackpadController.h"
#include "LivePerformance/ClipLauncher.h"
#include "LivePerformance/LivePerformanceComponent.h"
#include "LivePerformance/LiveLoopWindow.h"

// Tab bar: INSTRUMENT / SEQUENCER / MIXER  (Phase-6 redesign)
class InspectorTabBar : public juce::Component
{
public:
    std::function<void(int)> onTabChanged;

    void setTab(int t) { activeTab_ = t; repaint(); }
    int  getTab() const { return activeTab_; }

    // Dynamic sub-label setters — call from MainComponent when state changes
    void setInstrumentSub(int chIdx, const juce::String& name)
    {
        instrSub_ = "CH " + juce::String(chIdx + 1).paddedLeft('0', 2)
                  + juce::String::fromUTF8("  \xe2\x80\x94  ") + name.toUpperCase();
        gutterSub_ = instrSub_;
        repaint();
    }
    void setSequencerSub(const juce::String& patternName)
    {
        seqSub_ = patternName.toUpperCase();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        using LF = StudioLookAndFeel;
        const int W = getWidth();
        const int H = getHeight();

        // Chassis gradient background
        juce::ColourGradient bg(juce::Colour(LF::kChassis2), 0.0f, 0.0f,
                                juce::Colour(LF::kChassis),  0.0f, (float)H, false);
        g.setGradientFill(bg);
        g.fillAll();

        // Top + bottom hairlines
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine(0.0f, 0.5f, (float)W, 0.5f, 1.0f);
        g.drawLine(0.0f, (float)(H - 1), (float)W, (float)(H - 1), 1.0f);

        const int tw = W / 3;

        static const char* kLabels[] = { "INSTRUMENT", "SEQUENCER", "MIXER" };
        const juce::String kSubs[3] = {
            instrSub_,
            seqSub_,
            juce::String::fromUTF8("8 INSERT  \xc2\xb7  1 MASTER"),
        };

        for (int i = 0; i < 3; ++i)
        {
            const bool on = (i == activeTab_);
            juce::Rectangle<int> tab(i * tw, 0, (i == 2) ? W - i * tw : tw, H);

            if (on)
            {
                g.setColour(juce::Colour(LF::kPanel));
                g.fillRect(tab.reduced(1, 0));
                g.setColour(juce::Colour(LF::kPanelRim));
                g.drawLine((float)(tab.getX() + 1), 0.0f, (float)(tab.getX() + 1), (float)H, 0.8f);
                g.drawLine((float)(tab.getRight() - 1), 0.0f, (float)(tab.getRight() - 1), (float)H, 0.8f);
                g.setColour(juce::Colour(LF::kAccent));
                g.fillRect(tab.getX() + 2, 0, tab.getWidth() - 4, 2);
            }

            // Main label — bigger font, centred in tall tab
            g.setFont(LF::monoFont(11.0f, juce::Font::bold));
            g.setColour(on ? juce::Colour(LF::kText) : juce::Colour(LF::kTextDim));
            g.drawText(kLabels[i], tab.getX(), H / 2 - 18, tab.getWidth(), 18,
                       juce::Justification::centred);

            // Sub-label
            g.setFont(LF::monoFont(8.5f, juce::Font::bold));
            g.setColour(on ? juce::Colour(LF::kAccent) : juce::Colour(LF::kTextFaint));
            g.drawText(kSubs[i], tab.getX() + 2, H / 2 + 2, tab.getWidth() - 4, 14,
                       juce::Justification::centred, true);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int tw = getWidth() / 3;
        const int t  = juce::jlimit(0, 2, e.x / tw);
        if (t != activeTab_)
        {
            activeTab_ = t;
            repaint();
            if (onTabChanged) onTabChanged(activeTab_);
        }
    }

private:
    int          activeTab_ = 1;   // default: SEQUENCER
    juce::String instrSub_  = "CH 01";
    juce::String seqSub_    = "PATTERN";
    juce::String gutterSub_ = "CH 01";
};

// =========================================================================
// Step Inspector strip — right side of the inspector tab bar row
// =========================================================================
class StepInspectorStrip : public juce::Component
{
public:
    std::function<void(int ch, int step, const StepParams&)> onParamsChanged;

    StepInspectorStrip()
    {
        using LF = StudioLookAndFeel;
        auto setup = [&](juce::Slider& s, double lo, double hi, double step, juce::Colour col)
        {
            s.setRange(lo, hi, step);
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            s.setColour(juce::Slider::trackColourId,       col);
            s.setColour(juce::Slider::backgroundColourId,  juce::Colour(LF::kDark));
            s.setColour(juce::Slider::thumbColourId,       juce::Colour(LF::kPanel));
            addChildComponent(s);
        };
        setup(velSlider_,   0.0, 2.0,   0.01, juce::Colour(0xff3a9ad9));
        setup(gateSlider_,  0.0, 2.0,   0.01, juce::Colour(0xff27ae60));
        setup(probSlider_,  0.0, 1.0,   0.01, juce::Colour(0xffe6b32b));
        setup(pitchSlider_, -12.0, 12.0, 1.0, juce::Colour(StudioLookAndFeel::kAccent));
        setup(cutoffSlider_, -3.0, 3.0,  0.01, juce::Colour(0xffcc6699));
        setup(timingSlider_, -0.5, 0.5,  0.01, juce::Colour(0xff9977cc));

        velSlider_   .onValueChange = [this] { pushChange(); };
        gateSlider_  .onValueChange = [this] { pushChange(); };
        probSlider_  .onValueChange = [this] { pushChange(); };
        pitchSlider_ .onValueChange = [this] { pushChange(); };
        cutoffSlider_.onValueChange = [this] { pushChange(); };
        timingSlider_.onValueChange = [this] { pushChange(); };

        addChildComponent(resetBtn_);
        resetBtn_.setButtonText("RESET");
        resetBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
        resetBtn_.onClick = [this]
        {
            if (ch_ < 0) return;
            StepParams def;
            updateFromExternal(def);
            pushChange();
        };
    }

    void setStep(int ch, int step, const juce::String& chName, const StepParams& p)
    {
        ch_ = ch; step_ = step; chName_ = chName;
        currentParams_ = p;
        syncSliders();
        velSlider_   .setVisible(true);
        gateSlider_  .setVisible(true);
        probSlider_  .setVisible(true);
        pitchSlider_ .setVisible(true);
        cutoffSlider_.setVisible(true);
        timingSlider_.setVisible(true);
        resetBtn_    .setVisible(true);
        repaint();
    }

    void clearStep()
    {
        ch_ = step_ = -1; chName_ = {};
        velSlider_   .setVisible(false);
        gateSlider_  .setVisible(false);
        probSlider_  .setVisible(false);
        pitchSlider_ .setVisible(false);
        cutoffSlider_.setVisible(false);
        timingSlider_.setVisible(false);
        resetBtn_    .setVisible(false);
        repaint();
    }

    void updateFromExternal(const StepParams& p)
    {
        currentParams_ = p;
        syncSliders();
        repaint();
    }

    void resized() override
    {
        const int W = getWidth(), H = getHeight();
        if (W < 10 || H < 10) return;

        // Layout: header (22px) → row1 (26px) → row2 (26px)
        constexpr int kLblW  = 46;  // label column
        constexpr int kValW  = 38;  // value text column
        constexpr int kGap   = 8;
        const int     slH    = 14;
        const int     col3W  = (W - kGap*4) / 3;  // each of 3 columns
        const int     slW    = col3W - kLblW - kValW;

        auto colX = [&](int c) { return kGap + c * (col3W + kGap); };
        auto slX  = [&](int c) { return colX(c) + kLblW; };

        const int row1Y = 22 + (26 - slH) / 2;
        const int row2Y = 22 + 26 + (26 - slH) / 2;

        velSlider_   .setBounds(slX(0), row1Y, slW, slH);
        gateSlider_  .setBounds(slX(1), row1Y, slW, slH);
        probSlider_  .setBounds(slX(2), row1Y, slW, slH);
        pitchSlider_ .setBounds(slX(0), row2Y, slW, slH);
        cutoffSlider_.setBounds(slX(1), row2Y, slW, slH);
        timingSlider_.setBounds(slX(2), row2Y, slW, slH);

        resetBtn_.setBounds(W - 60, 2, 56, 18);
    }

    void paint(juce::Graphics& g) override
    {
        using LF = StudioLookAndFeel;
        const int W = getWidth(), H = getHeight();

        // Same chassis gradient as the tab bar beside it
        juce::ColourGradient bg(juce::Colour(LF::kChassis2), 0.0f, 0.0f,
                                juce::Colour(LF::kChassis),  0.0f, (float)H, false);
        g.setGradientFill(bg);
        g.fillAll();

        // Top + bottom hairlines (matching InspectorTabBar)
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine(0.0f, 0.5f, (float)W, 0.5f, 1.0f);
        g.drawLine(0.0f, (float)(H - 1), (float)W, (float)(H - 1), 1.0f);

        // Left separator
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine(1.0f, 4.0f, 1.0f, (float)(H - 4), 0.8f);

        // Header
        g.setFont(LF::monoFont(9.5f, juce::Font::bold));
        g.setColour(juce::Colour(LF::kTextFaint));
        g.drawText("STEP INSPECTOR", 8, 0, 130, 22, juce::Justification::centredLeft);

        if (ch_ < 0)
        {
            g.setFont(LF::monoFont(9.0f));
            g.setColour(juce::Colour(LF::kTextFaint).withAlpha(0.45f));
            g.drawText(juce::String::fromUTF8("— select a step"), 140, 0, W - 200, 22,
                       juce::Justification::centredLeft);
            return;
        }

        // Step + channel badge
        const juce::String info = "S" + juce::String(step_ + 1).paddedLeft('0', 2)
                                + juce::String::fromUTF8("  \xe2\x80\x94  ") + chName_.toUpperCase();
        g.setFont(LF::monoFont(10.0f, juce::Font::bold));
        g.setColour(juce::Colour(LF::kAccent));
        g.drawText(info, 140, 0, W - 210, 22, juce::Justification::centredLeft);

        // Param labels + values in 3-column grid
        struct ParamInfo { const char* label; juce::uint32 col; juce::Slider* slider; bool isPitch; };
        ParamInfo rows[2][3] = {
            { {"VEL",   0xff3a9ad9, &velSlider_,    false},
              {"GATE",  0xff27ae60, &gateSlider_,   false},
              {"PROB",  0xffe6b32b, &probSlider_,   false} },
            { {"PITCH", LF::kAccent, &pitchSlider_, true },
              {"CUT",   0xffcc6699, &cutoffSlider_, false},
              {"TIMING",0xff9977cc, &timingSlider_, false} }
        };

        constexpr int kLblW = 46;
        constexpr int kGap  = 8;
        const int col3W = (W - kGap*4) / 3;

        for (int row = 0; row < 2; ++row)
        {
            const int rowY = 22 + row * 26;
            for (int col = 0; col < 3; ++col)
            {
                const auto& p  = rows[row][col];
                const int   cx = kGap + col * (col3W + kGap);

                // Colored label
                g.setFont(LF::monoFont(8.0f, juce::Font::bold));
                g.setColour(juce::Colour(p.col).withAlpha(0.9f));
                g.drawText(p.label, cx, rowY + 4, kLblW - 4, 18,
                           juce::Justification::centredRight);

                // Value text (right of slider)
                const double v = p.slider->getValue();
                juce::String val;
                if (p.isPitch)
                    val = (v >= 0 ? "+" : "") + juce::String((int)v) + "st";
                else
                    val = juce::String(v, 2);

                const int slW  = col3W - kLblW - 38;
                const int slRight = cx + kLblW + slW;
                g.setFont(LF::monoFont(8.0f, juce::Font::bold));
                g.setColour(juce::Colour(LF::kTextDim));
                g.drawText(val, slRight + 2, rowY + 4, 36, 18,
                           juce::Justification::centredLeft);
            }
        }
    }

private:
    juce::Slider     velSlider_, gateSlider_, probSlider_;
    juce::Slider     pitchSlider_, cutoffSlider_, timingSlider_;
    juce::TextButton resetBtn_;
    int              ch_ = -1, step_ = -1;
    juce::String     chName_;
    StepParams       currentParams_;

    void syncSliders()
    {
        velSlider_   .setValue(currentParams_.velocity,             juce::dontSendNotification);
        gateSlider_  .setValue(currentParams_.gate,                 juce::dontSendNotification);
        probSlider_  .setValue(currentParams_.probability,           juce::dontSendNotification);
        pitchSlider_ .setValue((double)currentParams_.pitchOffset,  juce::dontSendNotification);
        cutoffSlider_.setValue(currentParams_.cutoffMod,            juce::dontSendNotification);
        timingSlider_.setValue(currentParams_.timingOffset,         juce::dontSendNotification);
    }

    void pushChange()
    {
        if (ch_ < 0 || step_ < 0) return;
        currentParams_.velocity      = (float)velSlider_.getValue();
        currentParams_.gate          = (float)gateSlider_.getValue();
        currentParams_.probability   = (float)probSlider_.getValue();
        currentParams_.pitchOffset   = (int)std::round(pitchSlider_.getValue());
        currentParams_.cutoffMod     = (float)cutoffSlider_.getValue();
        currentParams_.timingOffset  = (float)timingSlider_.getValue();
        if (onParamsChanged) onParamsChanged(ch_, step_, currentParams_);
        repaint();  // refresh value text
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepInspectorStrip)
};

// =========================================================================

class MainComponent : public juce::Component,
                      public juce::Timer,
                      public juce::DragAndDropContainer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    StudioLookAndFeel    lookAndFeel;    // M11 — must be first so it outlives all components

    Project              project;
    AudioEngine          audioEngine;
    ToolbarComponent     toolbar;
    PlaylistComponent    playlist;
    juce::Viewport       playlistViewport;
    juce::ComboBox       playlistSnapBox;
    juce::TextButton     playlistZoomInBtn  { "+" };
    juce::TextButton     playlistZoomOutBtn { "-" };
    ChannelRackComponent channelRack;
    juce::Viewport       channelRackViewport;
    MixerComponent       mixer;

    // M15 — sample browser panel
    SampleBrowserComponent sampleBrowser;
    juce::Viewport         browserViewport;
    juce::TextButton       browserCollapseBtn;   // always-visible collapse/expand tab
    bool isBrowserOpen = true;                   // default: open on launch

    // M3 — floating piano roll window
    std::unique_ptr<PianoRollWindow> pianoRollWindow;
    int  pianoRollChannel = -1;
    bool showMixer = false;
    bool pianoRollPlaybackOverridesPlayMode = false;
    PlayMode playModeBeforePianoRollPlayback = PlayMode::Pattern;

    // M13/M14 — floating synth + FX editors
    std::unique_ptr<SynthEditorWindow> synthEditorWindow;
    std::unique_ptr<FXEditorWindow>    fxEditorWindow;
    std::unique_ptr<AutoTuneEditorWindow> autoTuneEditorWindow;
    int autoTuneEditorTrack = -1;
    int synthEditorChannel = -1;
    int fxEditorTrack      = -1;

    // Inspector tab bar (INSTRUMENT=0 / SEQUENCER=1 / MIXER=2)
    InspectorTabBar      inspectorTabBar_;
    StepInspectorStrip   stepInspector_;
    int              inspectorTab_ = 1;   // default: SEQUENCER (channel rack)
    juce::Rectangle<int> rightPanelBounds_;        // cached in resized(), used in paint()
    juce::Rectangle<int> inspectorContentBounds_;  // area below tab bar (inspector content)

    // Launchpad — inline right panel (toggled)
    LaunchpadPanel   launchpadPanel;
    bool             showLaunchpad = false;

    std::unique_ptr<juce::DocumentWindow> audioDeviceWindow;

    // M8 — plugin browser + per-channel plugin editor windows
    std::unique_ptr<PluginBrowserWindow>                    pluginBrowserWindow;
    std::array<std::unique_ptr<PluginEditorWindow>, 16>     pluginEditorWindows;

    // Dynamic EQ windows (0-7 = mixer tracks, 8 = master)
    std::array<std::unique_ptr<DynamicEQWindow>, 9>         dynEQWindows;

    // Trackpad multitouch → launchpad pads 0-15
    TrackpadController trackpadController;

    // M2 — pattern management state
    int activePatternId = 1;

    // Pause/resume — saved bar position when Space-paused in Song mode (< 0 = no pause state)
    double pausedBarSong = -1.0;
    int patternStartStep = 0;
    int pianoRollStartStep = 0;

    // Double-Space detection: timestamp when Space last resumed playback
    juce::int64 lastSpaceResumeTime = 0;

    Pattern* findPattern(int id);
    int      nextPatternId() const;
    void     selectPattern(int id);
    void     syncPatternToEngine();
    void     syncChannelRackToProject();
    int      ensureAutoBassChannel();
    void     focusPianoRollChannel(int channel);
    void     switchToPatternModeForEditing();
    void     followPlaylistPlayhead();
    void     beginPianoRollPatternPlayback();
    void     restorePlayModeAfterPianoRollPlayback();
    void     exportCurrentPianoRollToMidi();
    void     importCurrentPianoRollFromMidi();
    void     refreshSynthEditorPresetList(const juce::String& selectPresetName = {});

    // M6 — undo/redo
    juce::UndoManager undoManager;

    // M4 — file state
    juce::File currentFile;
    bool       projectDirty = false;

    // Rebuilds all UI from the current project (used after load / new)
    void reloadProjectIntoUI();
    void markDirty();

    // File operations
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();

    // Kept alive until async chooser completes
    std::shared_ptr<juce::FileChooser> fileChooser;

    bool recordTransitioning_ = false;
    bool loopRecordEnabled_   = false;   // mirrors audioEngine.isLoopRecording() for message thread
    bool liveMode_            = false;   // Live Performance Mode active state

    ClipLauncher clipLauncher_;          // Live Performance clip launch state (message thread)

    // Live Performance debug window
    std::unique_ptr<LivePerformanceWindow> liveDebugWindow_;
    std::unique_ptr<LiveLoopWindow>        liveLoopWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
