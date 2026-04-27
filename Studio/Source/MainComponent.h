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

// Tab bar that switches between SEQUENCER / MIXER / INSTRUMENT inspector panels
class InspectorTabBar : public juce::Component
{
public:
    std::function<void(int)> onTabChanged;

    void setTab(int t) { activeTab_ = t; repaint(); }
    int  getTab() const { return activeTab_; }

    void paint(juce::Graphics& g) override
    {
        using LF = StudioLookAndFeel;
        juce::ColourGradient bg(juce::Colour(LF::kChassis2), 0.0f, 0.0f,
                                juce::Colour(LF::kChassis), 0.0f, (float)getHeight(), false);
        g.setGradientFill(bg);
        g.fillAll();

        const int n = 3;
        const int tw = getWidth() / n;
        const char* names[] = { "SEQUENCER", "MIXER", "INSTRUMENT" };
        const char* subs[]  = { "PATTERN A", "6 CH  2 BUS", "POLY-6 MK2" };

        for (int i = 0; i < n; ++i)
        {
            juce::Rectangle<int> tab(i * tw, 0, tw, getHeight());
            if (i == activeTab_)
            {
                g.setColour(juce::Colour(LF::kPanel));
                g.fillRoundedRectangle(tab.toFloat().reduced(2.0f, 0.0f), 4.0f);
                g.setColour(juce::Colour(LF::kAccent));
                g.fillRect(tab.withHeight(2).reduced(3, 0));
                g.setColour(juce::Colour(LF::kText));
            }
            else
            {
                g.setColour(juce::Colour(LF::kTextDim));
            }
            auto top = tab.removeFromTop(18);
            g.setFont(StudioLookAndFeel::monoFont(10.0f, juce::Font::bold));
            g.drawText(names[i], top, juce::Justification::centred);
            g.setColour(i == activeTab_ ? juce::Colour(LF::kAccent) : juce::Colour(LF::kTextFaint));
            g.setFont(StudioLookAndFeel::monoFont(8.0f, juce::Font::bold));
            g.drawText(subs[i], tab.removeFromTop(12), juce::Justification::centred);
        }

        auto tag = juce::Rectangle<int>(getWidth() - 150, 7, 138, getHeight() - 14);
        g.setColour(juce::Colour(LF::kChassis));
        g.fillRoundedRectangle(tag.toFloat(), 4.0f);
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawRoundedRectangle(tag.toFloat(), 4.0f, 1.0f);
        g.setColour(juce::Colour(LF::kTextFaint));
        g.setFont(StudioLookAndFeel::monoFont(8.0f, juce::Font::bold));
        g.drawText("INSPECTOR  TRACK 03", tag, juce::Justification::centred);

        // Separator line at bottom
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine(0.0f, (float)(getHeight() - 1), (float)getWidth(), (float)(getHeight() - 1), 1.0f);
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
    int activeTab_ = 0;
};

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

    // Inspector tab bar (SEQUENCER / MIXER / INSTRUMENT)
    InspectorTabBar  inspectorTabBar_;
    int              inspectorTab_ = 0;   // 0=sequencer, 1=mixer, 2=instrument

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
