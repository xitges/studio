#include "MainComponent.h"

// ---------------------------------------------------------------------------
// M6 -- generic lambda-based undo action
// ---------------------------------------------------------------------------
namespace {
struct LambdaAction : public juce::UndoableAction
{
    std::function<bool()> performFn, undoFn;
    LambdaAction(std::function<bool()> p, std::function<bool()> u)
        : performFn(std::move(p)), undoFn(std::move(u)) {}
    bool perform() override { return performFn(); }
    bool undo()    override { return undoFn();    }
};

constexpr int kMidiPpq = 960;

class AudioSettingsContent : public juce::Component
{
public:
    explicit AudioSettingsContent(juce::AudioDeviceManager& deviceManager)
        : selector(deviceManager,
                   0, 0,
                   0, 2,
                   false, false,
                   true, true)
    {
        addAndMakeVisible(titleLabel);
        titleLabel.setText("Audio Output", juce::dontSendNotification);
        titleLabel.setJustificationType(juce::Justification::centredLeft);
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
        titleLabel.setFont(juce::Font(juce::FontOptions().withHeight(17.0f).withStyle("Bold")));

        addAndMakeVisible(hintLabel);
        hintLabel.setText("Reconnect Bluetooth? Reopen this panel and reselect the output device.", juce::dontSendNotification);
        hintLabel.setJustificationType(juce::Justification::centredLeft);
        hintLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb9c0ca));
        hintLabel.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));

        addAndMakeVisible(selector);
        setSize(460, 300);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1b1c1f));
        g.setColour(juce::Colour(0xff24262b));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f, 10.0f), 10.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f, 10.0f), 10.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(22, 18);
        titleLabel.setBounds(area.removeFromTop(24));
        area.removeFromTop(2);
        hintLabel.setBounds(area.removeFromTop(18));
        area.removeFromTop(10);
        selector.setBounds(area);
    }

private:
    juce::Label titleLabel;
    juce::Label hintLabel;
    juce::AudioDeviceSelectorComponent selector;
};

class AudioSettingsWindow : public juce::DocumentWindow
{
public:
    explicit AudioSettingsWindow(juce::AudioDeviceManager& deviceManager)
        : juce::DocumentWindow("Audio Settings",
                               juce::Colour(0xff202124),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);
        setContentOwned(new AudioSettingsContent(deviceManager), true);
        centreWithSize(460, 300);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }
};
} // namespace

MainComponent::MainComponent()
{
    // M11 -- apply custom dark LookAndFeel globally
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

    // ---- Seed project with initial data
    project.bpm          = 70.0;
    project.channelCount = 3;

    // M5 -- default 8 mixer tracks
    for (int t = 0; t < 8; ++t)
    {
        MixerTrack mt;
        mt.name = "Track " + juce::String(t + 1);
        project.mixerTracks.push_back(mt);
    }
    // M5 -- default routing now lives in Pattern constructor (channelMixerRouting[i] = i % 8)

    // M11 -- default 8 playlist tracks
    for (int t = 0; t < 8; ++t)
    {
        PlaylistTrack pt;
        pt.name = "Track " + juce::String(t + 1);
        project.playlistTracks.push_back(pt);
    }

    // Reserve capacity so push_back is less likely to reallocate mid-session
    project.patterns.reserve(64);

    Pattern p1;
    p1.id = 1; p1.name = "Pattern 1"; p1.lengthBars = 1; p1.stepCount = 16;
    p1.channelNames[0] = "Kick";
    p1.channelNames[1] = "Snare";
    p1.channelNames[2] = "HiHat";
    // Default beat (variation A = index 0)
    p1.variations[0].steps[0][0] = p1.variations[0].steps[0][4] = p1.variations[0].steps[0][8] = p1.variations[0].steps[0][12] = true;
    p1.variations[0].steps[1][4] = p1.variations[0].steps[1][12] = true;
    for (int s = 0; s < 16; ++s) p1.variations[0].steps[2][s] = true;
    project.patterns.push_back(p1);

    PlaylistClip c1; c1.id=1; c1.patternId=1; c1.name="Intro Beat";
    c1.trackIndex=0; c1.startBar=0;  c1.lengthBars=4;
    project.playlistClips.push_back(c1);

    PlaylistClip c2; c2.id=2; c2.patternId=-1; c2.name="Main Beat";
    c2.trackIndex=0; c2.startBar=4;  c2.lengthBars=8;
    project.playlistClips.push_back(c2);

    PlaylistClip c3; c3.id=3; c3.patternId=-1; c3.name="Break";
    c3.trackIndex=1; c3.startBar=8;  c3.lengthBars=4;
    project.playlistClips.push_back(c3);

    PlaylistClip c4; c4.id=4; c4.patternId=-1; c4.name="Fill";
    c4.trackIndex=2; c4.startBar=12; c4.lengthBars=2;
    project.playlistClips.push_back(c4);

    activePatternId = 1;

    audioEngine.initialise();
    audioEngine.setProject(&project);

    // ---- Toolbar
    addAndMakeVisible(toolbar);
    addAndMakeVisible(inspectorTabBar_);
    addAndMakeVisible(stepInspector_);
    instrumentViewport_.setViewedComponent(&instrumentPanel_, false);
    instrumentViewport_.setScrollBarsShown(true, false);
    instrumentViewport_.setScrollBarThickness(8);
    addChildComponent(instrumentViewport_);

    instrumentPanel_.onTuneChanged = [this](float semitones)
    {
        if (auto* pat = findPattern(activePatternId))
        {
            const int ch = audioEngine.getMidiTargetChannel();
            if (ch >= 0 && ch < 16)
            {
                pat->channelPitch[(size_t)ch] = semitones;
                audioEngine.setChannelPitch(ch, semitones);
                markDirty();
            }
        }
    };

    instrumentPanel_.onSynthParamsChanged = [this](const SynthParams& sp)
    {
        if (auto* pat = findPattern(activePatternId))
        {
            const int ch = audioEngine.getMidiTargetChannel();
            if (ch >= 0 && ch < 16)
            {
                pat->synthParams[(size_t)ch] = sp;
                audioEngine.updatePatternSnapshot();
                markDirty();
            }
        }
    };

    instrumentPanel_.onPreviewRequested = [this](const SynthParams& params, int midiPitch)
    {
        const int ch = audioEngine.getMidiTargetChannel();
        if (ch >= 0 && ch < 16)
            audioEngine.previewSynthNote(ch, midiPitch, params);
    };

    instrumentPanel_.onStopPreviewRequested = [this]
    {
        const int ch = audioEngine.getMidiTargetChannel();
        if (ch >= 0 && ch < 16)
            audioEngine.stopEditorPreview(ch);
    };

    instrumentPanel_.onIsPreviewActive = [this]() -> bool
    {
        const int ch = audioEngine.getMidiTargetChannel();
        return ch >= 0 && ch < 16 && audioEngine.isEditorPreviewActive(ch);
    };

    instrumentPanel_.onDrumPreview = [this]
    {
        const int ch = audioEngine.getMidiTargetChannel();
        if (ch >= 0 && ch < 16)
            audioEngine.previewNote(ch, 60);
    };

    instrumentPanel_.onSavePresetRequested = [this](const SynthParams& params)
    {
        auto dialog = std::make_shared<juce::AlertWindow>("Save Synth Preset",
                                                          "Enter a preset name for the current synth settings.",
                                                          juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("name", "My Preset");
        dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0);
        auto* dlgRaw = dialog.get();
        dlgRaw->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, dialog, params](int result) mutable
            {
                if (result != 1) return;
                const auto name = dialog->getTextEditorContents("name").trim();
                if (name.isEmpty()) return;
                SynthPresets::Preset pr; pr.name = name; pr.params = params;
                project.customSynthPresets.push_back(pr);
                markDirty();
                const juce::String sel = name;
                refreshSynthEditorPresetList(sel);
                instrumentPanel_.setAvailablePresets(
                    SynthPresets::mergeFactoryAndCustom(project.customSynthPresets), sel);
            }), true);
    };

    instrumentPanel_.onRenamePresetRequested = [this](const juce::String& oldName)
    {
        auto dialog = std::make_shared<juce::AlertWindow>("Rename Preset",
                                                          "New name for: " + oldName,
                                                          juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("name", oldName);
        dialog->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel", 0);
        auto* dlgRaw = dialog.get();
        dlgRaw->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, dialog, oldName](int result) mutable
            {
                if (result != 1) return;
                const auto newName = dialog->getTextEditorContents("name").trim();
                if (newName.isEmpty()) return;
                for (auto& p : project.customSynthPresets)
                    if (p.name == oldName) { p.name = newName; break; }
                markDirty();
                refreshSynthEditorPresetList(newName);
                instrumentPanel_.setAvailablePresets(
                    SynthPresets::mergeFactoryAndCustom(project.customSynthPresets), newName);
            }), true);
    };

    instrumentPanel_.onDeletePresetRequested = [this](const juce::String& name)
    {
        project.customSynthPresets.erase(
            std::remove_if(project.customSynthPresets.begin(), project.customSynthPresets.end(),
                           [&](const SynthPresets::Preset& p) { return p.name == name; }),
            project.customSynthPresets.end());
        markDirty();
        refreshSynthEditorPresetList();
        instrumentPanel_.setAvailablePresets(
            SynthPresets::mergeFactoryAndCustom(project.customSynthPresets));
    };

    inspectorTabBar_.onTabChanged = [this](int t)
    {
        inspectorTab_ = t;
        showMixer = (t == 2);   // tab 2 = MIXER
        resized();
    };

    toolbar.updatePatternList(project.patterns, activePatternId);
    channelRack.setPatternStartStep(patternStartStep);

    // Phase-6: initialise inspector tab sub-labels
    if (!project.patterns.empty())
        inspectorTabBar_.setSequencerSub(project.patterns[0].name);
    inspectorTabBar_.setInstrumentSub(0, channelRack.getChannelName(0));

    // Initialise instrument panel with channel 0
    if (!project.patterns.empty())
    {
        const bool hp0 = audioEngine.hasPlugin(0);
        const auto& pat0 = project.patterns[0];
        instrumentPanel_.setChannel(0, channelRack.getChannelName(0),
                                    pat0.samplePaths[0],
                                    pat0.synthParams[0],
                                    pat0.channelPitch[0],
                                    pat0.channelTypes[0],
                                    hp0,
                                    hp0 ? audioEngine.getPlugin(0)->getName() : juce::String{});
        if (hp0)
            instrumentPanel_.buildVstKnobs(audioEngine.getPlugin(0));
        if (inspectorTab_ == 0)
            instrumentPanel_.setSize(
                instrumentViewport_.getWidth() - instrumentViewport_.getScrollBarThickness(),
                instrumentPanel_.getNeededHeight());
        if (pat0.channelTypes[0] == ChannelType::Melodic && !hp0)
            instrumentPanel_.setAvailablePresets(
                SynthPresets::mergeFactoryAndCustom(project.customSynthPresets),
                pat0.synthParams[0].presetName);
    }

    toolbar.onPlay = [this]
    {
        syncPatternToEngine();
        markDirty();
        project.bpm = toolbar.getBPM();
        audioEngine.setBPM(project.bpm);
        audioEngine.setPlayMode(toolbar.getPlayMode());
        project.patternStartStep = patternStartStep;

        if (project.playMode == PlayMode::Song)
        {
            const double resumeBar = (pausedBarSong >= 0.0) ? pausedBarSong : project.songStartBar;
            audioEngine.play(patternStartStep, project.songStartBar);
            if (resumeBar > 0.0) audioEngine.seekSongToBar(resumeBar);
        }
        else
        {
            // Pattern mode: resume from saved beat position
            const double resumeBeat = pausedPatternBeat;
            if (resumeBeat > 0.0)
            {
                const int stepCount  = juce::jmax(1, channelRack.getStepCount());
                const int resumeStep = juce::jlimit(0, stepCount - 1, (int)(resumeBeat / 0.25));
                audioEngine.play(resumeStep, project.songStartBar);
            }
            else
            {
                audioEngine.play(patternStartStep, project.songStartBar);
            }
        }
        pausedBarSong     = -1.0;
        pausedPatternBeat = -1.0;
    };

    toolbar.onStop = [this]
    {
        // Save current position for resume
        if (audioEngine.isPlaying())
        {
            pausedBarSong     = audioEngine.getSongBeatPosition() / 4.0;
            pausedPatternBeat = audioEngine.getPatternBeatPos();
        }

        // Stop recording if active
        if (audioEngine.isRecording())
        {
            audioEngine.setInputMonitoring(false);
            audioEngine.stopRecording();
            toolbar.setRecordingActive(false);
        }

        audioEngine.stop();
        audioEngine.allSynthNotesOff();
        channelRack.setPlaybackStep(-1);
        playlist.setPlayheadBar(pausedBarSong > 0.0 ? pausedBarSong : -1.0);
        if (pianoRollWindow != nullptr)
            pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);
    };

    // Rec button: single click to start recording, click again to stop
    toolbar.onRecordStart = [this]
    {
        if (recordTransitioning_) return;
        recordTransitioning_ = true;

        // Live Performance Mode: arm current channel for loop recording
        if (liveMode_)
        {
            const int ch = audioEngine.getMidiTargetChannel();
            if (audioEngine.liveLoopGetState(ch) == LiveLoopEngine::State::Idle)
            {
                audioEngine.liveLoopArm(ch, 16.0);  // default 16 beats; user changes via UI
                toolbar.setRecordingActive(true);
            }
            recordTransitioning_ = false;
            return;
        }

        // Normal mode: WAV file recording
        juce::File recDir;
        if (currentFile.existsAsFile())
            recDir = currentFile.getParentDirectory().getChildFile("Recordings");
        else
            recDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile("Studio").getChildFile("Recordings");
        recDir.createDirectory();

        auto now = juce::Time::getCurrentTime();
        auto filename = "Recording_" + now.formatted("%Y%m%d_%H%M%S") + ".wav";
        auto recFile = recDir.getChildFile(filename);

        audioEngine.setInputMonitoring(true);

        if (audioEngine.startRecording(recFile))
            toolbar.setRecordingActive(true);

        recordTransitioning_ = false;
    };

    toolbar.onRecordStop = [this]
    {
        if (recordTransitioning_) return;
        recordTransitioning_ = true;

        if (liveMode_)
        {
            // Stop live loop recording on current channel
            const int ch = audioEngine.getMidiTargetChannel();
            audioEngine.liveLoopStop(ch);
            toolbar.setRecordingActive(false);
            recordTransitioning_ = false;
            return;
        }

        audioEngine.setInputMonitoring(false);
        audioEngine.stopRecording();
        toolbar.setRecordingActive(false);

        recordTransitioning_ = false;
    };

    // When recording finishes, auto-create an audio clip in the playlist
    audioEngine.onRecordingFinished = [this](const juce::File& recordedFile,
                                             double startBar, double lengthBars)
    {
        if (!recordedFile.existsAsFile() || lengthBars <= 0.0)
            return;

        // Find next available clip ID
        int maxId = 0;
        for (const auto& c : project.playlistClips)
            if (c.id > maxId) maxId = c.id;

        PlaylistClip clip;
        clip.id            = maxId + 1;
        clip.clipType      = ClipType::Audio;
        clip.audioFilePath = recordedFile.getFullPathName();
        clip.name          = recordedFile.getFileNameWithoutExtension();
        clip.startBar      = (float)startBar;
        clip.lengthBars    = (float)lengthBars;
        clip.trackIndex    = 0;

        project.playlistClips.push_back(clip);
        audioEngine.loadAudioClip(clip.id, recordedFile, clip.lengthBars);
        audioEngine.refreshSongCacheAsync();

        playlist.repaint();
        markDirty();
    };

    toolbar.onBPMChanged = [this](double bpm)
    {
        project.bpm = bpm;
        audioEngine.setBPM(bpm);
        if (pianoRollWindow != nullptr)
            pianoRollWindow->content.setBPM(bpm);
        markDirty();
    };

    toolbar.onPlayModeChanged = [this](PlayMode mode)
    {
        // Stop current playback BEFORE changing mode so stop() uses the
        // correct mode-specific logic (song: clears songPlaying; pattern: stops sequencer).
        audioEngine.stop();
        audioEngine.allSynthNotesOff();
        audioEngine.clearTransientPlaybackState();
        channelRack.setPlaybackStep(-1);
        playlist.setPlayheadBar(-1.0);
        if (pianoRollWindow != nullptr)
            pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);

        project.playMode = mode;
        audioEngine.setPlayMode(mode);
        markDirty();
    };

    channelRack.onPatternStartStepChanged = [this](int stepZeroBased)
    {
        patternStartStep = juce::jmax(0, stepZeroBased);
        project.patternStartStep = patternStartStep;
        markDirty();
    };

    // M2.1 -- Pattern selector
    toolbar.onPatternSelected = [this](int id)
    {
        selectPattern(id);
    };

    toolbar.onNewPattern = [this]
    {
        audioEngine.ensureCacheLoaderStopped();
        const int newId   = nextPatternId();
        const int newNum  = (int)project.patterns.size() + 1;

        Pattern np;
        np.id           = newId;
        np.name         = "Pattern " + juce::String(newNum);
        np.stepCount    = 16;
        np.channelCount = 3;   // fresh pattern starts with 3 channels

        project.patterns.push_back(np);
        selectPattern(newId);
        markDirty();
        audioEngine.refreshSongCacheAsync();
    };

    toolbar.onDuplicatePattern = [this]
    {
        audioEngine.ensureCacheLoaderStopped();
        if (auto* src = findPattern(activePatternId))
            channelRack.saveToPattern(*src);

        if (auto* src = findPattern(activePatternId))
        {
            Pattern dup  = *src;
            const int newId = nextPatternId();
            dup.id   = newId;
            dup.name = src->name + " (copy)";
            project.patterns.push_back(dup);

            selectPattern(newId);
            markDirty();
            audioEngine.refreshSongCacheAsync();
        }
    };

    auto markDirtyFn = [this] { markDirty(); };

    toolbar.onDeletePattern = [this, markDirtyFn]
    {
        if (project.patterns.size() <= 1) return;
        audioEngine.ensureCacheLoaderStopped();

        const int deletedId = activePatternId;

        auto& pats = project.patterns;
        pats.erase(std::remove_if(pats.begin(), pats.end(),
            [deletedId](const Pattern& p){ return p.id == deletedId; }), pats.end());

        const int fallbackId = pats.front().id;
        for (auto& clip : project.playlistClips)
            if (clip.patternId == deletedId)
                clip.patternId = -1;

        selectPattern(fallbackId);
        markDirty();
        audioEngine.refreshSongCacheAsync();
    };

    toolbar.onRenamePattern = [this]
    {
        auto* pat = findPattern(activePatternId);
        if (pat == nullptr) return;

        auto* dialog = new juce::AlertWindow("Rename Pattern", "Enter new name:",
                                              juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("name", pat->name);
        dialog->addButton("OK",     1);
        dialog->addButton("Cancel", 0);

        dialog->enterModalState(true,
            juce::ModalCallbackFunction::create([this, dialog](int result)
            {
                if (result == 1)
                {
                    if (auto* p = findPattern(activePatternId))
                    {
                        const auto newName = dialog->getTextEditorContents("name").trim();
                        if (newName.isNotEmpty())
                            p->name = newName;
                    }
                    toolbar.updatePatternList(project.patterns, activePatternId);
                }
                delete dialog;
            }),
            false);
    };

    // ---- M4: File buttons
    toolbar.onNewFile    = [this] { newProject();   };
    toolbar.onOpenFile   = [this] { openProject();  };
    toolbar.onSaveFile   = [this] { saveProject();  };
    toolbar.onSaveFileAs = [this] { saveProjectAs(); };

    toolbar.onExport = [this]
    {
        syncPatternToEngine();

        // Calculate how many bars to render
        int numBars = 0;
        if (toolbar.getPlayMode() == PlayMode::Pattern)
        {
            if (auto* pat = findPattern(activePatternId))
                numBars = juce::jmax(1, (int)std::ceil((double)pat->stepCount / 16.0)) * 4;
            if (numBars <= 0) numBars = 4;
        }
        else
        {
            for (const auto& clip : project.playlistClips)
                numBars = juce::jmax(numBars, (int)std::ceil(clip.startBar + clip.lengthBars));
            if (numBars <= 0) numBars = 8;
        }

        const int      barsToRender  = numBars;
        const PlayMode modeToRender  = toolbar.getPlayMode();

        fileChooser = std::make_shared<juce::FileChooser>(
            "Export to WAV",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("export.wav"),
            "*.wav");

        fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
            [this, barsToRender, modeToRender](const juce::FileChooser& fc)
            {
                auto chosen = fc.getResult();
                if (chosen == juce::File{}) return;

                if (chosen.getFileExtension().toLowerCase() != ".wav")
                    chosen = chosen.withFileExtension("wav");

                audioEngine.stop();

                if (audioEngine.renderToFile(chosen, modeToRender, barsToRender))
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        "Export Complete",
                        "Saved to:\n" + chosen.getFullPathName());
                else
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Export Failed",
                        "Could not write the WAV file.");
            });
    };

    toolbar.onExportStems = [this]
    {
        syncPatternToEngine();

        int numBars = 0;
        if (toolbar.getPlayMode() == PlayMode::Pattern)
        {
            if (auto* pat = findPattern(activePatternId))
                numBars = juce::jmax(1, (int)std::ceil((double)pat->stepCount / 16.0)) * 4;
            if (numBars <= 0) numBars = 4;
        }
        else
        {
            for (const auto& clip : project.playlistClips)
                numBars = juce::jmax(numBars, (int)std::ceil(clip.startBar + clip.lengthBars));
            if (numBars <= 0) numBars = 8;
        }

        const int      barsToRender = numBars;
        const PlayMode modeToRender = toolbar.getPlayMode();

        fileChooser = std::make_shared<juce::FileChooser>(
            "Select Folder for Stems",
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [this, barsToRender, modeToRender](const juce::FileChooser& fc)
            {
                auto folder = fc.getResult();
                if (!folder.isDirectory()) return;

                audioEngine.stop();

                int count = audioEngine.renderStemsToFolder(folder, modeToRender, barsToRender);
                if (count > 0)
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        "Stem Export Complete",
                        juce::String(count) + " stems saved to:\n" + folder.getFullPathName());
                else
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Stem Export Failed",
                        "Could not export stems.");
            });
    };

    toolbar.setProjectTitle("", false);

    // ---- Playlist inside Viewport
    playlist.setProject(&project);
    playlist.onSeekToBar = [this](double bar)
    {
        pausedBarSong = bar;    // remember this as the next resume point
        audioEngine.seekSongToBar(bar);
    };

    playlist.onZoomChanged = [this] { resized(); };

    playlistViewport.setViewedComponent(&playlist, false);

    // M15 -- sample browser
    sampleBrowser.onPreviewFile = [this](const juce::File& f)
    {
        audioEngine.previewBrowserFile(f);
    };
    sampleBrowser.onStopPreview = [this]
    {
        audioEngine.stopBrowserPreview();
    };
    browserViewport.setViewedComponent(&sampleBrowser, false);
    browserViewport.setScrollBarsShown(true, false);
    browserViewport.setScrollBarThickness(6);
    addAndMakeVisible(browserViewport);

    // In-browser collapse handled by SampleBrowserComponent's own button
    sampleBrowser.onCollapseClicked = [this]
    {
        isBrowserOpen = false;
        resized();
    };

    // Reopen tab -- shown only when browser is closed, at the left edge
    browserCollapseBtn.setButtonText(">");
    browserCollapseBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
    browserCollapseBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextDim));
    browserCollapseBtn.onClick = [this]
    {
        isBrowserOpen = true;
        resized();
    };
    addAndMakeVisible(browserCollapseBtn);
    playlistViewport.setScrollBarsShown(true, true);
    playlistViewport.setScrollBarThickness(8);
    addAndMakeVisible(playlistViewport);

    // Snap box -- floats outside the viewport so it never scrolls away
    playlistSnapBox.addItem("1 Bar",    1);
    playlistSnapBox.addItem("1/2 Bar",  2);
    playlistSnapBox.addItem("1/4 Bar",  4);
    playlistSnapBox.addItem("1/8 Bar",  8);
    playlistSnapBox.addItem("1/16 Bar", 16);
    playlistSnapBox.addItem("Free",     99);
    playlistSnapBox.setSelectedId(4, juce::dontSendNotification);  // default: 1/4 bar
    playlistSnapBox.onChange = [this]
    {
        const int id = playlistSnapBox.getSelectedId();
        playlist.setSnapDivisor(id == 99 ? 0 : id);
    };
    addAndMakeVisible(playlistSnapBox);

    // M11 -- zoom buttons (anchored alongside snap box)
    playlistZoomInBtn.setTooltip("Zoom in");
    playlistZoomOutBtn.setTooltip("Zoom out");
    playlistZoomInBtn.onClick = [this]
    {
        playlist.setBarWidth(playlist.getBarWidth() + 16);
        resized();
    };
    playlistZoomOutBtn.onClick = [this]
    {
        playlist.setBarWidth(playlist.getBarWidth() - 16);
        resized();
    };
    addAndMakeVisible(playlistZoomInBtn);
    addAndMakeVisible(playlistZoomOutBtn);

    // ---- Channel Rack inside Viewport
    channelRack.loadPattern(project.patterns[0]);   // load initial pattern into grid

    channelRackViewport.setViewedComponent(&channelRack, false);
    channelRackViewport.setScrollBarsShown(true, false);
    channelRackViewport.setScrollBarThickness(8);
    addAndMakeVisible(channelRackViewport);

    channelRack.getCurrentStep = [this]()
    {
        return audioEngine.getSequencer().getCurrentStep();
    };

    channelRack.onSampleDropped = [this](int ch, juce::File file)
    {
        audioEngine.loadSample(ch, file);
        if (auto* pat = findPattern(activePatternId))
        {
            pat->samplePaths[ch] = file.getFullPathName();
            
            // 1. 샘플 파일명으로 패턴 데이터 업데이트
            juce::String newName = file.getFileNameWithoutExtension();
            pat->channelNames[ch] = newName;
            
            // 2. 채널랙 새로고침
            channelRack.resetToChannelCount(project.channelCount, pat->channelNames);
            channelRack.loadPattern(*pat);
            
            // 3. 인스펙터 탭 바 (글자) 즉시 새로고침
            if (audioEngine.getMidiTargetChannel() == ch)
            {
                inspectorTabBar_.setInstrumentSub(ch, newName);
                inspectorTabBar_.repaint();
                
                // 4. 메인 화면의 인스트러먼트 패널 (파형 및 헤더) 즉시 새로고침
                {
                    const bool hp = audioEngine.hasPlugin(ch);
                    instrumentPanel_.setChannel(
                        ch, newName,
                        pat->samplePaths[ch],
                        pat->synthParams[ch],
                        pat->channelPitch[ch],
                        pat->channelTypes[(size_t)ch],
                        hp,
                        hp ? audioEngine.getPlugin(ch)->getName() : juce::String{});
                    if (inspectorTab_ == 0)
                        instrumentPanel_.setSize(
                            instrumentViewport_.getWidth() - instrumentViewport_.getScrollBarThickness(),
                            instrumentPanel_.getNeededHeight());
                }
            }
                
        
        }
        markDirty();
        audioEngine.refreshSongCacheAsync();
    };

    channelRack.onChannelSelected = [this](int ch)
    {
        audioEngine.setMidiTargetChannel(ch);
        channelRack.setSelectedMidiChannel(ch);
        inspectorTabBar_.setInstrumentSub(ch, channelRack.getChannelName(ch));

        if (auto* pat = findPattern(activePatternId))
        {
            const juce::String samplePath = pat->samplePaths[(size_t)ch];
            const bool hp = audioEngine.hasPlugin(ch);
            instrumentPanel_.setChannel(ch, channelRack.getChannelName(ch),
                                        samplePath, pat->synthParams[(size_t)ch],
                                        pat->channelPitch[(size_t)ch],
                                        pat->channelTypes[(size_t)ch],
                                        hp,
                                        hp ? audioEngine.getPlugin(ch)->getName() : juce::String{});
            if (hp)
                instrumentPanel_.buildVstKnobs(audioEngine.getPlugin(ch));
            else
                instrumentPanel_.clearVstKnobs();

            if (inspectorTab_ == 0)
                instrumentPanel_.setSize(
                    instrumentViewport_.getWidth() - instrumentViewport_.getScrollBarThickness(),
                    instrumentPanel_.getNeededHeight());
            if (pat->channelTypes[(size_t)ch] == ChannelType::Melodic && !hp)
                instrumentPanel_.setAvailablePresets(
                    SynthPresets::mergeFactoryAndCustom(project.customSynthPresets),
                    pat->synthParams[(size_t)ch].presetName);
        }
    };

    channelRack.onMuteChanged = [this](int ch, bool muted)
    {
        audioEngine.setChannelMuted(ch, muted);
    };

    channelRack.onSoloChanged = [this](int ch, bool soloed)
    {
        audioEngine.setChannelSolo(ch, soloed);
    };

    channelRack.onVolumeChanged = [this](int ch, float v)
    {
        audioEngine.setChannelVolume(ch, v);
    };

    channelRack.onPanChanged = [this](int ch, float p)
    {
        audioEngine.setChannelPan(ch, p);
    };

    channelRack.onPitchChanged = [this](int ch, float s)
    {
        audioEngine.setChannelPitch(ch, s);
    };

    channelRack.onStepCountChanged = [this](int newCount)
    {
        if (auto* pat = findPattern(activePatternId))
        {
            pat->stepCount = newCount;
            patternStartStep = juce::jlimit(0, pat->stepCount - 1, patternStartStep);
            pianoRollStartStep = juce::jlimit(0, pat->stepCount - 1, pianoRollStartStep);
            project.patternStartStep = patternStartStep;
            channelRack.setPatternStartStep(patternStartStep);
            audioEngine.setPatternStepCount(newCount);
            if (pianoRollWindow != nullptr && pianoRollWindow->isVisible())
            {
                pianoRollWindow->content.pianoRoll.updateStepCount();
                pianoRollWindow->content.setStartStep(pianoRollStartStep);
            }
        }
        markDirty();
    };

    // ---- Groove swing
    channelRack.onSwingChanged = [this](float swing)
    {
        if (auto* pat = findPattern(activePatternId))
        {
            pat->swingAmount = swing;
            audioEngine.setSwingAmount(swing);
        }
        markDirty();
    };

    // ---- M6: Step drag undo batching
    // onStepDragBegin opens a new transaction at mouseDown so all steps toggled
    // during a single drag gesture are grouped into one Ctrl+Z action.
    // onStepDragEnd closes the group so the next action starts a fresh transaction.
    channelRack.onStepDragBegin = [this] { undoManager.beginNewTransaction(); };
    channelRack.onStepDragEnd   = [this] { undoManager.beginNewTransaction(); };

    // ---- M6: Step toggle undo
    channelRack.onStepToggled = [this](int ch, int step, bool newState, bool oldState)
    {
        markDirty();
        const int patId = activePatternId;
        const int varIdx = channelRack.activeVariation;
        undoManager.perform(new LambdaAction(
            [this, patId, ch, step, newState, varIdx]() -> bool {
                if (auto* p = findPattern(patId)) {
                    p->variations[varIdx].steps[ch][step] = newState;
                    if (patId == activePatternId) {
                        channelRack.setStep(ch, step, newState);
                        audioEngine.setStepPattern(ch, step, newState);  // live sync
                        audioEngine.updatePatternSnapshot();
                    }
                }
                markDirty(); return true;
            },
            [this, patId, ch, step, oldState, varIdx]() -> bool {
                if (auto* p = findPattern(patId)) {
                    p->variations[varIdx].steps[ch][step] = oldState;
                    if (patId == activePatternId) {
                        channelRack.setStep(ch, step, oldState);
                        audioEngine.setStepPattern(ch, step, oldState);  // live sync
                        audioEngine.updatePatternSnapshot();
                    }
                }
                markDirty(); return true;
            }));
    };

    channelRack.onClearAllSteps = [this]
    {
        if (auto* pat = findPattern(activePatternId))
        {
            channelRack.saveToPattern(*pat);
            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
                for (int s = 0; s < pat->stepCount; ++s)
                    audioEngine.setStepPattern(ch, s, false);

            audioEngine.updatePatternSnapshot();
            markDirty();
        }
    };

    // ---- Per-step params: inspector edits -> pattern model -> engine snapshot
    channelRack.onStepParamsChanged = [this](int ch, int step, const StepParams& p)
    {
        if (auto* pat = findPattern(activePatternId))
        {
            const int vi = channelRack.activeVariation;
            pat->variations[vi].stepParams[ch][step] = p;
            audioEngine.updatePatternSnapshot();
            markDirty();
        }
        // Keep top inspector in sync when bottom sliders are edited
        stepInspector_.updateFromExternal(p);
    };

    channelRack.onInspectorOpened = [this](int ch, int step,
                                           const juce::String& chName, const StepParams& p)
    {
        stepInspector_.setStep(ch, step, chName, p);
    };

    channelRack.onInspectorClosed = [this]
    {
        stepInspector_.clearStep();
    };

    stepInspector_.onParamsChanged = [this](int ch, int step, const StepParams& p)
    {
        channelRack.setStepParams(ch, step, p);
        if (auto* pat = findPattern(activePatternId))
        {
            const int vi = channelRack.activeVariation;
            pat->variations[vi].stepParams[ch][step] = p;
            audioEngine.updatePatternSnapshot();
            markDirty();
        }
    };

    // ---- Audio clip drop -> load into AudioEngine
    playlist.onAudioClipDropped = [this](int clipId, juce::String path)
    {
        // Find the clip to get its calculated lengthBars
        float len = 0.0f;
        for (const auto& c : project.playlistClips)
            if (c.id == clipId) { len = c.lengthBars; break; }
        audioEngine.loadAudioClip(clipId, juce::File(path), len);
        markDirty();
    };

    // Waveform preview: return the decoded buffer from AudioEngine
    playlist.getAudioBuffer = [this](const juce::String& path)
        -> std::shared_ptr<juce::AudioBuffer<float>>
    {
        return audioEngine.getAudioFileBuffer(path);
    };

    // ---- M6: Playlist clip undo
    // M11 -- track management callbacks
    playlist.onTrackAdded   = [this]()      { markDirty(); resized(); };
    playlist.onTrackRenamed = [this](int)   { markDirty(); };
    playlist.onTrackDeleted = [this](int)   { markDirty(); resized(); };

    // Pattern copy -- duplicate the assigned pattern and reassign this clip to the copy
    playlist.onClipDetach = [this](int clipId)
    {
        // Find the clip
        PlaylistClip* clip = nullptr;
        for (auto& c : project.playlistClips)
            if (c.id == clipId) { clip = &c; break; }
        if (clip == nullptr) return;

        const int oldPatternId = clip->patternId;
        Pattern* src = findPattern(oldPatternId);
        if (src == nullptr) return;

        // Flush the rack into the source pattern if it's currently active
        if (src->id == activePatternId)
            channelRack.saveToPattern(*src);

        // Copy the pattern with a new unique ID
        const int newPatId = nextPatternId();
        Pattern copy    = *src;
        copy.id         = newPatId;
        copy.name       = src->name + " (copy)";

        undoManager.perform(new LambdaAction(
            [this, clipId, newPatId, copy]() mutable -> bool {
                // Add the new pattern if it doesn't already exist
                if (findPattern(newPatId) == nullptr)
                    project.patterns.push_back(copy);
                // Reassign clip
                for (auto& c : project.playlistClips)
                    if (c.id == clipId) { c.patternId = newPatId; break; }
                toolbar.updatePatternList(project.patterns, activePatternId);
                playlist.repaint(); markDirty(); return true;
            },
            [this, clipId, oldPatternId, newPatId]() -> bool {
                // Restore clip's original pattern
                for (auto& c : project.playlistClips)
                    if (c.id == clipId) { c.patternId = oldPatternId; break; }
                // Remove the detached pattern copy
                auto& pats = project.patterns;
                pats.erase(std::remove_if(pats.begin(), pats.end(),
                    [newPatId](const Pattern& p) { return p.id == newPatId; }), pats.end());
                toolbar.updatePatternList(project.patterns, activePatternId);
                playlist.repaint(); markDirty(); return true;
            }
        ));
        audioEngine.refreshSongCacheAsync();
    };

    playlist.onClipAdded = [this](PlaylistClip clip)
    {
        markDirty();
        const int clipId = clip.id;
        auto clipState = std::make_shared<PlaylistClip>(clip);
        undoManager.perform(new LambdaAction(
            [this, clipState]() -> bool {
                bool found = false;
                for (const auto& c : project.playlistClips)
                    if (c.id == clipState->id) { found = true; break; }
                if (!found) project.playlistClips.push_back(*clipState);
                playlist.repaint(); markDirty(); return true;
            },
            [this, clipId, clipState]() -> bool {
                // Capture latest state before removing so redo restores it
                for (const auto& c : project.playlistClips)
                    if (c.id == clipId) { *clipState = c; break; }
                auto& list = project.playlistClips;
                list.erase(std::remove_if(list.begin(), list.end(),
                    [clipId](const PlaylistClip& c){ return c.id == clipId; }), list.end());
                playlist.repaint(); markDirty(); return true;
            }
        ));
        audioEngine.refreshSongCacheAsync();
    };

    playlist.onClipDeleted = [this](PlaylistClip clip)
    {
        markDirty();
        const int clipId = clip.id;
        auto clipState = std::make_shared<PlaylistClip>(clip);
        // Capture latest state from project (may differ from PlaylistComponent's copy)
        for (const auto& c : project.playlistClips)
            if (c.id == clipId) { *clipState = c; break; }
        undoManager.perform(new LambdaAction(
            [this, clipId]() -> bool {
                auto& list = project.playlistClips;
                list.erase(std::remove_if(list.begin(), list.end(),
                    [clipId](const PlaylistClip& c){ return c.id == clipId; }), list.end());
                playlist.repaint(); markDirty(); return true;
            },
            [this, clipState]() -> bool {
                project.playlistClips.push_back(*clipState);
                playlist.repaint(); markDirty(); return true;
            }));
        audioEngine.refreshSongCacheAsync();
    };

    playlist.onClipMoved = [this](int id, float oldBar, int oldTrack, float newBar, int newTrack)
    {
        markDirty();
        undoManager.perform(new LambdaAction(
            [this, id, newBar, newTrack]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == id) { c.startBar = newBar; c.trackIndex = newTrack; break; }
                audioEngine.rebuildRuntimeStateFromProject();
                playlist.repaint(); markDirty(); return true;
            },
            [this, id, oldBar, oldTrack]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == id) { c.startBar = oldBar; c.trackIndex = oldTrack; break; }
                audioEngine.rebuildRuntimeStateFromProject();
                playlist.repaint(); markDirty(); return true;
            }));
    };

    playlist.onClipResized = [this](int id, float oldLen, float newLen)
    {
        markDirty();
        undoManager.perform(new LambdaAction(
            [this, id, newLen]() -> bool {
                for (auto& c : project.playlistClips)
                {
                    if (c.id == id)
                    {
                        c.lengthBars = newLen;
                        // Update audio clip playback window to match new length
                        if (c.clipType == ClipType::Audio && c.audioFilePath.isNotEmpty())
                            audioEngine.loadAudioClip(id, juce::File(c.audioFilePath), newLen);
                        break;
                    }
                }
                audioEngine.rebuildRuntimeStateFromProject();
                playlist.repaint(); markDirty(); return true;
            },
            [this, id, oldLen]() -> bool {
                for (auto& c : project.playlistClips)
                {
                    if (c.id == id)
                    {
                        c.lengthBars = oldLen;
                        if (c.clipType == ClipType::Audio && c.audioFilePath.isNotEmpty())
                            audioEngine.loadAudioClip(id, juce::File(c.audioFilePath), oldLen);
                        break;
                    }
                }
                audioEngine.rebuildRuntimeStateFromProject();
                playlist.repaint(); markDirty(); return true;
            }));
    };

    playlist.onClipPatternChanged = [this](int clipId, int newPatternId)
    {
        int oldPatternId = -1;
        for (const auto& c : project.playlistClips)
            if (c.id == clipId) { oldPatternId = c.patternId; break; }

        undoManager.perform(new LambdaAction(
            [this, clipId, newPatternId]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == clipId) { c.patternId = newPatternId; break; }
                playlist.repaint(); markDirty(); return true;
            },
            [this, clipId, oldPatternId]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == clipId) { c.patternId = oldPatternId; break; }
                playlist.repaint(); markDirty(); return true;
            }));
        audioEngine.refreshSongCacheAsync();
    };

    // Phase 4 -- undoable clip rename
    playlist.onClipRenamed = [this](int clipId, juce::String oldName, juce::String newName)
    {
        undoManager.perform(new LambdaAction(
            [this, clipId, newName]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == clipId) { c.name = newName; break; }
                playlist.repaint(); markDirty(); return true;
            },
            [this, clipId, oldName]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == clipId) { c.name = oldName; break; }
                playlist.repaint(); markDirty(); return true;
            }
        ));
    };

    // Audio clip pitch changed
    playlist.onClipPitchChanged = [this](int clipId, float /*oldPitch*/, float newPitch)
    {
        AudioClipMode mode = AudioClipMode::Resample;
        for (auto& c : project.playlistClips)
            if (c.id == clipId) { c.pitchSemitone = newPitch; mode = c.audioClipMode; break; }
        audioEngine.reprocessAudioClipPitch(clipId, mode, newPitch);
        audioEngine.setProject(&project);
        markDirty();
    };

    // Audio clip mode changed -- rebuild pitched buffer, push runtime
    playlist.onClipModeChanged = [this](int clipId, AudioClipMode /*old*/, AudioClipMode newMode)
    {
        float pitch = 0.0f;
        for (auto& c : project.playlistClips)
            if (c.id == clipId) { c.audioClipMode = newMode; pitch = c.pitchSemitone; break; }
        audioEngine.reprocessAudioClipPitch(clipId, newMode, pitch);
        audioEngine.setProject(&project);
        markDirty();
    };

    // Clip fade changed -- update model and push runtime
    playlist.onClipFadeChanged = [this](int clipId, float fadeInBars, float fadeOutBars)
    {
        for (auto& c : project.playlistClips)
        {
            if (c.id == clipId)
            {
                c.fadeInBars  = fadeInBars;
                c.fadeOutBars = fadeOutBars;
                break;
            }
        }
        audioEngine.setProject(&project);
        markDirty();
    };

    // Pattern slip edit -- update patternStartOffsetBars and push runtime
    playlist.onPatternSlipEdited = [this](int clipId, float /*oldOffset*/, float newOffset)
    {
        for (auto& c : project.playlistClips)
        {
            if (c.id == clipId)
            {
                c.patternStartOffsetBars = newOffset;
                break;
            }
        }
        audioEngine.setProject(&project);
        markDirty();
    };

    // Audio slip edit -- update sourceOffsetSamples and push runtime
    playlist.onClipSlipEdited = [this](int clipId, float /*oldOffset*/, float newOffset)
    {
        for (auto& c : project.playlistClips)
        {
            if (c.id == clipId)
            {
                c.sourceOffsetSamples = newOffset;
                break;
            }
        }
        audioEngine.setProject(&project);
        markDirty();
    };

    playlist.onClipMuteToggled = [this](int clipId, bool muted)
    {
        for (auto& c : project.playlistClips)
        {
            if (c.id == clipId) { c.muted = muted; break; }
        }
        audioEngine.setProject(&project);
        markDirty();
    };

    // Automation undo callbacks
    playlist.onAutomationPointAdded = [this](int laneIdx, int ptIdx, AutomationPoint pt)
    {
        audioEngine.setProject(&project);
        markDirty();
        undoManager.perform(new LambdaAction(
            []() { return true; }, // perform = already done
            [this, laneIdx, ptIdx]()
            {
                if (laneIdx < (int)project.automationLanes.size())
                {
                    auto& lane = project.automationLanes[(size_t)laneIdx];
                    if (ptIdx < (int)lane.points.size())
                        lane.points.erase(lane.points.begin() + ptIdx);
                    audioEngine.setProject(&project);
                    playlist.repaint();
                }
                return true;
            }));
    };

    playlist.onAutomationPointMoved = [this](int laneIdx, int ptIdx, AutomationPoint before, AutomationPoint after)
    {
        audioEngine.setProject(&project);
        markDirty();
        undoManager.perform(new LambdaAction(
            [this, laneIdx, ptIdx, after]()
            {
                if (laneIdx < (int)project.automationLanes.size())
                {
                    auto& lane = project.automationLanes[(size_t)laneIdx];
                    if (ptIdx < (int)lane.points.size())
                        lane.points[(size_t)ptIdx] = after;
                    audioEngine.setProject(&project);
                    playlist.repaint();
                }
                return true;
            },
            [this, laneIdx, ptIdx, before]()
            {
                if (laneIdx < (int)project.automationLanes.size())
                {
                    auto& lane = project.automationLanes[(size_t)laneIdx];
                    if (ptIdx < (int)lane.points.size())
                        lane.points[(size_t)ptIdx] = before;
                    audioEngine.setProject(&project);
                    playlist.repaint();
                }
                return true;
            }));
    };

    playlist.onAutomationLaneAdded = [this](AutomationLane /*lane*/)
    {
        audioEngine.setProject(&project);
        markDirty();
        resized();
    };

    playlist.onAutomationLaneRemoved = [this](int /*laneIdx*/, AutomationLane /*lane*/)
    {
        audioEngine.setProject(&project);
        markDirty();
        resized();
    };

    // Phase 4 -- double-click clip -> navigate to its pattern
    playlist.onNavigateToPattern = [this](int patternId)
    {
        if (findPattern(patternId) == nullptr) return;
        // Save current rack to active pattern before switching
        if (auto* cur = findPattern(activePatternId))
            channelRack.saveToPattern(*cur);
        activePatternId = patternId;
        if (auto* pat = findPattern(activePatternId))
        {
            channelRack.loadPattern(*pat);
            patternStartStep = juce::jlimit(0, pat->stepCount - 1, patternStartStep);
            pianoRollStartStep = juce::jlimit(0, pat->stepCount - 1, pianoRollStartStep);
            project.patternStartStep = patternStartStep;
            channelRack.setPatternStartStep(patternStartStep);
            if (pianoRollWindow != nullptr && pianoRollWindow->isVisible())
                pianoRollWindow->content.setStartStep(pianoRollStartStep);
        }
        toolbar.updatePatternList(project.patterns, activePatternId);
        if (auto* pat = findPattern(activePatternId))
            mixer.updateRoutingLabels(pat->channelMixerRouting);
        audioEngine.rebuildRuntimeStateFromProject();
    };

    // ---- M3: Piano Roll
    channelRack.onOpenPianoRoll = [this](int ch)
    {
        switchToPatternModeForEditing();
        pianoRollChannel = ch;
        audioEngine.setMidiTargetChannel(ch);

        if (pianoRollWindow == nullptr)
        {
            pianoRollWindow = std::make_unique<PianoRollWindow>();
            pianoRollWindow->centreWithSize(1520, 700);
        }

        if (auto* pat = findPattern(activePatternId))
        {
            pianoRollWindow->content.setPattern(pat, ch, project.bpm);
            pianoRollStartStep = juce::jlimit(0, pat->stepCount - 1, pianoRollStartStep);
            pianoRollWindow->content.setStartStep(pianoRollStartStep);
            pianoRollWindow->content.setKeySignature(project.keySignature);
            pianoRollWindow->content.pianoRoll.onNotesChanged = [this] { markDirty(); };
            pianoRollWindow->content.pianoRoll.onNoteDeleted  = [this, ch](int pitch)
            {
                // Apply channelBasePitch transposition to match what was actually triggered
                const int tp = juce::jlimit(0, 127,
                    pitch + (int)std::round(audioEngine.getChannelBasePitch(ch)));
                audioEngine.noteOffChannel(ch, tp);
            };
            pianoRollWindow->content.pianoRoll.onKeyPreview   = [this, ch](int pitch)
            {
                audioEngine.previewNote(ch, pitch);
            };
            pianoRollWindow->content.pianoRoll.onPlayStopToggle = [this]
            {
                if (audioEngine.isPlaying())
                {
                    // Commit any held recording notes before stopping
                    pianoRollWindow->content.pianoRoll.setRecording(false);
                    audioEngine.stop();
                    audioEngine.allSynthNotesOff();
                    channelRack.setPlaybackStep(-1);
                    playlist.setPlayheadBar(-1.0);
                    pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);
                    restorePlayModeAfterPianoRollPlayback();
                }
                else
                {
                    auto& pr = pianoRollWindow->content.pianoRoll;
                    if (pr.getRecState() == PianoRollComponent::RecState::Armed)
                    {
                        // Space again while Armed -> cancel -> Idle
                        pr.setRecording(false);
                    }
                    else if (pr.isTriggerEnabled() &&
                             pianoRollWindow->content.recBtn.getToggleState())
                    {
                        // Trigger ON + REC active -> enter Armed (engine stays stopped)
                        pr.setRecording(true);
                    }
                    else
                    {
                        beginPianoRollPatternPlayback();
                        syncPatternToEngine();
                        const int startStep = (pianoRollWindow != nullptr)
                            ? pianoRollWindow->content.pianoRoll.getStartStep()
                            : pianoRollStartStep;
                        pianoRollStartStep = juce::jmax(0, startStep);
                        audioEngine.play(pianoRollStartStep, project.songStartBar);
                    }
                }
            };

            // Smart Record: armed trigger fires transport from beat 0
            pianoRollWindow->content.pianoRoll.onStartTransport = [this]
            {
                beginPianoRollPatternPlayback();
                syncPatternToEngine();
                const int startStep = (pianoRollWindow != nullptr)
                    ? pianoRollWindow->content.pianoRoll.getStartStep()
                    : pianoRollStartStep;
                pianoRollStartStep = juce::jmax(0, startStep);
                audioEngine.play(pianoRollStartStep, project.songStartBar);
            };
            pianoRollWindow->content.pianoRoll.onStartStepSelected = [this](int stepZeroBased)
            {
                pianoRollStartStep = juce::jmax(0, stepZeroBased);
                markDirty();
            };
            pianoRollWindow->content.onStepCountChanged = [this](int newCount)
            {
                channelRack.setStepCountFromExternal(newCount);
            };
            pianoRollWindow->content.onBPMChanged = [this](double bpm)
            {
                project.bpm = bpm;
                toolbar.setBPM(bpm);
                audioEngine.setBPM(bpm);
                markDirty();
            };

            // Punch-in detection: piano roll checks if engine is already running
            pianoRollWindow->content.pianoRoll.isPlayingCallback = [this]
            {
                return audioEngine.isPlaying();
            };
            pianoRollWindow->content.onKeySignatureChanged = [this](const KeySignature& key)
            {
                project.keySignature = key;
                markDirty();
            };
            pianoRollWindow->content.onEnsureBassChannel = [this]() -> int
            {
                return ensureAutoBassChannel();
            };
            pianoRollWindow->content.onBasslineApplied = [this](int ch)
            {
                focusPianoRollChannel(ch);
                syncPatternToEngine();
                audioEngine.updatePatternSnapshot();
                markDirty();
            };
            pianoRollWindow->content.onExportMidi = [this] { exportCurrentPianoRollToMidi(); };
            pianoRollWindow->content.onImportMidi = [this] { importCurrentPianoRollFromMidi(); };

            // Loop MIDI recording: enable/disable engine recording when REC state changes
            pianoRollWindow->content.pianoRoll.onRecordingStateChanged = [this]
            {
                if (pianoRollWindow == nullptr) return;
                const bool nowRecording = pianoRollWindow->content.pianoRoll.getRecState()
                                          == PianoRollComponent::RecState::Recording;
                loopRecordEnabled_ = nowRecording;

                if (nowRecording)
                {
                    if (auto* pat = findPattern(activePatternId))
                        audioEngine.setLoopMarkers(0.0, pat->stepCount * 0.25);
                    audioEngine.enableLoopRecord(true);
                    // Preserve the triggering note: inject currently-held pitches
                    // into liveNotes_ at beat 0 so the first note is not lost.
                    audioEngine.injectFirstLoopNotes(pianoRollChannel);
                }
                else
                {
                    audioEngine.enableLoopRecord(false);
                    loopRecordEnabled_ = false;

                    // Drain any remaining committed PianoRoll notes immediately
                    AudioEngine::CommittedNote cn;
                    bool anyAdded = false;
                    while (audioEngine.drainCommittedNote(cn))
                    {
                        const int targetPatId = (cn.patternId >= 0) ? cn.patternId : activePatternId;
                        if (auto* pat = findPattern(targetPatId))
                        {
                            const int varIdx = juce::jlimit(0, Pattern::kMaxVariations - 1, cn.variation);
                            pat->variations[(size_t)varIdx].notes[(size_t)cn.channel].push_back(cn.note);
                            anyAdded = true;
                        }
                    }
                    if (anyAdded)
                    {
                        audioEngine.updatePatternSnapshot();
                        if (pianoRollWindow != nullptr)
                            pianoRollWindow->content.pianoRoll.repaint();
                        markDirty();
                    }
                }
            };
        }

        pianoRollWindow->setVisible(true);
        pianoRollWindow->toFront(true);
    };

    channelRack.onChannelTypeChanged = [this](int ch, ChannelType t)
    {
        if (auto* pat = findPattern(activePatternId))
        {
            pat->channelTypes[ch] = t;

            juce::String newName;
            if (t == ChannelType::Melodic)
            {
                // Save the current name so we can restore it if the user reverts to Drum
                preTypeChangeName_[(size_t)ch] = pat->channelNames[ch];

                int melIdx = 0;
                for (int i = 0; i <= ch && i < Pattern::kMaxChannels; ++i)
                    if (pat->channelTypes[i] == ChannelType::Melodic)
                        ++melIdx;

                newName = "Mel " + juce::String(melIdx);
            }
            else  // reverting to Drum
            {
                newName = preTypeChangeName_[(size_t)ch].isNotEmpty()
                          ? preTypeChangeName_[(size_t)ch]
                          : "Channel " + juce::String(ch + 1);
                preTypeChangeName_[(size_t)ch] = {};
            }

            pat->channelNames[ch] = newName;
            channelRack.setChannelName(ch, newName);

            const int displayedCh = audioEngine.getMidiTargetChannel();
            if (displayedCh == ch || displayedCh < 0)
                inspectorTabBar_.setInstrumentSub(ch, newName);

            // Refresh instrument panel if this is the channel currently displayed
            if (ch == juce::jmax(0, displayedCh))
            {
                const bool hp = audioEngine.hasPlugin(ch);
                instrumentPanel_.setChannel(ch, newName,
                                            pat->samplePaths[(size_t)ch],
                                            pat->synthParams[(size_t)ch],
                                            pat->channelPitch[(size_t)ch],
                                            t,
                                            hp,
                                            hp ? audioEngine.getPlugin(ch)->getName() : juce::String{});
                if (inspectorTab_ == 0)
                    instrumentPanel_.setSize(
                        instrumentViewport_.getWidth() - instrumentViewport_.getScrollBarThickness(),
                        instrumentPanel_.getNeededHeight());
                if (t == ChannelType::Melodic && !hp)
                    instrumentPanel_.setAvailablePresets(
                        SynthPresets::mergeFactoryAndCustom(project.customSynthPresets),
                        pat->synthParams[(size_t)ch].presetName);
                resized();
            }
        }
        channelRack.setChannelType(ch, t);
        markDirty();
    };

    // Pattern Variation (A/B/C/D) -- save current variation, switch, reload
    channelRack.onVariationChanged = [this](int prevIdx, int newIdx)
    {
        // Save current step edits to the OLD variation before switching
        if (auto* pat = findPattern(activePatternId))
            channelRack.saveToPattern(*pat, prevIdx);
        // Tell AudioEngine which variation to use for pattern mode playback
        audioEngine.setActiveVariation(newIdx);
        audioEngine.updatePatternSnapshot();
        // Reload the newly selected variation into the UI
        if (auto* pat = findPattern(activePatternId))
            channelRack.loadPattern(*pat, newIdx);
        // Update piano roll if open
        if (pianoRollWindow != nullptr && pianoRollWindow->isVisible() && pianoRollChannel >= 0)
        {
            if (auto* pat = findPattern(activePatternId))
            {
                pianoRollWindow->content.pianoRoll.variationIdx = newIdx;
                pianoRollWindow->content.setPattern(pat, pianoRollChannel, project.bpm);
                pianoRollStartStep = juce::jlimit(0, pat->stepCount - 1, pianoRollStartStep);
                pianoRollWindow->content.setStartStep(pianoRollStartStep);
            }
        }
    };

    // ---- M13: Synth Editor
    channelRack.onOpenSynthEditor = [this](int ch)
    {
        switchToPatternModeForEditing();
        // Kill any voices that survived the mode switch (e.g. we were already
        // in Pattern mode so switchToPatternModeForEditing() returned early).
        audioEngine.allSynthNotesOff();
        synthEditorChannel = ch;

        if (synthEditorWindow == nullptr)
        {
            synthEditorWindow = std::make_unique<SynthEditorWindow>();
            synthEditorWindow->centreWithSize(700, 790);
        }

        synthEditorWindow->setChannelName(
            juce::String(ch + 1));   // simple "1"-based label for now

        {
            auto* pat = findPattern(activePatternId);
            refreshSynthEditorPresetList(pat != nullptr ? pat->synthParams[(size_t)ch].presetName : juce::String{});
            if (pat != nullptr)
            {
                // One-time migration: if this Sampler channel still carries the
                // generic synth defaults, upgrade to neutral sampler params so the
                // sample plays cleanly and the snapshot reflects the correction.
                if (pat->channelSourceTypes[(size_t)ch] == ChannelSourceType::Sampler
                    && looksLikeUntouchedSynthDefaults(pat->synthParams[(size_t)ch]))
                {
                    pat->synthParams[(size_t)ch] = samplerNeutralSynthParams();
                    audioEngine.updatePatternSnapshot();
                    markDirty();
                }

                synthEditorWindow->panel.loadParams(pat->synthParams[(size_t)ch]);
                synthEditorWindow->panel.loadSamplerData(pat->channelSourceTypes[(size_t)ch],
                                                        pat->samplerParams[(size_t)ch]);
                // Provide the sample buffer so the editor can render a waveform preview.
                if (pat->channelSourceTypes[(size_t)ch] == ChannelSourceType::Sampler)
                    synthEditorWindow->panel.setSamplerPreviewBuffer(
                        audioEngine.getSamplerSourceBuffer(ch));
                else
                    synthEditorWindow->panel.setSamplerPreviewBuffer(nullptr);
            }
        }

        synthEditorWindow->panel.onParamsChanged = [this, ch]
        {
            if (auto* pat = findPattern(activePatternId))
            {
                synthEditorWindow->panel.applyToParams(pat->synthParams[(size_t)ch]);
                pat->channelSourceTypes[(size_t)ch] = synthEditorWindow->panel.getSourceType();
                pat->samplerParams     [(size_t)ch] = synthEditorWindow->panel.getSamplerParams();

                // If source type is Sampler and a path is set, load the buffer
                if (pat->channelSourceTypes[(size_t)ch] == ChannelSourceType::Sampler)
                {
                    const juce::String path = pat->samplerParams[(size_t)ch].samplePath;
                    if (path.isNotEmpty())
                    {
                        audioEngine.loadSamplerSource(ch, juce::File(path));
                        // Feed the loaded buffer to the editor so it can render a live preview.
                        synthEditorWindow->panel.setSamplerPreviewBuffer(
                            audioEngine.getSamplerSourceBuffer(ch));
                    }
                    else
                    {
                        synthEditorWindow->panel.setSamplerPreviewBuffer(nullptr);
                    }
                }
            }
            audioEngine.updatePatternSnapshot();
            markDirty();
        };
        synthEditorWindow->panel.onPreviewRequested = [this, ch](const SynthParams& params, int midiPitch)
        {
            audioEngine.previewSynthNote(ch, midiPitch, params);
        };
        synthEditorWindow->panel.onStopPreviewRequested = [this, ch]
        {
            audioEngine.stopEditorPreview(ch);
        };
        synthEditorWindow->panel.onIsPreviewActive = [this, ch]() -> bool
        {
            return audioEngine.isEditorPreviewActive(ch);
        };
        synthEditorWindow->panel.onSavePresetRequested = [this](const SynthParams& params)
        {
            auto dialog = std::make_shared<juce::AlertWindow>("Save Synth Preset",
                                                              "Enter a preset name for the current synth settings.",
                                                              juce::MessageBoxIconType::NoIcon);
            dialog->addTextEditor("name", "My Preset");
            dialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, dialog, params](int result)
            {
                if (result != 1)
                    return;

                auto presetName = dialog->getTextEditorContents("name").trim();
                if (presetName.isEmpty())
                    return;

                const auto factoryPresets = SynthPresets::getAll();
                const bool conflictsWithFactory = std::any_of(factoryPresets.begin(), factoryPresets.end(),
                                                              [&presetName](const SynthPresets::Preset& preset)
                                                              {
                                                                  return preset.name.equalsIgnoreCase(presetName);
                                                              });
                if (conflictsWithFactory)
                    presetName << " Custom";

                auto existing = std::find_if(project.customSynthPresets.begin(),
                                             project.customSynthPresets.end(),
                                             [&presetName](const SynthPresets::Preset& preset)
                                             {
                                                 return preset.name.equalsIgnoreCase(presetName);
                                             });

                if (existing != project.customSynthPresets.end())
                    existing->params = params;
                else
                    project.customSynthPresets.push_back({ presetName, params });

                refreshSynthEditorPresetList(presetName);
                markDirty();
            }));
        };
        synthEditorWindow->panel.onRenamePresetRequested = [this](const juce::String& oldName)
        {
            auto dialog = std::make_shared<juce::AlertWindow>("Rename Synth Preset",
                                                              "Enter a new name for the selected custom preset.",
                                                              juce::MessageBoxIconType::NoIcon);
            dialog->addTextEditor("name", oldName);
            dialog->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, dialog, oldName](int result)
            {
                if (result != 1)
                    return;

                auto newName = dialog->getTextEditorContents("name").trim();
                if (newName.isEmpty())
                    return;

                const auto factoryPresets = SynthPresets::getAll();
                const bool conflictsWithFactory = std::any_of(factoryPresets.begin(), factoryPresets.end(),
                                                              [&newName](const SynthPresets::Preset& preset)
                                                              {
                                                                  return preset.name.equalsIgnoreCase(newName);
                                                              });
                if (conflictsWithFactory)
                    newName << " Custom";

                auto existing = std::find_if(project.customSynthPresets.begin(),
                                             project.customSynthPresets.end(),
                                             [&oldName](const SynthPresets::Preset& preset)
                                             {
                                                 return preset.name == oldName;
                                             });
                if (existing == project.customSynthPresets.end())
                    return;

                auto conflictingCustom = std::find_if(project.customSynthPresets.begin(),
                                                      project.customSynthPresets.end(),
                                                      [&newName, &oldName](const SynthPresets::Preset& preset)
                                                      {
                                                          return preset.name.equalsIgnoreCase(newName)
                                                              && preset.name != oldName;
                                                      });
                if (conflictingCustom != project.customSynthPresets.end())
                    newName << " Copy";

                existing->name = newName;
                refreshSynthEditorPresetList(newName);
                markDirty();
            }));
        };
        synthEditorWindow->panel.onDeletePresetRequested = [this](const juce::String& presetName)
        {
            auto dialog = std::make_shared<juce::AlertWindow>("Delete Synth Preset",
                                                              "Delete the selected custom preset?",
                                                              juce::MessageBoxIconType::WarningIcon);
            dialog->addButton("Delete", 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->enterModalState(true, juce::ModalCallbackFunction::create([this, dialog, presetName](int result)
            {
                juce::ignoreUnused(dialog);
                if (result != 1)
                    return;

                project.customSynthPresets.erase(
                    std::remove_if(project.customSynthPresets.begin(),
                                   project.customSynthPresets.end(),
                                   [&presetName](const SynthPresets::Preset& preset)
                                   {
                                       return preset.name == presetName;
                                   }),
                    project.customSynthPresets.end());

                refreshSynthEditorPresetList();
                markDirty();
            }));
        };

        synthEditorWindow->setVisible(true);
        synthEditorWindow->toFront(true);
    };

    // ---- M8: VST/AU Plugin hosting

    PluginManager::getInstance().initialise();

    channelRack.onLoadPlugin = [this](int ch)
    {
        if (pluginBrowserWindow == nullptr)
        {
            pluginBrowserWindow = std::make_unique<PluginBrowserWindow>();

            pluginBrowserWindow->onPluginSelected = [this](int targetCh,
                                                             const juce::PluginDescription& desc)
            {
                juce::String errorMsg;
                audioEngine.loadPlugin(targetCh, desc, errorMsg);

                if (errorMsg.isNotEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Plugin Load Error", errorMsg);
                    return;
                }

                // Save to current pattern's plugin slots
                if (auto* pat = findPattern(activePatternId))
                {
                    pat->pluginSlots[(size_t)targetCh].pluginId =
                        desc.createIdentifierString();
                    pat->pluginSlots[(size_t)targetCh].enabled = true;
                }
                // Also update project-level for serialization backward compat
                project.channelInstrumentPlugins[(size_t)targetCh].pluginId =
                    desc.createIdentifierString();
                project.channelInstrumentPlugins[(size_t)targetCh].enabled = true;
                channelRack.setChannelHasPlugin(targetCh, true);
                audioEngine.updatePatternSnapshot();
                markDirty();
                
                const juce::String pluginChName = "Plugin " + juce::String(targetCh + 1);
                if (auto* pat2 = findPattern(activePatternId))
                    pat2->channelNames[targetCh] = pluginChName;
                channelRack.setChannelName(targetCh, pluginChName);
                inspectorTabBar_.setInstrumentSub(targetCh, pluginChName);
                
                if (auto* pat = findPattern(activePatternId))
                {
                    const bool hp = audioEngine.hasPlugin(targetCh);
                    instrumentPanel_.setChannel(targetCh,
                                                channelRack.getChannelName(targetCh),
                                                pat->samplePaths[(size_t)targetCh],
                                                pat->synthParams[(size_t)targetCh],
                                                pat->channelPitch[(size_t)targetCh],
                                                pat->channelTypes[(size_t)targetCh],
                                                hp,
                                                hp ? audioEngine.getPlugin(targetCh)->getName() : juce::String{});
                    instrumentPanel_.buildVstKnobs(audioEngine.getPlugin(targetCh));
                    if (inspectorTab_ == 0)
                        instrumentPanel_.setSize(
                            instrumentViewport_.getWidth() - instrumentViewport_.getScrollBarThickness(),
                            instrumentPanel_.getNeededHeight());
                }

                inspectorTabBar_.setTab(0);
                if (inspectorTabBar_.onTabChanged)
                    inspectorTabBar_.onTabChanged(0);
                

            };
        }

        pluginBrowserWindow->showForChannel(ch);
    };

    channelRack.onOpenPluginEditor = [this](int ch)
    {
        auto* plugin = audioEngine.getPlugin(ch);
        if (plugin == nullptr) return;

        if (pluginEditorWindows[(size_t)ch] != nullptr
            && pluginEditorWindows[(size_t)ch]->isVisible())
        {
            pluginEditorWindows[(size_t)ch]->toFront(true);
            return;
        }

        pluginEditorWindows[(size_t)ch] =
            std::make_unique<PluginEditorWindow>(*plugin, ch);
        pluginEditorWindows[(size_t)ch]->setVisible(true);
    };

    channelRack.onRemovePlugin = [this](int ch)
        {
        if (pluginEditorWindows[(size_t)ch] != nullptr)
            pluginEditorWindows[(size_t)ch].reset();
        instrumentPanel_.clearVstKnobs();

        audioEngine.unloadPlugin(ch);
        project.channelInstrumentPlugins[(size_t)ch] = {};
        if (auto* pat = findPattern(activePatternId))
            pat->pluginSlots[(size_t)ch] = PluginSlot{};
        channelRack.setChannelHasPlugin(ch, false);

        // 채널 이름 복구
        const juce::String defaultName = "noname";
        if (auto* pat = findPattern(activePatternId))
            pat->channelNames[ch] = defaultName;
        channelRack.setChannelName(ch, defaultName);
        inspectorTabBar_.setInstrumentSub(ch, defaultName);

        // InstrumentPanel을 MelSynth 모드로 복구
        if (auto* pat = findPattern(activePatternId))
        {
            instrumentPanel_.setChannel(
                ch,
                defaultName,
                pat->samplePaths[(size_t)ch],
                pat->synthParams[(size_t)ch],
                pat->channelPitch[(size_t)ch],
                pat->channelTypes[(size_t)ch],
                false,
                {});
            if (inspectorTab_ == 0)
                instrumentPanel_.setSize(
                    instrumentViewport_.getWidth() - instrumentViewport_.getScrollBarThickness(),
                    instrumentPanel_.getNeededHeight());
        }

        audioEngine.updatePatternSnapshot();
        markDirty();
    };

    // Instrument panel "Open VST Editor" button
    instrumentPanel_.onOpenPluginEditor = [this]
    {
        const int ch = audioEngine.getMidiTargetChannel();
        auto* plugin = audioEngine.getPlugin(ch);
        if (plugin == nullptr) return;

        if (pluginEditorWindows[(size_t)ch] != nullptr
            && pluginEditorWindows[(size_t)ch]->isVisible())
        {
            pluginEditorWindows[(size_t)ch]->toFront(true);
            return;
        }

        pluginEditorWindows[(size_t)ch] =
            std::make_unique<PluginEditorWindow>(*plugin, ch);
        pluginEditorWindows[(size_t)ch]->setVisible(true);
    };

    // Phase 3 -- channel mixer routing
    channelRack.onChannelRoutingChanged = [this](int ch, int newTrack)
    {
        auto* pat = findPattern(activePatternId);
        if (!pat) return;
        const int oldTrack = pat->channelMixerRouting[ch];
        if (oldTrack == newTrack) return;
        undoManager.perform(new LambdaAction(
            [this, ch, newTrack]() -> bool {
                if (auto* p = findPattern(activePatternId))
                {
                    p->channelMixerRouting[ch] = newTrack;
                    mixer.updateRoutingLabels(p->channelMixerRouting);
                }
                audioEngine.rebuildRuntimeStateFromProject();
                markDirty(); return true;
            },
            [this, ch, oldTrack]() -> bool {
                if (auto* p = findPattern(activePatternId))
                {
                    p->channelMixerRouting[ch] = oldTrack;
                    mixer.updateRoutingLabels(p->channelMixerRouting);
                }
                audioEngine.rebuildRuntimeStateFromProject();
                markDirty(); return true;
            }
        ));
    };

    channelRack.onAddChannel = [this](const juce::String& name)
    {
        if (project.channelCount >= Pattern::kMaxChannels) return;

        // Save current rack state before expanding
        if (auto* pat = findPattern(activePatternId))
            channelRack.saveToPattern(*pat);

        const int newCh = project.channelCount;
        project.channelCount++;

        // Initialise the new slot in every pattern
        for (auto& pat : project.patterns)
        {
            pat.channelNames[newCh]        = name;
            pat.channelTypes[newCh]        = ChannelType::Drum;
            pat.samplePaths[newCh]         = {};
            pat.synthParams[newCh]         = SynthParams{};
            pat.channelSourceTypes[newCh]  = ChannelSourceType::Synth;
            pat.samplerParams[newCh]       = SamplerParams{};
            pat.channelMixerRouting[newCh] = newCh % 8;
            pat.channelVolume[newCh]       = 0.8f;
            pat.channelPan[newCh]          = 0.0f;
            pat.channelPitch[newCh]        = 0.0f;
            for (int v = 0; v < Pattern::kMaxVariations; ++v)
            {
                for (int st = 0; st < Pattern::kMaxSteps; ++st)
                    pat.variations[v].steps[newCh][st] = false;
                pat.variations[v].notes[newCh].clear();
            }
        }

        // Rebuild rack UI and refresh snapshot
        if (auto* activePat = findPattern(activePatternId))
        {
            channelRack.resetToChannelCount(project.channelCount, activePat->channelNames);
            channelRack.loadPattern(*activePat);
        }
        audioEngine.updatePatternSnapshot();
        markDirty();
    };

    channelRack.onDeleteChannel = [this](int ch)
    {
        if (project.channelCount <= 1) return;

        // Save current rack edits into the active pattern first
        if (auto* pat = findPattern(activePatternId))
            channelRack.saveToPattern(*pat);

        // Capture per-pattern channel data for undo
        struct ChSnap
        {
            struct VarData { bool steps[Pattern::kMaxSteps] = {}; std::vector<NoteEvent> notes; };
            juce::String      name;
            ChannelType       type             = ChannelType::Drum;
            juce::String      samplePath;
            SynthParams       synthParams;
            ChannelSourceType sourceType       = ChannelSourceType::Synth;
            SamplerParams     samplerParams;
            int               mixerRoute       = 0;
            float             volume = 0.8f, pan = 0.0f, pitch = 0.0f;
            VarData           varData[Pattern::kMaxVariations];
        };

        auto snap = std::make_shared<std::vector<ChSnap>>();
        for (auto& pat : project.patterns)
        {
            ChSnap s;
            s.name          = pat.channelNames[ch];
            s.type          = pat.channelTypes[ch];
            s.samplePath    = pat.samplePaths[ch];
            s.synthParams   = pat.synthParams[ch];
            s.sourceType    = pat.channelSourceTypes[ch];
            s.samplerParams = pat.samplerParams[ch];
            s.mixerRoute    = pat.channelMixerRouting[ch];
            s.volume      = pat.channelVolume[ch];
            s.pan         = pat.channelPan[ch];
            s.pitch       = pat.channelPitch[ch];
            for (int v = 0; v < Pattern::kMaxVariations; ++v)
            {
                for (int st = 0; st < Pattern::kMaxSteps; ++st)
                    s.varData[v].steps[st] = pat.variations[v].steps[ch][st];
                s.varData[v].notes = pat.variations[v].notes[ch];
            }
            snap->push_back(std::move(s));
        }
        const int oldCount = project.channelCount;
        const auto oldPluginSlots = project.channelInstrumentPlugins;

        auto doDelete = [this, ch]()
        {
            for (auto& pat : project.patterns)
            {
                for (int i = ch; i < project.channelCount - 1; ++i)
                {
                    pat.channelNames[i]        = pat.channelNames[i + 1];
                    pat.channelTypes[i]        = pat.channelTypes[i + 1];
                    pat.samplePaths[i]         = pat.samplePaths[i + 1];
                    pat.synthParams[i]         = pat.synthParams[i + 1];
                    pat.channelSourceTypes[i]  = pat.channelSourceTypes[i + 1];
                    pat.samplerParams[i]       = pat.samplerParams[i + 1];
                    pat.channelMixerRouting[i] = pat.channelMixerRouting[i + 1];
                    pat.channelVolume[i]       = pat.channelVolume[i + 1];
                    pat.channelPan[i]          = pat.channelPan[i + 1];
                    pat.channelPitch[i]        = pat.channelPitch[i + 1];
                    for (int v = 0; v < Pattern::kMaxVariations; ++v)
                    {
                        for (int st = 0; st < Pattern::kMaxSteps; ++st)
                            pat.variations[v].steps[i][st] = pat.variations[v].steps[i + 1][st];
                        pat.variations[v].notes[i] = pat.variations[v].notes[i + 1];
                    }
                }
                const int last = project.channelCount - 1;
                pat.channelNames[last]        = "Channel " + juce::String(last + 1);
                pat.channelTypes[last]        = ChannelType::Drum;
                pat.samplePaths[last]         = {};
                pat.synthParams[last]         = SynthParams{};
                pat.channelSourceTypes[last]  = ChannelSourceType::Synth;
                pat.samplerParams[last]       = SamplerParams{};
                pat.channelMixerRouting[last] = last % 8;
                pat.channelVolume[last]       = 0.8f;
                pat.channelPan[last]          = 0.0f;
                pat.channelPitch[last]        = 0.0f;
                for (int v = 0; v < Pattern::kMaxVariations; ++v)
                {
                    for (int st = 0; st < Pattern::kMaxSteps; ++st)
                        pat.variations[v].steps[last][st] = false;
                    pat.variations[v].notes[last].clear();
                }
                // Shift per-pattern plugin slots
                for (int i = ch; i < project.channelCount - 1; ++i)
                    pat.pluginSlots[(size_t)i] = pat.pluginSlots[(size_t)(i + 1)];
                pat.pluginSlots[(size_t)(project.channelCount - 1)] = PluginSlot{};

                pat.channelCount = juce::jmax(1, project.channelCount - 1);
            }

            for (int i = ch; i < project.channelCount - 1; ++i)
                project.channelInstrumentPlugins[(size_t)i] = project.channelInstrumentPlugins[(size_t)(i + 1)];
            project.channelInstrumentPlugins[(size_t)(project.channelCount - 1)] = {};

            project.channelCount--;
            for (auto& w : pluginEditorWindows) w.reset();
            reloadProjectIntoUI();
            audioEngine.updatePatternSnapshot();
            markDirty();
        };

        auto doRestore = [this, ch, oldCount, snap, oldPluginSlots]()
        {
            project.channelCount = oldCount;
            int patIdx = 0;
            for (auto& pat : project.patterns)
            {
                if (patIdx >= (int)snap->size()) break;
                const auto& s = (*snap)[(size_t)patIdx++];
                // Shift existing channels up to make room
                for (int i = project.channelCount - 1; i > ch; --i)
                {
                    pat.channelNames[i]        = pat.channelNames[i - 1];
                    pat.channelTypes[i]        = pat.channelTypes[i - 1];
                    pat.samplePaths[i]         = pat.samplePaths[i - 1];
                    pat.synthParams[i]         = pat.synthParams[i - 1];
                    pat.channelSourceTypes[i]  = pat.channelSourceTypes[i - 1];
                    pat.samplerParams[i]       = pat.samplerParams[i - 1];
                    pat.channelMixerRouting[i] = pat.channelMixerRouting[i - 1];
                    pat.channelVolume[i]       = pat.channelVolume[i - 1];
                    pat.channelPan[i]          = pat.channelPan[i - 1];
                    pat.channelPitch[i]        = pat.channelPitch[i - 1];
                    for (int v = 0; v < Pattern::kMaxVariations; ++v)
                    {
                        for (int st = 0; st < Pattern::kMaxSteps; ++st)
                            pat.variations[v].steps[i][st] = pat.variations[v].steps[i - 1][st];
                        pat.variations[v].notes[i] = pat.variations[v].notes[i - 1];
                    }
                }
                // Restore the deleted channel at index ch
                pat.channelNames[ch]        = s.name;
                pat.channelTypes[ch]        = s.type;
                pat.samplePaths[ch]         = s.samplePath;
                pat.synthParams[ch]         = s.synthParams;
                pat.channelSourceTypes[ch]  = s.sourceType;
                pat.samplerParams[ch]       = s.samplerParams;
                pat.channelMixerRouting[ch] = s.mixerRoute;
                pat.channelVolume[ch]       = s.volume;
                pat.channelPan[ch]          = s.pan;
                pat.channelPitch[ch]        = s.pitch;
                for (int v = 0; v < Pattern::kMaxVariations; ++v)
                {
                    for (int st = 0; st < Pattern::kMaxSteps; ++st)
                        pat.variations[v].steps[ch][st] = s.varData[v].steps[st];
                    pat.variations[v].notes[ch] = s.varData[v].notes;
                }
                pat.channelCount = oldCount;
            }
            project.channelInstrumentPlugins = oldPluginSlots;
            reloadProjectIntoUI();
            audioEngine.updatePatternSnapshot();
            markDirty();
        };

        undoManager.perform(new LambdaAction(
            [doDelete]() -> bool { doDelete(); return true; },
            [doRestore]() -> bool { doRestore(); return true; }
        ));
    };

    // ---- M5: Mixer
    addAndMakeVisible(mixer);
    mixer.setVisible(false);   // hidden until toolbar toggle
    mixer.loadFromProject(project);
    if (!project.patterns.empty())
        mixer.updateRoutingLabels(project.patterns.front().channelMixerRouting);

    mixer.onTrackVolumeChanged  = [this](int t, float v) { audioEngine.setMixerTrackVolume(t, v); markDirty(); };
    mixer.onTrackPanChanged     = [this](int t, float p) { audioEngine.setMixerTrackPan(t, p);    markDirty(); };
    mixer.onTrackMuteChanged    = [this](int t, bool m)  { audioEngine.setMixerTrackMuted(t, m);  markDirty(); };
    mixer.onTrackSoloChanged    = [this](int t, bool s)  { audioEngine.setMixerTrackSoloed(t, s); markDirty(); };
    mixer.onMasterVolumeChanged = [this](float v)        { audioEngine.setMasterVolume(v);         markDirty(); };
    mixer.onMasterPanChanged    = [this](float p)        { audioEngine.setMasterPan(p);            markDirty(); };

    // M14 -- FX editor per mixer track
    mixer.onFXButtonClicked = [this](int t)
    {
        fxEditorTrack = t;

        if (fxEditorWindow == nullptr)
        {
            fxEditorWindow = std::make_unique<FXEditorWindow>();
            fxEditorWindow->centreWithSize(440, 420);
        }

        fxEditorWindow->setTrackName("Track " + juce::String(t + 1));
        fxEditorWindow->panel.loadParams(project.fxParams[(size_t)t]);

        fxEditorWindow->panel.onParamsChanged = [this, t]
        {
            fxEditorWindow->panel.applyToParams(project.fxParams[(size_t)t]);
            markDirty();
        };

        fxEditorWindow->setVisible(true);
        fxEditorWindow->toFront(true);
    };

    // Auto-Tune editor
    mixer.onAutoTuneButtonClicked = [this](int t)
    {
        autoTuneEditorTrack = t;

        if (autoTuneEditorWindow == nullptr)
        {
            autoTuneEditorWindow = std::make_unique<AutoTuneEditorWindow>();
            autoTuneEditorWindow->centreWithSize(420, 560);
        }

        autoTuneEditorWindow->setTrackName("Track " + juce::String(t + 1));
        autoTuneEditorWindow->panel.loadParams(project.autoTuneParams[(size_t)t]);

        autoTuneEditorWindow->panel.onParamsChanged = [this, t]
        {
            autoTuneEditorWindow->panel.applyToParams(project.autoTuneParams[(size_t)t]);
            audioEngine.rebuildRuntimeStateFromProject();
            markDirty();
        };

        autoTuneEditorWindow->setVisible(true);
        autoTuneEditorWindow->toFront(true);
    };

    // Dynamic EQ windows (0-7 = track, 8 = master)
    mixer.onEQButtonClicked = [this](int t)
    {
        const bool isMaster = (t == MixerComponent::numTracks);
        const juce::String name = isMaster ? "Master" : ("Track " + juce::String(t + 1));

        DynamicEQProcessor& proc = isMaster
            ? audioEngine.getMasterDynEQ()
            : audioEngine.getTrackDynEQ(t);

        const int idx = isMaster ? 8 : t;
        if (dynEQWindows[(size_t)idx] == nullptr)
            dynEQWindows[(size_t)idx] = std::make_unique<DynamicEQWindow>(proc, name);

        dynEQWindows[(size_t)idx]->setVisible(true);
        dynEQWindows[(size_t)idx]->toFront(true);
    };

    toolbar.onToggleMixer = [this]
    {
        showMixer    = !showMixer;
        inspectorTab_ = showMixer ? 2 : 1;   // 1=SEQUENCER, 2=MIXER
        inspectorTabBar_.setTab(inspectorTab_);
        resized();
    };

    // Launchpad -- inline panel callbacks (wired once at construction)
    launchpadPanel.setProject(&project);
    addChildComponent(launchpadPanel);   // hidden initially

    launchpadPanel.onPadTriggered = [this](int padIdx)
    {
        audioEngine.triggerLaunchpadPad(padIdx);
    };

    launchpadPanel.onPadStopped = [this](int padIdx)
    {
        audioEngine.stopLaunchpadPad(padIdx);
    };

    launchpadPanel.onStopAll = [this]()
    {
        audioEngine.stopAllLaunchpadPads();
    };

    launchpadPanel.onClearAll = [this]()
    {
        for (int i = 0; i < 64; ++i)
            audioEngine.unloadLaunchpadSample(i);
        markDirty();
    };

    launchpadPanel.onDefaultsLoaded = [this]()
    {
        for (int i = 0; i < 64; ++i)
        {
            const auto& pad = project.launchpadPads[(size_t)i];
            if (pad.filePath.isNotEmpty())
                audioEngine.loadLaunchpadSample(i, juce::File(pad.filePath));
            else
                audioEngine.unloadLaunchpadSample(i);
        }
    };

    launchpadPanel.onSampleDropped = [this](int padIdx, juce::File file)
    {
        audioEngine.loadLaunchpadSample(padIdx, file);
        markDirty();
    };

    launchpadPanel.onConvertToPattern =
        [this](const std::vector<LaunchpadPanel::RecordedHit>& hits, int numBars)
    {
        auto* dialog = new juce::AlertWindow("Save as Pattern", "Pattern name:",
                                              juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("name", "Pad Recording");
        dialog->addButton("OK",     1);
        dialog->addButton("Cancel", 0);

        auto hitsCopy = hits;
        dialog->enterModalState(true,
            juce::ModalCallbackFunction::create(
                [this, hitsCopy, numBars, dialog](int result)
            {
                if (result == 1)
                {
                    juce::String patName = dialog->getTextEditorContents("name").trim();
                    if (patName.isEmpty()) patName = "Pad Recording";

                    Pattern newPat;
                    newPat.id         = nextPatternId();
                    newPat.name       = patName;
                    newPat.stepCount  = numBars * 16;
                    newPat.lengthBars = numBars;

                    std::map<int, int> padToChannel;
                    int nextCh = 0;
                    for (const auto& hit : hitsCopy)
                    {
                        if (padToChannel.count(hit.padIdx) == 0 &&
                            nextCh < Pattern::kMaxChannels)
                        {
                            padToChannel[hit.padIdx] = nextCh;
                            const auto fp = project.launchpadPads[(size_t)hit.padIdx].filePath;
                            if (fp.isNotEmpty())
                            {
                                newPat.samplePaths[nextCh] = fp;
                                audioEngine.loadSample(nextCh, juce::File(fp));
                            }
                            ++nextCh;
                        }
                    }

                    for (const auto& hit : hitsCopy)
                    {
                        auto it = padToChannel.find(hit.padIdx);
                        if (it == padToChannel.end()) continue;
                        const int ch   = it->second;
                        const int step = (int)std::round(hit.beatPos / 0.25)
                                         % newPat.stepCount;
                        if (step >= 0 && step < newPat.stepCount)
                            newPat.variations[0].steps[ch][step] = true;
                    }

                    project.patterns.push_back(newPat);
                    toolbar.updatePatternList(project.patterns, newPat.id);
                    selectPattern(newPat.id);
                    markDirty();

                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        "Pattern Created",
                        "\"" + newPat.name + "\" added with "
                        + juce::String(nextCh) + " channel(s).");
                }
                delete dialog;
            }), false);
    };

    launchpadPanel.getBPM = [this]() { return project.bpm; };

    // Launchpad toggle -- show/hide inline panel
    toolbar.onToggleLaunchpad = [this]
    {
        showLaunchpad = !showLaunchpad;
        resized();
    };

    // M12 -- MIDI device selection popup
    toolbar.onAudioButton = [this]
    {
        if (audioDeviceWindow == nullptr)
        {
            audioDeviceWindow = std::make_unique<AudioSettingsWindow>(audioEngine.getAudioDeviceManager());
        }

        audioDeviceWindow->setVisible(true);
        audioDeviceWindow->toFront(true);
    };

    toolbar.onMidiButton = [this]
    {
        const auto devices = audioEngine.getMidiInputDevices();
        const juce::String currentId = audioEngine.getOpenMidiDeviceId();

        juce::PopupMenu menu;

        // Device list
        menu.addSectionHeader("MIDI Input Device");
        menu.addItem(1, "(None)", true, currentId.isEmpty());
        for (int i = 0; i < devices.size(); ++i)
            menu.addItem(100 + i, devices[i].name, true, devices[i].identifier == currentId);

        // Target channel submenu
        juce::PopupMenu chanMenu;
        for (int ch = 0; ch < 16; ++ch)
            chanMenu.addItem(200 + ch, "Ch " + juce::String(ch + 1));
        menu.addSeparator();
        menu.addSubMenu("MIDI Target Channel", chanMenu);

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(nullptr),
            [this, devices](int result)
            {
                if (result == 1)
                {
                    audioEngine.closeMidiDevice();
                }
                else if (result >= 100 && result < 100 + devices.size())
                {
                    audioEngine.openMidiDevice(devices[result - 100].identifier);
                }
                else if (result >= 200 && result < 216)
                {
                    audioEngine.setMidiTargetChannel(result - 200);
                    audioEngine.updatePatternSnapshot();
                }
            });
    };

    // Trackpad multitouch -> launchpad pads 0-15
    toolbar.onToggleTrackpad = [this]
    {
        if (trackpadController.isRunning())
        {
            trackpadController.stop();
        }
        else
        {
            trackpadController.onPadEvent = [this](int padIdx, float /*velocity*/, bool isNoteOn)
            {
                if (isNoteOn)
                {
                    audioEngine.triggerLaunchpadPad(padIdx);
                }
                else
                {
                    if (padIdx >= 0 && padIdx < 64
                        && project.launchpadPads[(size_t)padIdx].playMode == PadPlayMode::Gate)
                        audioEngine.stopLaunchpadPad(padIdx);
                }
            };
            trackpadController.start();
        }
    };

    setWantsKeyboardFocus(true);

    // Sync engine with the initial pattern so previewNote / synth works before first Play
    audioEngine.setActivePattern(activePatternId);
    audioEngine.updatePatternSnapshot();

    // -- Live Performance: ClipLauncher wiring --------------------------------
    clipLauncher_.setQuantizeBeats(4.0);  // 1 bar quantize by default

    // Default pad->channel mapping: pad N -> channel N (first 16 pads)
    if (!project.patterns.empty())
    {
        const int firstPatId = project.patterns.front().id;
        for (int pad = 0; pad < 16 && pad < (int)project.patterns.size(); ++pad)
            clipLauncher_.setClip(pad, pad, project.patterns[(size_t)pad].id, 0);
        (void)firstPatId;
    }

    clipLauncher_.onClipLaunched = [this](int channel, int patternId, int variationIdx)
    {
        // Stop synth voices on this channel cleanly before switching its pattern
        audioEngine.allSynthNotesOff();
        audioEngine.setChannelPattern(channel, patternId, variationIdx);
    };

    clipLauncher_.onClipStopped = [this](int channel)
    {
        audioEngine.liveLoopStop(channel);
        audioEngine.setChannelPattern(channel, -1, 0);
    };

    // When a pad is queued: arm that channel for live loop recording
    clipLauncher_.onClipQueued = [this](int channel)
    {
        audioEngine.setMidiTargetChannel(channel);
        if (audioEngine.liveLoopGetState(channel) == LiveLoopEngine::State::Idle)
            audioEngine.liveLoopArm(channel, 16.0);
    };

    // Pads are NOT intercepted as clip triggers -- they play drum sounds normally.
    // (setPadRange disabled: no pad range set, so all notes go to instrument routing)
    // -------------------------------------------------------------------------

    // -- Live Loop Window setup -----------------------------------------------
    liveLoopWindow_ = std::make_unique<LiveLoopWindow>(audioEngine);
    auto& lw = liveLoopWindow_->content;

    lw.getSelectedChannel = [this]() { return audioEngine.getMidiTargetChannel(); };

    lw.getChannelName = [](int ch) -> juce::String
    {
        return "Track " + juce::String(ch + 1);
    };

    lw.getInstrumentName = [this](int ch) -> juce::String
    {
        return audioEngine.getLiveChannelInstrumentName(ch);
    };

    lw.getCurrentBeat = [this]() { return audioEngine.liveLoopGetGlobalBeat(); };
    lw.getBpm         = [this]() { return audioEngine.getBPM(); };

    lw.onChannelSelected = [this](int ch)
    {
        audioEngine.setMidiTargetChannel(ch);
        channelRack.setSelectedMidiChannel(ch);
    };

    lw.onArmChannel = [this](int /*ch*/, double loopBeats)
    {
        const int ch = audioEngine.getMidiTargetChannel();
        audioEngine.liveLoopArm(ch, loopBeats);
    };

    lw.onStopChannel = [this](int ch)
    {
        audioEngine.liveLoopStop(ch);
    };

    lw.onOverdubChannel = [this](int ch)
    {
        audioEngine.liveLoopOverdub(ch);
    };

    lw.onUndoChannel = [this](int ch)
    {
        audioEngine.liveLoopUndo(ch);
    };

    lw.onHalfLoop = [this](int ch)
    {
        audioEngine.liveLoopHalfLoop(ch);
    };

    lw.onDoubleLoop = [this](int ch)
    {
        audioEngine.liveLoopDoubleLoop(ch);
    };

    lw.onLaunchAll = [this](double loopBeats)
    {
        audioEngine.liveLoopLaunchAll(loopBeats);
    };

    lw.onArmFreeChannel = [this](int ch)
    {
        audioEngine.liveLoopArmFree(ch);
    };

    lw.onMuteChannel = [this](int ch, bool muted)
    {
        audioEngine.liveLoopSetMute(ch, muted);
    };

    lw.onVolumeChanged = [this](int ch, float v)
    {
        audioEngine.liveLoopSetVolume(ch, v);
    };

    lw.onStopAll = [this]
    {
        audioEngine.liveLoopResetAll();
    };

    lw.onBpmChanged = [this](double bpm)
    {
        audioEngine.setBPM(bpm);
    };

    lw.onQuantizeChanged = [this](double stepBeats)
    {
        audioEngine.liveLoopSetQuantize(stepBeats);
    };

    lw.onMetronomeToggle = [this](bool on)
    {
        audioEngine.setMetronomeEnabled(on);
    };

    lw.onCountInChanged = [this](int bars)
    {
        audioEngine.liveLoopSetCountInBars(bars);
    };

    lw.onSnapForwardChanged = [this](bool fwd)
    {
        audioEngine.liveLoopSetSnapForward(fwd);
    };

    lw.onSampleDropped = [this](int ch, const juce::File& file)
    {
        audioEngine.loadSamplerSource(ch, file);
        // Also select this channel so MIDI targets it immediately
        audioEngine.setMidiTargetChannel(ch);
    };

    // Apply defaults immediately
    audioEngine.liveLoopSetQuantize(0.25);
    audioEngine.liveLoopSetCountInBars(1);
    audioEngine.liveLoopSetSnapForward(false);  // default: nearest

    // -- Window close: stop all live loops and sync toolbar state ------------
    liveLoopWindow_->onClose = [this]
    {
        liveMode_ = false;
        toolbar.setLiveModeActive(false);
        audioEngine.liveLoopResetAll();
    };

    // -- Live Performance Mode toggle (wired to toolbar LIVE button) ---------
    toolbar.onToggleLiveMode = [this]
    {
        liveMode_ = !liveMode_;
        toolbar.setLiveModeActive(liveMode_);

        if (liveMode_)
        {
            liveLoopWindow_->setVisible(true);
            liveLoopWindow_->toFront(true);
        }
        else
        {
            liveLoopWindow_->setVisible(false);
            audioEngine.liveLoopResetAll();
        }
    };

    toolbar.onMasterVolChanged = [this](double v)
    {
        audioEngine.setMasterVolume((float)v);
    };

    toolbar.onRewind      = [this] {
        pausedBarSong = -1.0; pausedPatternBeat = -1.0;
        audioEngine.stop();
        channelRack.setPlaybackStep(-1);
        playlist.setPlayheadBar(-1.0);
    };
    toolbar.onFastForward = [this] { /* fast-forward: stub */ };
    toolbar.onToggleLoop  = [this]
    {
        loopRecordEnabled_ = !loopRecordEnabled_;
        toolbar.setLoopEnabled(loopRecordEnabled_);
    };
    // -------------------------------------------------------------------------

    startTimerHz(30);
    setSize(1600, 900);
}

MainComponent::~MainComponent()
{
    trackpadController.stop();
    stopTimer();

    // Release static PropertiesFile instances before JUCE's LeakDetector fires.
    SampleBrowserComponent::closeSettings();
    LaunchpadPanel::closeSettings();

    // Explicitly destroy all floating windows before engine shutdown.
    // They may hold lambdas that reference engine/project data; destroying
    // them first avoids dangling-reference crashes and JUCE leak false-positives.
    for (auto& w : pluginEditorWindows) w.reset();
    for (auto& w : dynEQWindows)        w.reset();
    pluginBrowserWindow.reset();
    audioDeviceWindow.reset();
    autoTuneEditorWindow.reset();
    fxEditorWindow.reset();
    synthEditorWindow.reset();
    pianoRollWindow.reset();
    liveDebugWindow_.reset();
    liveLoopWindow_.reset();

    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    audioEngine.shutdown();
}

// ---------------------------------------------------------------------------
// Pattern helpers
// ---------------------------------------------------------------------------

Pattern* MainComponent::findPattern(int id)
{
    for (auto& p : project.patterns)
        if (p.id == id) return &p;
    return nullptr;
}

int MainComponent::nextPatternId() const
{
    int maxId = 0;
    for (const auto& p : project.patterns)
        maxId = juce::jmax(maxId, p.id);
    return maxId + 1;
}

void MainComponent::selectPattern(int id)
{
    // Save current channel rack steps + plugin states into the currently active pattern
    if (auto* cur = findPattern(activePatternId))
    {
        channelRack.saveToPattern(*cur);
        audioEngine.savePluginStatesToSlots(cur->pluginSlots);
    }

    activePatternId = id;
    project.activePatternId = id;
    markDirty();

    // Close all plugin editor windows before swapping plugins
    for (auto& w : pluginEditorWindows) w.reset();

    // Load new pattern into channel rack, samples, and sync steps to engine immediately
    if (auto* newPat = findPattern(activePatternId))
    {
        // Restore per-pattern channel count
        const int patChCount = juce::jlimit(1, Pattern::kMaxChannels, newPat->channelCount);
        project.channelCount = patChCount;
        channelRack.resetToChannelCount(patChCount, newPat->channelNames);
        channelRack.loadPattern(*newPat);

        // Restore per-pattern plugins.
        // In song mode, load the union of all plugins needed by clips (so switching
        // patterns doesn't kill song playback).  In pattern mode, load only the
        // new pattern's plugins.
        if (toolbar.getPlayMode() == PlayMode::Song)
            audioEngine.ensureSongPluginsLoaded();
        else
            audioEngine.restorePluginsFromSlots(newPat->pluginSlots);

        // Badge reflects what the current pattern has, not the audio engine union
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            channelRack.setChannelHasPlugin(ch, newPat->pluginSlots[(size_t)ch].enabled);

        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            if (newPat->samplePaths[ch].isNotEmpty())
                audioEngine.loadSample(ch, juce::File(newPat->samplePaths[ch]));
            else
                audioEngine.unloadSample(ch);

            // Reload sampler source buffer for Sampler channels.
            // loadSamplerSource clears the slot if samplePath is empty.
            if (newPat->channelSourceTypes[ch] == ChannelSourceType::Sampler)
                audioEngine.loadSamplerSource(ch, juce::File(newPat->samplerParams[ch].samplePath));
        }

        // Sync step pattern to engine so it plays correctly immediately
        audioEngine.setPatternStepCount(newPat->stepCount);
        patternStartStep = juce::jlimit(0, newPat->stepCount - 1, patternStartStep);
        pianoRollStartStep = juce::jlimit(0, newPat->stepCount - 1, pianoRollStartStep);
        project.patternStartStep = patternStartStep;
        channelRack.setPatternStartStep(patternStartStep);
        if (pianoRollWindow != nullptr && pianoRollWindow->isVisible())
            pianoRollWindow->content.setStartStep(pianoRollStartStep);
        {
            const int vi = channelRack.activeVariation;
            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
                for (int s = 0; s < newPat->stepCount; ++s)
                    audioEngine.setStepPattern(ch, s, newPat->variations[vi].steps[ch][s]);
        }
    }

    toolbar.updatePatternList(project.patterns, activePatternId);
    audioEngine.setActivePattern(activePatternId);
    audioEngine.updatePatternSnapshot();

    // Phase-6: update inspector tab sub-label with current pattern name
    if (auto* pat = findPattern(activePatternId))
        inspectorTabBar_.setSequencerSub(pat->name);

    // Update open piano roll to the newly selected pattern
    if (pianoRollWindow != nullptr && pianoRollWindow->isVisible() && pianoRollChannel >= 0)
        if (auto* pat = findPattern(activePatternId))
        {
            pianoRollWindow->content.setPattern(pat, pianoRollChannel, project.bpm);
            pianoRollWindow->content.setBPM(project.bpm);
            pianoRollStartStep = juce::jlimit(0, pat->stepCount - 1, pianoRollStartStep);
            pianoRollWindow->content.setStartStep(pianoRollStartStep);
            pianoRollWindow->content.setKeySignature(project.keySignature);
        }
}

void MainComponent::syncPatternToEngine()
{
    auto* pat = findPattern(activePatternId);
    if (pat == nullptr) return;

    // Flush channel rack UI -> pattern model
    channelRack.saveToPattern(*pat);

    audioEngine.setActivePattern(activePatternId);   // ensure NoteEvent loop uses current pattern

    if (toolbar.getPlayMode() == PlayMode::Pattern)
    {
        audioEngine.setPatternStepCount(pat->stepCount);
        patternStartStep = juce::jlimit(0, pat->stepCount - 1, patternStartStep);
        project.patternStartStep = patternStartStep;
        const int vi = channelRack.activeVariation;
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            for (int s = 0; s < pat->stepCount; ++s)
                audioEngine.setStepPattern(ch, s, pat->variations[vi].steps[ch][s]);
    }

    audioEngine.updatePatternSnapshot();
}

// ---------------------------------------------------------------------------
// M4 -- File operations
// ---------------------------------------------------------------------------

void MainComponent::markDirty()
{
    projectDirty = true;
    
    juce::String title = "Untitled";
    if (currentFile != juce::File())
        title = currentFile.getFileNameWithoutExtension();
        
    toolbar.setProjectTitle(title, true);
}

void MainComponent::exportCurrentPianoRollToMidi()
{
    if (pianoRollChannel < 0) return;
    auto* pat = findPattern(activePatternId);
    if (pat == nullptr) return;
    const int exportPatternId = activePatternId;
    const int exportChannel = pianoRollChannel;

    const auto channelName = pat->channelNames[(size_t)exportChannel].isNotEmpty()
        ? pat->channelNames[(size_t)exportChannel]
        : ("Channel " + juce::String(exportChannel + 1));
    const auto defaultName = juce::File::createLegalFileName(
        pat->name + " - " + channelName + ".mid");

    fileChooser = std::make_shared<juce::FileChooser>(
        "Export Piano Roll Notes",
        currentFile.existsAsFile() ? currentFile.getParentDirectory().getChildFile(defaultName)
                                   : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                         .getChildFile(defaultName),
        "*.mid");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this, exportPatternId, exportChannel](const juce::FileChooser& fc)
        {
            auto chosen = fc.getResult();
            if (chosen == juce::File()) return;

            auto* exportPat = findPattern(exportPatternId);
            if (exportPat == nullptr) return;

            juce::MidiMessageSequence sequence;
            sequence.addEvent(juce::MidiMessage::tempoMetaEvent(
                juce::roundToInt(60000000.0 / juce::jmax(1.0, project.bpm))), 0.0);

            const int exportVarIdx = channelRack.activeVariation;
            const auto& notes = exportPat->variations[exportVarIdx].notes[(size_t)exportChannel];
            for (const auto& note : notes)
            {
                const double startTick = juce::jmax(0.0, (double)note.startBeat * kMidiPpq);
                const double endTick = juce::jmax(startTick + 1.0,
                                                  (double)(note.startBeat + note.lengthBeats) * kMidiPpq);
                const auto velocity = (juce::uint8)juce::jlimit(1, 127,
                    (int)std::round(juce::jlimit(0.0f, 1.0f, note.velocity) * 127.0f));
                sequence.addEvent(juce::MidiMessage::noteOn(1, juce::jlimit(0, 127, note.pitch), velocity), startTick);
                sequence.addEvent(juce::MidiMessage::noteOff(1, juce::jlimit(0, 127, note.pitch)), endTick);
            }

            sequence.updateMatchedPairs();
            juce::MidiFile midiFile;
            midiFile.setTicksPerQuarterNote(kMidiPpq);
            midiFile.addTrack(sequence);

            if (auto stream = std::unique_ptr<juce::FileOutputStream>(chosen.createOutputStream()))
            {
                if (midiFile.writeTo(*stream))
                    return;
            }

            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "MIDI Export Failed",
                "Could not write the MIDI note file.");
        });
}

void MainComponent::importCurrentPianoRollFromMidi()
{
    if (pianoRollChannel < 0) return;
    auto* pat = findPattern(activePatternId);
    if (pat == nullptr) return;
    const int importPatternId = activePatternId;
    const int importChannel = pianoRollChannel;

    fileChooser = std::make_shared<juce::FileChooser>(
        "Import Piano Roll Notes",
        currentFile.existsAsFile() ? currentFile.getParentDirectory()
                                   : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.mid");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this, importPatternId, importChannel](const juce::FileChooser& fc)
        {
            auto chosen = fc.getResult();
            if (!chosen.existsAsFile()) return;

            auto* importPat = findPattern(importPatternId);
            if (importPat == nullptr) return;

            std::unique_ptr<juce::FileInputStream> stream(chosen.createInputStream());
            if (stream == nullptr)
                return;

            juce::MidiFile midiFile;
            if (!midiFile.readFrom(*stream))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "MIDI Import Failed",
                    "Could not read the MIDI note file.");
                return;
            }

            const int timeFormat = midiFile.getTimeFormat();
            const double ticksPerBeat = timeFormat > 0 ? (double)timeFormat : (double)kMidiPpq;

            std::vector<NoteEvent> importedNotes;
            double maxEndBeat = 0.0;

            for (int trackIdx = 0; trackIdx < midiFile.getNumTracks(); ++trackIdx)
            {
                if (auto* srcTrack = midiFile.getTrack(trackIdx))
                {
                    juce::MidiMessageSequence track(*srcTrack);
                    track.updateMatchedPairs();
                    for (int i = 0; i < track.getNumEvents(); ++i)
                    {
                        if (auto* event = track.getEventPointer(i))
                        {
                            if (!event->message.isNoteOn())
                                continue;

                            const auto* off = event->noteOffObject;
                            const double startBeat = juce::jmax(0.0, event->message.getTimeStamp() / ticksPerBeat);
                            const double endBeat = (off != nullptr)
                                ? juce::jmax(startBeat + 0.05, off->message.getTimeStamp() / ticksPerBeat)
                                : (startBeat + 0.25);

                            NoteEvent note;
                            note.pitch = juce::jlimit(0, 127, event->message.getNoteNumber());
                            note.startBeat = (float)startBeat;
                            note.lengthBeats = (float)juce::jmax(0.05, endBeat - startBeat);
                            note.velocity = juce::jlimit(0.0f, 1.0f, event->message.getFloatVelocity());
                            importedNotes.push_back(note);
                            maxEndBeat = juce::jmax(maxEndBeat, endBeat);
                        }
                    }
                }
            }

            std::sort(importedNotes.begin(), importedNotes.end(),
                      [](const NoteEvent& a, const NoteEvent& b)
                      {
                          if (a.startBeat == b.startBeat) return a.pitch < b.pitch;
                          return a.startBeat < b.startBeat;
                      });

            if (importedNotes.empty())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "MIDI Import Failed",
                    "The selected MIDI file does not contain any note events.");
                return;
            }

            audioEngine.ensureCacheLoaderStopped();
            audioEngine.stop();
            audioEngine.allSynthNotesOff();
            channelRack.setPlaybackStep(-1);
            playlist.setPlayheadBar(-1.0);
            if (pianoRollWindow != nullptr)
                pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);

            importPat->variations[channelRack.activeVariation].notes[(size_t)importChannel] = std::move(importedNotes);
            const int importedSteps = juce::jlimit(1, kMaxPatternSteps,
                                                   (int)std::ceil(maxEndBeat / 0.25));
            importPat->stepCount = importedSteps;
            importPat->lengthBars = juce::jmax(1, (int)std::ceil((double)importedSteps / 16.0));

            audioEngine.setPatternStepCount(importPat->stepCount);
            audioEngine.updatePatternSnapshot();

            if (activePatternId == importPatternId)
                channelRack.loadPattern(*importPat);

            if (pianoRollWindow != nullptr)
            {
                pianoRollWindow->content.setPattern(importPat, importChannel, project.bpm);
                pianoRollStartStep = juce::jlimit(0, importPat->stepCount - 1, pianoRollStartStep);
                pianoRollWindow->content.setStartStep(pianoRollStartStep);
            }

            markDirty();
        });
}

void MainComponent::reloadProjectIntoUI()
{
    undoManager.clearUndoHistory();   // M6: stale actions reference old project data

    // Stop the audio engine before touching shared project data --
    // processSongMode reads project->patterns on the audio thread.
    audioEngine.stop();
    audioEngine.allSynthNotesOff();
    audioEngine.clearTransientPlaybackState();
    channelRack.setPlaybackStep(-1);
    playlist.setPlayheadBar(-1.0);

    // Reload audio clips
    audioEngine.unloadAllAudioClips();
    for (const auto& clip : project.playlistClips)
    {
        if (clip.clipType == ClipType::Audio && clip.audioFilePath.isNotEmpty())
            audioEngine.loadAudioClip(clip.id, juce::File(clip.audioFilePath), clip.lengthBars);
    }

    // Reload launchpad samples
    for (int i = 0; i < 64; ++i)
    {
        const auto& pad = project.launchpadPads[(size_t)i];
        if (pad.filePath.isNotEmpty())
            audioEngine.loadLaunchpadSample(i, juce::File(pad.filePath));
        else
            audioEngine.unloadLaunchpadSample(i);
    }
    launchpadPanel.setProject(&project);

    // Restore active pattern if valid, otherwise fall back to the first pattern
    activePatternId = project.activePatternId;
    patternStartStep = juce::jmax(0, project.patternStartStep);
    pianoRollStartStep = patternStartStep;
    if (findPattern(activePatternId) == nullptr)
        activePatternId = project.patterns.empty() ? 1 : project.patterns.front().id;
    project.activePatternId = activePatternId;

    // Migrate legacy projects: if patterns lack per-pattern channelCount,
    // inherit the global project.channelCount so existing projects aren't truncated.
    for (auto& pat : project.patterns)
    {
        if (pat.channelCount == 3 && project.channelCount > 3)
            pat.channelCount = project.channelCount;
    }

    // Restore channel count from the active pattern
    {
        const auto* activePat = findPattern(activePatternId);
        const int chCount = activePat != nullptr ? activePat->channelCount : project.channelCount;
        project.channelCount = chCount;
        const juce::String* names = activePat != nullptr ? activePat->channelNames : nullptr;
        channelRack.resetToChannelCount(chCount, names);
    }

    // Load active pattern into channel rack and restore its samples.
    // Also migrate any Sampler channels that still carry untouched synth defaults.
    if (auto* activePat = findPattern(activePatternId))
    {
        // One-time migration pass across all patterns: upgrade any Sampler channel
        // that still has the generic synth defaults to neutral sampler params.
        for (auto& pat : project.patterns)
        {
            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            {
                if (pat.channelSourceTypes[ch] == ChannelSourceType::Sampler
                    && looksLikeUntouchedSynthDefaults(pat.synthParams[ch]))
                {
                    pat.synthParams[ch] = samplerNeutralSynthParams();
                }
            }
        }

        channelRack.loadPattern(*activePat);
        patternStartStep = juce::jlimit(0, activePat->stepCount - 1, patternStartStep);
        pianoRollStartStep = juce::jlimit(0, activePat->stepCount - 1, pianoRollStartStep);
        project.patternStartStep = patternStartStep;
        channelRack.setPatternStartStep(patternStartStep);
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            if (activePat->samplePaths[ch].isNotEmpty())
                audioEngine.loadSample(ch, juce::File(activePat->samplePaths[ch]));
            else
                audioEngine.unloadSample(ch);

            // Load sampler source buffer if this channel is in Sampler mode
            if (activePat->channelSourceTypes[ch] == ChannelSourceType::Sampler
                && activePat->samplerParams[ch].samplePath.isNotEmpty())
            {
                audioEngine.loadSamplerSource(ch, juce::File(activePat->samplerParams[ch].samplePath));
            }
        }
    }

    // Refresh toolbar
    toolbar.setBPM(project.bpm);
    toolbar.setPlayMode(project.playMode);
    toolbar.updatePatternList(project.patterns, activePatternId);

    // Rebuild snapshot from the newly loaded active pattern
    audioEngine.setActivePattern(activePatternId);
    audioEngine.updatePatternSnapshot();
    audioEngine.setBPM(project.bpm);
    audioEngine.setPlayMode(project.playMode);

    // Refresh playlist
    playlist.setProject(&project);

    // M5 -- ensure mixer tracks exist, then sync UI
    if (project.mixerTracks.empty())
    {
        for (int t = 0; t < 8; ++t)
        {
            MixerTrack mt;
            mt.name = "Track " + juce::String(t + 1);
            project.mixerTracks.push_back(mt);
        }
        // Routing now lives in Pattern constructor (channelMixerRouting[i] = i % 8)
    }
    mixer.loadFromProject(project);
    if (auto* activePat = findPattern(activePatternId))
        mixer.updateRoutingLabels(activePat->channelMixerRouting);

    refreshSynthEditorPresetList();

    // M11 -- ensure playlist tracks exist
    if (project.playlistTracks.empty())
        for (int t = 0; t < 8; ++t)
        {
            PlaylistTrack pt;
            pt.name = "Track " + juce::String(t + 1);
            project.playlistTracks.push_back(pt);
        }

    // M3 -- channel types are now per-pattern; loadPattern (above) already applied them

    // M8 -- unload any previously active plugins, then reload from project
    for (int ch = 0; ch < 16; ++ch)
    {
        if (pluginEditorWindows[(size_t)ch] != nullptr)
            pluginEditorWindows[(size_t)ch].reset();
        instrumentPanel_.clearVstKnobs();
        audioEngine.unloadPlugin(ch);
        channelRack.setChannelHasPlugin(ch, false);
    }

    // Restore plugins from the active pattern's per-pattern slots
    if (auto* activePat = findPattern(activePatternId))
    {
        audioEngine.restorePluginsFromSlots(activePat->pluginSlots);
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            channelRack.setChannelHasPlugin(ch, audioEngine.hasPlugin(ch));
    }

    if (pianoRollWindow != nullptr && pianoRollWindow->isVisible() && pianoRollChannel >= 0)
    {
        if (auto* pat = findPattern(activePatternId))
        {
            pianoRollWindow->content.setPattern(pat, pianoRollChannel, project.bpm);
            pianoRollStartStep = juce::jlimit(0, pat->stepCount - 1, pianoRollStartStep);
            pianoRollWindow->content.setStartStep(pianoRollStartStep);
            pianoRollWindow->content.setKeySignature(project.keySignature);
        }
    }

    if (synthEditorWindow != nullptr && synthEditorWindow->isVisible() && synthEditorChannel >= 0)
    {
        if (auto* pat = findPattern(activePatternId))
        {
            const int ch = synthEditorChannel;
            synthEditorWindow->panel.loadParams(pat->synthParams[(size_t)ch]);
            synthEditorWindow->panel.loadSamplerData(pat->channelSourceTypes[(size_t)ch],
                                                    pat->samplerParams[(size_t)ch]);
            if (pat->channelSourceTypes[(size_t)ch] == ChannelSourceType::Sampler)
                synthEditorWindow->panel.setSamplerPreviewBuffer(
                    audioEngine.getSamplerSourceBuffer(ch));
            else
                synthEditorWindow->panel.setSamplerPreviewBuffer(nullptr);
        }
    }

    projectDirty = false;
    toolbar.setProjectTitle(currentFile.getFileNameWithoutExtension(), false);

    // Phase-6: refresh inspector tab sub-labels after project load
    if (auto* pat = findPattern(activePatternId))
        inspectorTabBar_.setSequencerSub(pat->name);
    inspectorTabBar_.setInstrumentSub(0, channelRack.getChannelName(0));

    // Refresh instrument panel (channel 0 default)
    if (auto* pat = findPattern(activePatternId))
    {
        const bool hp0 = audioEngine.hasPlugin(0);
        instrumentPanel_.setChannel(0, channelRack.getChannelName(0),
                                    pat->samplePaths[0], pat->synthParams[0],
                                    pat->channelPitch[0],
                                    pat->channelTypes[0],
                                    hp0,
                                    hp0 ? audioEngine.getPlugin(0)->getName() : juce::String{});
        instrumentPanel_.clearVstKnobs();
        if (hp0)
            instrumentPanel_.buildVstKnobs(audioEngine.getPlugin(0));
        if (inspectorTab_ == 0)
            instrumentPanel_.setSize(
                instrumentViewport_.getWidth() - instrumentViewport_.getScrollBarThickness(),
                instrumentPanel_.getNeededHeight());
        if (pat->channelTypes[0] == ChannelType::Melodic && !hp0)
            instrumentPanel_.setAvailablePresets(
                SynthPresets::mergeFactoryAndCustom(project.customSynthPresets),
                pat->synthParams[0].presetName);
    }
}

void MainComponent::newProject()
{
    // Prompt if unsaved changes exist
    if (projectDirty)
    {
        auto* dlg = new juce::AlertWindow("New Project",
            "Unsaved changes will be lost. Continue?",
            juce::MessageBoxIconType::WarningIcon);
        dlg->addButton("Continue", 1);
        dlg->addButton("Cancel",   0);
        dlg->enterModalState(true,
            juce::ModalCallbackFunction::create([this, dlg](int r)
            {
                if (r == 1)
                {
                    audioEngine.stop();
                    audioEngine.allSynthNotesOff();
                    audioEngine.clearTransientPlaybackState();
                    playlist.setPlayheadBar(-1.0);
                    project = Project{};
                    project.bpm = 70.0;
                    project.playMode = PlayMode::Pattern;
                    project.activePatternId = 1;
                    project.patterns.reserve(64);
                    Pattern def; def.id = 1; def.name = "Pattern 1"; def.stepCount = 16;
                    project.patterns.push_back(def);
                    currentFile = juce::File{};
                    reloadProjectIntoUI();
                }
                delete dlg;
            }), false);
        return;
    }

    audioEngine.stop();
    audioEngine.allSynthNotesOff();
    audioEngine.clearTransientPlaybackState();
    playlist.setPlayheadBar(-1.0);
    project = Project{};
    project.bpm = 70.0;
    project.playMode = PlayMode::Pattern;
    project.activePatternId = 1;
    project.patterns.reserve(64);
    Pattern def; def.id = 1; def.name = "Pattern 1"; def.stepCount = 16;
    project.patterns.push_back(def);
    currentFile = juce::File{};
    reloadProjectIntoUI();
}

void MainComponent::openProject()
{
    fileChooser = std::make_shared<juce::FileChooser>(
        "Open Project",
        currentFile.existsAsFile() ? currentFile.getParentDirectory()
                                   : juce::File::getSpecialLocation(
                                         juce::File::userDocumentsDirectory),
        ProjectSerializer::fileWildcard);

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto chosen = fc.getResult();
            if (!chosen.existsAsFile()) return;

            Project loaded;
            if (ProjectSerializer::load(chosen, loaded))
            {
                project     = std::move(loaded);
                project.patterns.reserve(64);   // avoid reallocation on add
                currentFile = chosen;
                audioEngine.setProject(&project);
                reloadProjectIntoUI();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Open Failed",
                    "Could not read the project file.");
            }
        });
}

// Sync channel rack's channel count + names/types into the project model before saving.
// Names & types live in the active pattern, so saveToPattern handles them.
void MainComponent::syncChannelRackToProject()
{
    project.channelCount = channelRack.getChannelCount();
    // Save current rack state (including names/types) into the active pattern
    if (auto* pat = findPattern(activePatternId))
        channelRack.saveToPattern(*pat);
}

void MainComponent::refreshSynthEditorPresetList(const juce::String& selectPresetName)
{
    if (synthEditorWindow == nullptr)
        return;

    synthEditorWindow->panel.setAvailablePresets(
        SynthPresets::mergeFactoryAndCustom(project.customSynthPresets),
        selectPresetName);
}

int MainComponent::ensureAutoBassChannel()
{
    auto* activePat = findPattern(activePatternId);
    if (activePat == nullptr)
        return -1;

    channelRack.saveToPattern(*activePat);

    for (int ch = 0; ch < project.channelCount && ch < Pattern::kMaxChannels; ++ch)
    {
        const auto name = activePat->channelNames[ch].trim().toLowerCase();
        if (activePat->channelTypes[ch] == ChannelType::Melodic
            && (name == "bass" || name.contains("bass")))
            return ch;
    }

    if (project.channelCount >= Pattern::kMaxChannels)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Bassline Generation",
            "No free channel is available for a bassline. Reuse another melodic channel or remove one first.");
        return -1;
    }

    const int newChannel = project.channelCount;
    const auto presets = SynthPresets::getAll();
    const auto bassPresetIt = std::find_if(presets.begin(), presets.end(),
                                           [](const SynthPresets::Preset& preset)
                                           {
                                               return preset.name == "Fat Bass";
                                           });
    const SynthParams bassParams = bassPresetIt != presets.end() ? bassPresetIt->params : SynthParams{};

    for (auto& pat : project.patterns)
    {
        pat.channelNames[newChannel] = "Bass";
        pat.channelTypes[newChannel] = ChannelType::Melodic;
        pat.synthParams[newChannel] = bassParams;
        pat.samplePaths[newChannel].clear();
        for (int vi = 0; vi < Pattern::kMaxVariations; ++vi)
            pat.variations[vi].notes[newChannel].clear();
    }

    project.channelCount = newChannel + 1;
    channelRack.resetToChannelCount(project.channelCount, activePat->channelNames);
    channelRack.loadPattern(*activePat);
    channelRack.setChannelType(newChannel, ChannelType::Melodic);
    audioEngine.unloadSample(newChannel);
    audioEngine.updatePatternSnapshot();
    markDirty();
    return newChannel;
}

void MainComponent::focusPianoRollChannel(int channelIndex)
{
    pianoRollChannel = channelIndex;
    // Auto-route MIDI input to the channel being viewed in the piano roll
    audioEngine.setMidiTargetChannel(channelIndex);
    if (pianoRollWindow == nullptr)
        return;

    if (auto* pat = findPattern(activePatternId))
    {
        pianoRollWindow->content.setPattern(pat, channelIndex, project.bpm);
        pianoRollStartStep = juce::jlimit(0, pat->stepCount - 1, pianoRollStartStep);
        pianoRollWindow->content.setStartStep(pianoRollStartStep);
        pianoRollWindow->content.setKeySignature(project.keySignature);
    }
}

void MainComponent::switchToPatternModeForEditing()
{
    if (toolbar.getPlayMode() == PlayMode::Pattern)
        return;

    // Always stop and kill voices when switching from Song -> Pattern mode,
    // regardless of whether playback is currently active.  If the engine was
    // paused mid-song, polySynth voices triggered by the song sequencer may
    // still be in ADSR decay and would become audible the moment playMode
    // flips to Pattern (where useSynth becomes true in mixToOutput).
    audioEngine.stop();
    audioEngine.allSynthNotesOff();
    channelRack.setPlaybackStep(-1);
    playlist.setPlayheadBar(-1.0);
    if (pianoRollWindow != nullptr)
        pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);
    pianoRollPlaybackOverridesPlayMode = false;

    project.playMode = PlayMode::Pattern;
    toolbar.setPlayMode(PlayMode::Pattern);
    audioEngine.setPlayMode(PlayMode::Pattern);
    markDirty();
}

void MainComponent::beginPianoRollPatternPlayback()
{
    if (toolbar.getPlayMode() == PlayMode::Pattern)
        return;

    pianoRollPlaybackOverridesPlayMode = true;
    playModeBeforePianoRollPlayback = toolbar.getPlayMode();
    toolbar.setPlayMode(PlayMode::Pattern);
    audioEngine.setPlayMode(PlayMode::Pattern);
}

void MainComponent::restorePlayModeAfterPianoRollPlayback()
{
    if (! pianoRollPlaybackOverridesPlayMode)
        return;

    pianoRollPlaybackOverridesPlayMode = false;
    toolbar.setPlayMode(playModeBeforePianoRollPlayback);
    audioEngine.setPlayMode(playModeBeforePianoRollPlayback);
}

void MainComponent::saveProject()
{
    if (currentFile.existsAsFile())
    {
        // Flush current channel rack -> active pattern before saving
        if (auto* pat = findPattern(activePatternId))
            channelRack.saveToPattern(*pat);
        syncChannelRackToProject();

        // M8 -- collect plugin state from audio engine before serialising
        for (int ch = 0; ch < 16; ++ch)
        {
            juce::MemoryBlock state;
            if (audioEngine.getPluginState(ch, state))
                project.channelInstrumentPlugins[(size_t)ch].pluginStateBase64 =
                    state.toBase64Encoding();
        }

        if (ProjectSerializer::save(project, currentFile))
        {
            projectDirty = false;
            toolbar.setProjectTitle(currentFile.getFileNameWithoutExtension(), false);
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Save Failed", "Could not write the project file.");
        }
    }
    else
    {
        saveProjectAs();
    }
}

void MainComponent::saveProjectAs()
{
    // Flush before showing dialog
    if (auto* pat = findPattern(activePatternId))
        channelRack.saveToPattern(*pat);
    syncChannelRackToProject();

    // M8 -- collect plugin state
    for (int ch = 0; ch < 16; ++ch)
    {
        juce::MemoryBlock state;
        if (audioEngine.getPluginState(ch, state))
            project.channelInstrumentPlugins[(size_t)ch].pluginStateBase64 =
                state.toBase64Encoding();
    }

    fileChooser = std::make_shared<juce::FileChooser>(
        "Save Project As",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile("Untitled." + juce::String(ProjectSerializer::fileExtension)),
        ProjectSerializer::fileWildcard);

    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto chosen = fc.getResult();
            if (chosen == juce::File{}) return;

            // Ensure correct extension
            if (chosen.getFileExtension().isEmpty())
                chosen = chosen.withFileExtension(ProjectSerializer::fileExtension);

            if (ProjectSerializer::save(project, chosen))
            {
                currentFile  = chosen;
                projectDirty = false;
                toolbar.setProjectTitle(chosen.getFileNameWithoutExtension(), false);
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Save Failed", "Could not write the project file.");
            }
        });
}

// ---------------------------------------------------------------------------
// M6 -- Keyboard shortcuts
// ---------------------------------------------------------------------------

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    // Forward to launchpad if the panel is visible
    if (showLaunchpad)
        if (launchpadPanel.handleKey(key))
            return true;

    const auto cmd   = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    if (key == juce::KeyPress('z', cmd, 0))
    {
        undoManager.undo();
        return true;
    }
    if (key == juce::KeyPress('z', cmd | shift, 0))
    {
        undoManager.redo();
        return true;
    }
    // Tab -- toggle Song / Pattern mode (skip when Live Loop window is open)
    if (key == juce::KeyPress(juce::KeyPress::tabKey))
    {
        if (liveLoopWindow_ == nullptr || !liveLoopWindow_->isVisible())
        {
            toolbar.togglePlayMode();
            audioEngine.setPlayMode(toolbar.getPlayMode());
        }
        return true;
    }

    // Cmd+C -- copy selected playlist clip
    if (key == juce::KeyPress('c', cmd, 0))
    {
        playlist.copySelectedClip();
        return true;
    }

    // Cmd+V -- paste clipboard clip
    if (key == juce::KeyPress('v', cmd, 0))
    {
        playlist.pasteClipboard();
        return true;
    }

    // Space -- pause / resume (song mode keeps position; pattern mode restarts)
    if (key == juce::KeyPress(juce::KeyPress::spaceKey))
    {
        if (audioEngine.isPlaying())
        {
            // PAUSE -- check for double-Space (Space resume -> Space again within 800ms = restart)
            const juce::int64 now = juce::Time::currentTimeMillis();
            const bool doubleSpace = lastSpaceResumeTime > 0 &&
                                     (now - lastSpaceResumeTime) < 600;
            lastSpaceResumeTime = 0;    // reset regardless

            if (pianoRollWindow != nullptr)
                pianoRollWindow->content.pianoRoll.setRecording(false);

            // Stop recording on any pause/stop via Space
            if (audioEngine.isRecording())
            {
                audioEngine.setInputMonitoring(false);
                audioEngine.stopRecording();
                toolbar.setRecordingActive(false);
            }

            if (doubleSpace)
            {
                // Double-Space: stop and return to beginning (no auto-play)
                pausedBarSong     = -1.0;
                pausedPatternBeat = -1.0;
                audioEngine.stop();
                audioEngine.allSynthNotesOff();
                channelRack.setPlaybackStep(-1);
                playlist.setPlayheadBar(-1.0);
                if (pianoRollWindow != nullptr)
                    pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);
            }
            else if (project.playMode == PlayMode::Song)
            {
                // Normal pause -- save position, keep playhead visible
                pausedBarSong = audioEngine.getSongBeatPosition() / 4.0;
                audioEngine.stop();
                audioEngine.allSynthNotesOff();
                playlist.setPlayheadBar(pausedBarSong);
            }
            else
            {
                // Pattern mode pause: save beat for resume
                pausedPatternBeat = audioEngine.getPatternBeatPos();
                pausedBarSong     = -1.0;
                audioEngine.stop();
                audioEngine.allSynthNotesOff();
                channelRack.setPlaybackStep(-1);
                if (pianoRollWindow != nullptr)
                    pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);
            }
        }
        else if (pianoRollWindow != nullptr && pianoRollWindow->isVisible())
        {
            auto& pr = pianoRollWindow->content.pianoRoll;

            if (pr.getRecState() == PianoRollComponent::RecState::Armed)
            {
                // Space again while Armed -> cancel; back to Idle
                pr.setRecording(false);
            }
            else if (pr.isTriggerEnabled() && pianoRollWindow->content.recBtn.getToggleState())
            {
                // Trigger ON + REC active -> enter Armed (do NOT start engine yet)
                pr.setRecording(true);
            }
            else
            {
                syncPatternToEngine();
                const int startStep = (pianoRollWindow != nullptr)
                    ? pianoRollWindow->content.pianoRoll.getStartStep()
                    : pianoRollStartStep;
                pianoRollStartStep = juce::jmax(0, startStep);
                audioEngine.play(pianoRollStartStep, project.songStartBar);
                // Note: piano roll path is pattern-mode only, no lastSpaceResumeTime needed
            }
        }
        else
        {
            // RESUME from paused/seeked position
            syncPatternToEngine();
            project.patternStartStep = patternStartStep;
            audioEngine.play(patternStartStep, project.songStartBar);

            // Song mode: seek to paused/clicked position (play() resets to 0, override it)
            if (project.playMode == PlayMode::Song && pausedBarSong > 0.0)
                audioEngine.seekSongToBar(pausedBarSong);

            lastSpaceResumeTime = juce::Time::currentTimeMillis();  // arm double-Space detection
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Component overrides
// ---------------------------------------------------------------------------

void MainComponent::paint(juce::Graphics& g)
{
    using LF = StudioLookAndFeel;

    // 1. Chassis gradient fill
//    juce::ColourGradient bodyGrad(juce::Colour(LF::0xfff2f2f2)), 0.0f, 0.0f,
//                                  juce::Colour(LF::kChassis2), 0.0f, (float)getHeight(), false);
    juce::Colour lightGreyWhite (0xfff2f2f2); // 아주 밝은 회색
    juce::Colour softWhite (0xfffafafa);      // 거의 하얀색
    
    juce::ColourGradient bodyGrad(softWhite, 0.0f, 0.0f,
                                  lightGreyWhite, 0.0f, (float)getHeight(), false);
    g.setGradientFill(bodyGrad);
    g.fillAll();

    // 2. Chassis border (1px, chassisShadow=#b9ad8c) + inset highlights
    {
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(juce::Colour(0xffb9ad8c));
        g.drawRoundedRectangle(bounds, 18.0f, 1.0f);
        // top inset highlight
        g.setColour(juce::Colour(0xffffffff).withAlpha(0.4f));
        g.drawLine(20.0f, 1.5f, bounds.getRight() - 20.0f, 1.5f, 1.5f);
        // bottom inset shadow
        g.setColour(juce::Colour(0xff000000).withAlpha(0.15f));
        g.drawLine(20.0f, bounds.getBottom() - 1.0f, bounds.getRight() - 20.0f, bounds.getBottom() - 1.0f, 2.0f);
    }

    // 3. Right panel cream background
    if (!rightPanelBounds_.isEmpty())
    {
        const auto r = rightPanelBounds_.toFloat();
        g.setColour(juce::Colour(LF::kPanel));
        g.fillRoundedRectangle(r, 8.0f);
        // inset top highlight
        g.setColour(juce::Colour(0xffffffff).withAlpha(0.5f));
        g.drawLine(r.getX() + 9.0f, r.getY() + 0.5f, r.getRight() - 9.0f, r.getY() + 0.5f, 1.0f);
        // panel rim border
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
    }

    // 4. Inspector content area: chassis gradient (matches reference's inspector bg)
    if (!inspectorContentBounds_.isEmpty() && inspectorTab_ != -1)
    {
        const auto r = inspectorContentBounds_.toFloat();
        juce::ColourGradient inspGrad(juce::Colours::white, r.getX(), r.getY(),
                                           juce::Colour(0xfff0f0f0), r.getX(), r.getBottom(), false);
        g.setGradientFill(inspGrad);
        g.fillRect(r);
        // bottom border
        g.setColour(juce::Colour(0xffdcdcdc));
        g.drawLine(r.getX(), r.getBottom() - 0.5f, r.getRight(), r.getBottom() - 0.5f, 1.0f);
    }

    // 5. Corner screws (10px diameter, 8px from corners)
    {
        const float sz = 10.0f;
        const float off = 8.0f;
        const float W = (float)getWidth();
        const float H = (float)getHeight();
        const float cx[] = { off + sz*0.5f, W - off - sz*0.5f, off + sz*0.5f, W - off - sz*0.5f };
        const float cy[] = { off + sz*0.5f, off + sz*0.5f, H - off - sz*0.5f, H - off - sz*0.5f };
        const float angles[] = { 45.0f, 125.0f, 75.0f, 160.0f };

        for (int i = 0; i < 4; ++i)
        {
            const float x = cx[i], y = cy[i];
            // Radial gradient: light at 30%/25% of diameter from top-left, dark at edge
            juce::ColourGradient screwGrad(
                juce::Colour(0xffffffff), x - sz*0.2f, y - sz*0.25f,
                juce::Colour(0xffa0a0a0), x + sz*0.35f, y + sz*0.35f, true);
            screwGrad.addColour(0.5, juce::Colour(0xffdcdcdc));
            g.setGradientFill(screwGrad);
            g.fillEllipse(x - sz*0.5f, y - sz*0.5f, sz, sz);
            // Border
            g.setColour(juce::Colour(0x66000000));
            g.drawEllipse(x - sz*0.5f + 0.5f, y - sz*0.5f + 0.5f, sz - 1.0f, sz - 1.0f, 1.0f);
            // Slot
            const float rad = angles[i] * juce::MathConstants<float>::pi / 180.0f;
            const float len = sz * 0.32f;
            g.setColour(juce::Colour(0x88000000));
            g.drawLine(x - std::cos(rad)*len, y - std::sin(rad)*len,
                       x + std::cos(rad)*len, y + std::sin(rad)*len, 1.0f);
        }
    }

    // 6. Footer
    const int footerH = 22;
    auto footer = getLocalBounds().removeFromBottom(footerH).reduced(8, 0);
    g.setColour(juce::Colour(0x20000000));
    g.drawLine((float)footer.getX(), (float)footer.getY(),
               (float)footer.getRight(), (float)footer.getY(), 1.0f);
    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f).withStyle("Bold")));
    g.setColour(juce::Colour(LF::kTextFaint));
    g.drawText(juce::String::fromUTF8("xitges instruments  \xc2\xb7  made in original spirit  \xc2\xb7  serial 02-1184-1"),
               footer.removeFromLeft(620), juce::Justification::centredLeft, true);
    g.drawText(juce::String::fromUTF8("USB-C  \xc2\xb7  MIDI 5P  \xc2\xb7  CV 1/8\" x4  \xc2\xb7  v2.4.1"),
               footer, juce::Justification::centredRight, true);
}

void MainComponent::resized()
{
    constexpr int kPad = 14;
    constexpr int kGap = 12;
    constexpr int kFooterH = 22;
    constexpr int kToolbarH = 132;   // 92px transport + 40px pattern strip
    constexpr int kTabH = 78;

    auto area = getLocalBounds().reduced(kPad);

    toolbar.setBounds(area.removeFromTop(kToolbarH));
    area.removeFromTop(kGap);

    auto footerArea = area.removeFromBottom(kFooterH);
    juce::ignoreUnused(footerArea);
    area.removeFromBottom(kGap);

    // Launchpad — right panel, full height below toolbar
    launchpadPanel.setVisible(showLaunchpad);
    if (showLaunchpad)
    {
        const int padW = juce::jmin(450, area.getWidth() / 2);
        launchpadPanel.setBounds(area.removeFromRight(padW));
    }

    // M15 — sample browser: left panel, collapsible
    constexpr int kBrowserW = 260;   // reference updated: 260px browser
    constexpr int kReopenW  = 22;
    constexpr int kReopenH  = 32;

    browserViewport.setVisible(isBrowserOpen);
    if (isBrowserOpen)
    {
        browserCollapseBtn.setVisible(false);
        auto browserArea = area.removeFromLeft(kBrowserW);
        browserViewport.setBounds(browserArea);
        sampleBrowser.setSize(kBrowserW, juce::jmax(browserArea.getHeight(), 800));
        area.removeFromLeft(kGap);
    }
    else
    {
        browserCollapseBtn.setVisible(true);
        browserCollapseBtn.setBounds(kPad, area.getY(), kReopenW, kReopenH);
        browserCollapseBtn.toFront(false);
    }

    // Cache right panel bounds (cream panel bg drawn in paint())
    rightPanelBounds_ = area;

    // Playlist — top ~55% of remaining area (Phase-6: reference layout)
    const int playlistHeight = juce::jmax(260, (int)std::round(area.getHeight() * 0.55f));
    auto playlistArea = area.removeFromTop(playlistHeight);

    const int ctrlY = playlistArea.getY() + 2;
    const int ctrlR = playlistArea.getRight() - 2;
    playlistSnapBox    .setBounds(ctrlR - 114,      ctrlY, 112, 20);
    playlistZoomInBtn  .setBounds(ctrlR - 114 - 46, ctrlY,  22, 20);
    playlistZoomOutBtn .setBounds(ctrlR - 114 - 70, ctrlY,  22, 20);

    playlistViewport.setBounds(playlistArea);
    playlist.setSize(juce::jmax(playlistArea.getWidth(),  playlist.getNeededWidth()),
                     juce::jmax(playlistArea.getHeight(), playlist.getNeededHeight()));

    // Inspector tab bar (left 330px) + step inspector strip (remainder)
    {
        auto tabStrip = area.removeFromTop(kTabH);
        constexpr int kTabBarW = 330;
        const int tabBarW = juce::jmin(kTabBarW, tabStrip.getWidth());
        inspectorTabBar_.setBounds(tabStrip.getX(), tabStrip.getY(),
                                   tabBarW, tabStrip.getHeight());

        if (tabStrip.getWidth() > tabBarW + 20)
        {
            stepInspector_.setBounds(tabStrip.getX() + tabBarW + 2, tabStrip.getY(),
                                     tabStrip.getWidth() - tabBarW - 2, tabStrip.getHeight());
            stepInspector_.setVisible(true);
        }
        else
        {
            stepInspector_.setVisible(false);
        }
    }
    inspectorContentBounds_ = area;

    // Inspector content — INSTRUMENT(0) / SEQUENCER(1) / MIXER(2)
    const bool showInstr = (inspectorTab_ == 0);
    const bool showSeq   = (inspectorTab_ == 1);
    const bool showMix   = (inspectorTab_ == 2);

    instrumentViewport_.setVisible(showInstr);
    channelRackViewport.setVisible(showSeq);
    mixer.setVisible(showMix);

    if (showInstr)
    {
        instrumentViewport_.setBounds(area);
        instrumentPanel_.setSize(
            area.getWidth() - instrumentViewport_.getScrollBarThickness(),
            instrumentPanel_.getNeededHeight());
    }
    else if (showSeq)
    {
        channelRackViewport.setBounds(area);
        const int viewW = area.getWidth();
        const int viewH = area.getHeight();
        if (channelRack.getWidth() != viewW)
            channelRack.setSize(viewW, juce::jmax(viewH, channelRack.getNeededHeight()));
    }
    else if (showMix)
    {
        mixer.setBounds(area);
    }
}

void MainComponent::timerCallback()
{
    // -- Live Performance: drain pad triggers -> ClipLauncher ------------------
    {
        ClipTriggerEvent ev;
        while (audioEngine.drainClipTrigger(ev))
            clipLauncher_.triggerClip(ev.padIndex, ev.velocity);

        const double beatPos = audioEngine.getPatternBeatPos();
        if (beatPos >= 0.0)
            clipLauncher_.processQuantizedLaunch(beatPos);
    }
    // -------------------------------------------------------------------------

    // -- MIDI CC -> Real-Time Parameter Control ---------------------------------
    // Drain CC events captured on the audio thread and apply the default mapping.
    //
    // Default mapping (MPK Mini / standard layout -- all adjustable later):
    //   CC 70-77 -> channel 0-7 volume  (normalised 0-127 -> 0.0-1.0)
    //   CC 10    -> global pan (unused for now)
    //   Any unmapped CC is discarded silently.
    {
        CcEvent cc;
        while (audioEngine.drainCcEvent(cc))
        {
            const float norm = cc.value / 127.0f;   // 0.0-1.0

            // Volume mapping: CC 70-77 -> channel 0-7
            if (cc.ccNumber >= 70 && cc.ccNumber <= 77)
            {
                const int ch = cc.ccNumber - 70;
                audioEngine.setCcChannelVolume(ch, norm);
            }
            // Loop length: CC 1 (mod wheel) -> 1/2/4/8 bars
            // 0-31 -> 1 bar (4 beats), 32-63 -> 2 bars (8), 64-95 -> 4 bars (16), 96-127 -> 8 bars (32)
            else if (cc.ccNumber == 1)
            {
                // Mod wheel -> loop length (stored in UI, not engine directly)
                // This is a read-only hint; actual length set when arming via UI
            }
            // Pan mapping: CC 10 -> channel determined by dawChannel field
            else if (cc.ccNumber == 10)
            {
                const int ch = (cc.dawChannel >= 0 && cc.dawChannel < 16)
                               ? cc.dawChannel : 0;
                audioEngine.setCcChannelPan(ch, norm);
            }
            // Extend mappings here as needed (MIDI learn, user map, etc.)
        }
    }
    // -------------------------------------------------------------------------

    if (pianoRollWindow != nullptr && pianoRollWindow->isVisible() && pianoRollChannel >= 0 && pianoRollChannel < 16)
    {
        std::array<bool, 128> held {};
        audioEngine.getMidiHeldNotesForChannel(pianoRollChannel, held);
        pianoRollWindow->content.pianoRoll.setExternalHeldPitches(held);

        // External MIDI trigger: if Armed and any note is held, fire Armed->Recording
        auto& pr = pianoRollWindow->content.pianoRoll;
        if (pr.getRecState() == PianoRollComponent::RecState::Armed)
        {
            for (int i = 0; i < 128; ++i)
            {
                if (held[i])
                {
                    pr.triggerFromExternalMidi();
                    break;
                }
            }
        }
    }

    // -- MIDI activity indicator: update ChannelRack dots ---------------------
    {
        uint16_t mask = 0;
        for (int ch = 0; ch < 16; ++ch)
        {
            std::array<bool, 128> held {};
            audioEngine.getMidiHeldNotesForChannel(ch, held);
            for (int n = 0; n < 128; ++n)
            {
                if (held[n]) { mask |= (uint16_t)(1 << ch); break; }
            }
        }
        channelRack.setMidiActivityMask(mask);
    }

    // -- Drain all committed notes (PianoRoll loop recording + Live Performance overdub) --
    {
        AudioEngine::CommittedNote cn;
        bool anyPianoRoll = false;
        bool anyLivePerf  = false;
        while (audioEngine.drainCommittedNote(cn))
        {
            // patternId == -1 -> PianoRoll path (write to activePatternId)
            // patternId >= 0  -> Live Performance path (write to specific pattern)
            const int targetPatId = (cn.patternId >= 0) ? cn.patternId : activePatternId;
            if (auto* pat = findPattern(targetPatId))
            {
                const int varIdx = juce::jlimit(0, Pattern::kMaxVariations - 1, cn.variation);
                pat->variations[(size_t)varIdx].notes[(size_t)cn.channel].push_back(cn.note);
                if (cn.patternId < 0) anyPianoRoll = true;
                else                  anyLivePerf  = true;
            }
        }
        if (anyPianoRoll && loopRecordEnabled_)
        {
            audioEngine.updatePatternSnapshot();
            if (pianoRollWindow != nullptr)
                pianoRollWindow->content.pianoRoll.repaint();
            markDirty();
        }
        if (anyLivePerf)
        {
            // Overdub notes: rebuild snapshot so the audio thread plays them next cycle
            audioEngine.updatePatternSnapshot();
            markDirty();
        }
    }

    // --- Input level metering (always active) ---
    toolbar.setInputLevels(audioEngine.getInputLevelL(), audioEngine.getInputLevelR());

    // --- Recording elapsed time ---
    if (audioEngine.isRecording())
    {
        const double sr = audioEngine.getSampleRate();
        if (sr > 0.0)
            toolbar.setRecordingElapsed((double)audioEngine.getRecordingSamplesWritten() / sr);
    }

    // --- Auto-Tune pitch display ---
    if (autoTuneEditorWindow != nullptr && autoTuneEditorWindow->isVisible()
        && autoTuneEditorTrack >= 0 && autoTuneEditorTrack < 8)
    {
        const auto& proc = audioEngine.getAutoTuneProcessor(autoTuneEditorTrack);
        autoTuneEditorWindow->panel.setDetectedPitch(proc.getDetectedPitchHz(),
                                                      proc.getTargetPitchHz(),
                                                      proc.getInputRms(),
                                                      proc.getConfidence(),
                                                      proc.isVoiced(),
                                                      proc.getCorrectedPitchHz());
    }

    // --- BAR.BEAT.TICK position display ---
    {
        const double barPos = (toolbar.getPlayMode() == PlayMode::Song)
                              ? audioEngine.getSongBeatPosition() / 4.0
                              : audioEngine.getPatternBeatPos() / 4.0;
        toolbar.setPlaybackBar(barPos);
    }

    if (!audioEngine.isPlaying()) return;

    if (toolbar.getPlayMode() == PlayMode::Song)
    {
        playlist.setPlayheadBar(audioEngine.getSongBeatPosition() / 4.0);
        followPlaylistPlayhead();
    }

    // M3 -- update piano roll playhead (wrap to pattern length)
    if (pianoRollWindow != nullptr && pianoRollWindow->isVisible())
    {
        double beatPos = audioEngine.getPatternBeatPos();
        if (auto* pat = findPattern(activePatternId))
        {
            const double patternBeats = pat->stepCount * 0.25;
            if (patternBeats > 0.0)
                beatPos = std::fmod(beatPos, patternBeats);
        }
        pianoRollWindow->content.pianoRoll.setPlayheadBeat(beatPos);
    }
}

void MainComponent::followPlaylistPlayhead()
{
    const double playheadBar = playlist.getPlayheadBar();
    if (playheadBar < 0.0)
        return;

    const int playheadX = playlist.getTrackHeaderWidth() + (int) std::round(playheadBar * playlist.getBarWidth());
    const auto visible = playlistViewport.getViewArea();
    const int leftMargin = visible.getWidth() / 4;
    const int rightMargin = visible.getWidth() / 3;

    int targetX = visible.getX();
    if (playheadX > visible.getRight() - rightMargin)
        targetX = juce::jmax(0, playheadX - (visible.getWidth() - rightMargin));
    else if (playheadX < visible.getX() + leftMargin)
        targetX = juce::jmax(0, playheadX - leftMargin);

    const int maxX = juce::jmax(0, playlist.getNeededWidth() - visible.getWidth());
    targetX = juce::jlimit(0, maxX, targetX);

    if (targetX != visible.getX())
        playlistViewport.setViewPosition(targetX, visible.getY());
}
