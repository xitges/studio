#include "MainComponent.h"

// ---------------------------------------------------------------------------
// M6 — generic lambda-based undo action
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
} // namespace

MainComponent::MainComponent()
{
    // M11 — apply custom dark LookAndFeel globally
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);

    // ---- Seed project with initial data
    project.bpm          = 70.0;
    project.channelCount = 3;

    // M5 — default 8 mixer tracks
    for (int t = 0; t < 8; ++t)
    {
        MixerTrack mt;
        mt.name = "Track " + juce::String(t + 1);
        project.mixerTracks.push_back(mt);
    }
    // M5 — default routing now lives in Pattern constructor (channelMixerRouting[i] = i % 8)

    // M11 — default 8 playlist tracks
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
    // Default beat
    p1.steps[0][0] = p1.steps[0][4] = p1.steps[0][8] = p1.steps[0][12] = true;
    p1.steps[1][4] = p1.steps[1][12] = true;
    for (int s = 0; s < 16; ++s) p1.steps[2][s] = true;
    project.patterns.push_back(p1);

    PlaylistClip c1; c1.id=1; c1.patternId=1; c1.name="Intro Beat";
    c1.trackIndex=0; c1.startBar=0;  c1.lengthBars=4;
    project.playlistClips.push_back(c1);

    PlaylistClip c2; c2.id=2; c2.patternId=1; c2.name="Main Beat";
    c2.trackIndex=0; c2.startBar=4;  c2.lengthBars=8;
    project.playlistClips.push_back(c2);

    PlaylistClip c3; c3.id=3; c3.patternId=1; c3.name="Break";
    c3.trackIndex=1; c3.startBar=8;  c3.lengthBars=4;
    project.playlistClips.push_back(c3);

    PlaylistClip c4; c4.id=4; c4.patternId=1; c4.name="Fill";
    c4.trackIndex=2; c4.startBar=12; c4.lengthBars=2;
    project.playlistClips.push_back(c4);

    activePatternId = 1;

    audioEngine.initialise();
    audioEngine.setProject(&project);

    // ---- Toolbar
    addAndMakeVisible(toolbar);

    toolbar.updatePatternList(project.patterns, activePatternId);

    toolbar.onPlay = [this]
    {
        syncPatternToEngine();
        markDirty();
        project.bpm = toolbar.getBPM();
        audioEngine.setBPM(project.bpm);
        audioEngine.setPlayMode(toolbar.getPlayMode());
        audioEngine.play();
    };

    toolbar.onStop = [this]
    {
        audioEngine.stop();
        audioEngine.allSynthNotesOff();
        channelRack.setPlaybackStep(-1);
        playlist.setPlayheadBar(-1.0);
        if (pianoRollWindow != nullptr)
            pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);
    };

    toolbar.onBPMChanged = [this](double bpm)
    {
        audioEngine.setBPM(bpm);
    };

    toolbar.onPlayModeChanged = [this](PlayMode mode)
    {
        audioEngine.setPlayMode(mode);
    };

    // M2.1 — Pattern selector
    toolbar.onPatternSelected = [this](int id)
    {
        selectPattern(id);
    };

    toolbar.onNewPattern = [this]
    {
        // selectPattern will save the current rack before switching.
        // Capture id/name BEFORE push_back in case it reallocates.
        audioEngine.ensureCacheLoaderStopped();
        const int newId   = nextPatternId();
        const int newNum  = (int)project.patterns.size() + 1;

        Pattern np;
        np.id        = newId;
        np.name      = "Pattern " + juce::String(newNum);
        np.stepCount = 16;
        project.patterns.push_back(np);  // may reallocate — no live pointers at this point

        selectPattern(newId);
        markDirty();
    };

    toolbar.onDuplicatePattern = [this]
    {
        audioEngine.ensureCacheLoaderStopped();
        // Save current rack into the active pattern first
        if (auto* src = findPattern(activePatternId))
            channelRack.saveToPattern(*src);

        // Re-find after save (safe — no reallocation yet)
        if (auto* src = findPattern(activePatternId))
        {
            // Copy by value and capture name BEFORE push_back may reallocate
            Pattern dup  = *src;
            const int newId = nextPatternId();
            dup.id   = newId;
            dup.name = src->name + " (copy)";
            project.patterns.push_back(dup);   // src may dangle after this — not used again

            selectPattern(newId);
            markDirty();
        }
    };

    // M4 — mark dirty on any pattern management operation
    auto markDirtyFn = [this] { markDirty(); };

    toolbar.onDeletePattern = [this, markDirtyFn]
    {
        if (project.patterns.size() <= 1) return; // always keep at least one
        audioEngine.ensureCacheLoaderStopped();

        const int deletedId = activePatternId;

        auto& pats = project.patterns;
        pats.erase(std::remove_if(pats.begin(), pats.end(),
            [deletedId](const Pattern& p){ return p.id == deletedId; }), pats.end());

        // Reassign any playlist clips that referenced the deleted pattern
        // to the first remaining pattern so they don't become orphans.
        const int fallbackId = pats.front().id;
        for (auto& clip : project.playlistClips)
            if (clip.patternId == deletedId)
                clip.patternId = fallbackId;

        selectPattern(fallbackId);
        markDirty();
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

    toolbar.setProjectTitle("", false);

    // ---- Playlist inside Viewport
    playlist.setProject(&project);
    playlist.getActivePatternId = [this]() { return activePatternId; };

    playlist.onSeekToBar = [this](double bar)
    {
        audioEngine.seekSongToBar(bar);
    };

    playlist.onZoomChanged = [this] { resized(); };

    playlistViewport.setViewedComponent(&playlist, false);

    // M15 — sample browser
    sampleBrowser.onPreviewFile = [this](const juce::File& f)
    {
        audioEngine.previewBrowserFile(f);
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

    // Reopen tab — shown only when browser is closed, at the left edge
    browserCollapseBtn.setButtonText(">");
    browserCollapseBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2a3a));
    browserCollapseBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffb0b0c8));
    browserCollapseBtn.onClick = [this]
    {
        isBrowserOpen = true;
        resized();
    };
    addAndMakeVisible(browserCollapseBtn);
    playlistViewport.setScrollBarsShown(true, true);
    playlistViewport.setScrollBarThickness(8);
    addAndMakeVisible(playlistViewport);

    // Snap box — floats outside the viewport so it never scrolls away
    playlistSnapBox.addItem("1 Bar",   1);
    playlistSnapBox.addItem("1/2 Bar", 2);
    playlistSnapBox.addItem("1/4 Bar", 4);
    playlistSnapBox.addItem("Free",    99);
    playlistSnapBox.setSelectedId(1, juce::dontSendNotification);
    playlistSnapBox.onChange = [this]
    {
        const int id = playlistSnapBox.getSelectedId();
        playlist.setSnapDivisor(id == 99 ? 0 : id);
    };
    addAndMakeVisible(playlistSnapBox);

    // M11 — zoom buttons (anchored alongside snap box)
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
            pat->samplePaths[ch] = file.getFullPathName();
        markDirty();
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

    // ---- M6: Step toggle undo
    channelRack.onStepToggled = [this](int ch, int step, bool newState, bool oldState)
    {
        markDirty();
        const int patId = activePatternId;
        undoManager.perform(new LambdaAction(
            [this, patId, ch, step, newState]() -> bool {
                if (auto* p = findPattern(patId)) {
                    p->steps[ch][step] = newState;
                    if (patId == activePatternId) {
                        channelRack.setStep(ch, step, newState);
                        audioEngine.setStepPattern(ch, step, newState);  // live sync
                        audioEngine.updatePatternSnapshot();
                    }
                }
                markDirty(); return true;
            },
            [this, patId, ch, step, oldState]() -> bool {
                if (auto* p = findPattern(patId)) {
                    p->steps[ch][step] = oldState;
                    if (patId == activePatternId) {
                        channelRack.setStep(ch, step, oldState);
                        audioEngine.setStepPattern(ch, step, oldState);  // live sync
                        audioEngine.updatePatternSnapshot();
                    }
                }
                markDirty(); return true;
            }));
    };

    // ---- M6: Playlist clip undo
    // M11 — track management callbacks
    playlist.onTrackAdded   = [this]()      { markDirty(); resized(); };
    playlist.onTrackRenamed = [this](int)   { markDirty(); };
    playlist.onTrackDeleted = [this](int)   { markDirty(); resized(); };

    // Pattern copy — duplicate the assigned pattern and reassign this clip to the copy
    playlist.onClipDetach = [this](int clipId)
    {
        // Find the clip
        PlaylistClip* clip = nullptr;
        for (auto& c : project.playlistClips)
            if (c.id == clipId) { clip = &c; break; }
        if (clip == nullptr) return;

        // Find the source pattern
        Pattern* src = findPattern(clip->patternId);
        if (src == nullptr) return;

        // Flush the rack into the source pattern if it's currently active
        if (src->id == activePatternId)
            channelRack.saveToPattern(*src);

        // Copy the pattern with a new unique ID
        const int newId = nextPatternId();
        Pattern copy    = *src;              // value copy — safe before push_back
        copy.id         = newId;
        copy.name       = src->name + " (copy)";
        project.patterns.push_back(copy);   // src may dangle — not used again

        // Reassign this clip to the new pattern copy
        clip->patternId = newId;

        toolbar.updatePatternList(project.patterns, activePatternId);
        playlist.repaint();
        markDirty();
    };

    playlist.onClipAdded = [this](PlaylistClip clip)
    {
        markDirty();
        const int clipId = clip.id;
        undoManager.perform(new LambdaAction(
            [this, clip]() -> bool {
                bool found = false;
                for (const auto& c : project.playlistClips) if (c.id == clip.id) { found = true; break; }
                if (!found) project.playlistClips.push_back(clip);
                playlist.repaint(); markDirty(); return true;
            },
            [this, clipId]() -> bool {
                auto& list = project.playlistClips;
                list.erase(std::remove_if(list.begin(), list.end(),
                    [clipId](const PlaylistClip& c){ return c.id == clipId; }), list.end());
                playlist.repaint(); markDirty(); return true;
            }));
    };

    playlist.onClipDeleted = [this](PlaylistClip clip)
    {
        markDirty();
        const int clipId = clip.id;
        undoManager.perform(new LambdaAction(
            [this, clipId]() -> bool {
                auto& list = project.playlistClips;
                list.erase(std::remove_if(list.begin(), list.end(),
                    [clipId](const PlaylistClip& c){ return c.id == clipId; }), list.end());
                playlist.repaint(); markDirty(); return true;
            },
            [this, clip]() -> bool {
                project.playlistClips.push_back(clip);
                playlist.repaint(); markDirty(); return true;
            }));
    };

    playlist.onClipMoved = [this](int id, float oldBar, int oldTrack, float newBar, int newTrack)
    {
        markDirty();
        undoManager.perform(new LambdaAction(
            [this, id, newBar, newTrack]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == id) { c.startBar = newBar; c.trackIndex = newTrack; break; }
                playlist.repaint(); markDirty(); return true;
            },
            [this, id, oldBar, oldTrack]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == id) { c.startBar = oldBar; c.trackIndex = oldTrack; break; }
                playlist.repaint(); markDirty(); return true;
            }));
    };

    playlist.onClipResized = [this](int id, float oldLen, float newLen)
    {
        markDirty();
        undoManager.perform(new LambdaAction(
            [this, id, newLen]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == id) { c.lengthBars = newLen; break; }
                playlist.repaint(); markDirty(); return true;
            },
            [this, id, oldLen]() -> bool {
                for (auto& c : project.playlistClips)
                    if (c.id == id) { c.lengthBars = oldLen; break; }
                playlist.repaint(); markDirty(); return true;
            }));
    };

    // ---- M3: Piano Roll
    channelRack.onOpenPianoRoll = [this](int ch)
    {
        pianoRollChannel = ch;

        if (pianoRollWindow == nullptr)
        {
            pianoRollWindow = std::make_unique<PianoRollWindow>();
            pianoRollWindow->centreWithSize(1000, 520);
        }

        if (auto* pat = findPattern(activePatternId))
        {
            pianoRollWindow->content.pianoRoll.setPattern(pat, ch, project.bpm);
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
                    audioEngine.stop();
                    audioEngine.allSynthNotesOff();
                    channelRack.setPlaybackStep(-1);
                    playlist.setPlayheadBar(-1.0);
                    if (pianoRollWindow != nullptr)
                        pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);
                }
                else
                {
                    syncPatternToEngine();
                    audioEngine.play();
                }
            };

            // Smart Record: armed trigger fires transport from beat 0
            pianoRollWindow->content.pianoRoll.onStartTransport = [this]
            {
                syncPatternToEngine();
                audioEngine.play();
            };
        }

        pianoRollWindow->setVisible(true);
        pianoRollWindow->toFront(true);
    };

    channelRack.onChannelTypeChanged = [this](int ch, ChannelType t)
    {
        if (auto* pat = findPattern(activePatternId))
            pat->channelTypes[ch] = t;
        channelRack.setChannelType(ch, t);
        markDirty();
    };

    // ---- M13: Synth Editor
    channelRack.onOpenSynthEditor = [this](int ch)
    {
        synthEditorChannel = ch;

        if (synthEditorWindow == nullptr)
        {
            synthEditorWindow = std::make_unique<SynthEditorWindow>();
            synthEditorWindow->centreWithSize(430, 406);
        }

        synthEditorWindow->setChannelName(
            juce::String(ch + 1));   // simple "1"-based label for now

        if (auto* pat = findPattern(activePatternId))
            synthEditorWindow->panel.loadParams(pat->synthParams[(size_t)ch]);

        synthEditorWindow->panel.onParamsChanged = [this, ch]
        {
            if (auto* pat = findPattern(activePatternId))
                synthEditorWindow->panel.applyToParams(pat->synthParams[(size_t)ch]);
            audioEngine.updatePatternSnapshot();  // keep snapshot in sync for real-time modulation
            markDirty();
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

                project.channelInstrumentPlugins[(size_t)targetCh].pluginId =
                    desc.createIdentifierString();
                project.channelInstrumentPlugins[(size_t)targetCh].enabled = true;
                channelRack.setChannelHasPlugin(targetCh, true);
                markDirty();
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

        audioEngine.unloadPlugin(ch);
        project.channelInstrumentPlugins[(size_t)ch] = {};
        channelRack.setChannelHasPlugin(ch, false);
        markDirty();
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

    // M14 — FX editor per mixer track
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

    toolbar.onToggleMixer = [this] { showMixer = !showMixer; resized(); };

    // Launchpad toggle
    toolbar.onToggleLaunchpad = [this]
    {
        if (launchpadWindow == nullptr)
        {
            launchpadWindow = std::make_unique<LaunchpadWindow>();
            launchpadWindow->panel.setProject(&project);

            // Pad pressed → trigger one-shot sample
            launchpadWindow->panel.onPadTriggered = [this](int padIdx)
            {
                audioEngine.triggerLaunchpadPad(padIdx);
            };

            // Sample dropped / assigned → load into audio engine
            launchpadWindow->panel.onSampleDropped = [this](int padIdx, juce::File file)
            {
                audioEngine.loadLaunchpadSample(padIdx, file);
                markDirty();
            };

            // Convert recorded sequence → new pattern
            launchpadWindow->panel.onConvertToPattern =
                [this](const std::vector<LaunchpadPanel::RecordedHit>& hits, int numBars)
            {
                // Ask for a name before creating the pattern
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

                            // Map each unique pad to a channel
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
                                        // Store path in pattern so selectPattern can reload it
                                        newPat.samplePaths[nextCh] = fp;
                                        audioEngine.loadSample(nextCh, juce::File(fp));
                                    }
                                    ++nextCh;
                                }
                            }

                            // Quantize hits to nearest 16th-note step
                            for (const auto& hit : hitsCopy)
                            {
                                auto it = padToChannel.find(hit.padIdx);
                                if (it == padToChannel.end()) continue;
                                const int ch   = it->second;
                                const int step = (int)std::round(hit.beatPos / 0.25)
                                                 % newPat.stepCount;
                                if (step >= 0 && step < newPat.stepCount)
                                    newPat.steps[ch][step] = true;
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

            // Provide BPM to the panel for beat-accurate recording
            launchpadWindow->panel.getBPM = [this]() { return project.bpm; };
        }

        const bool nowVisible = !launchpadWindow->isVisible();
        launchpadWindow->setVisible(nowVisible);
        if (nowVisible) launchpadWindow->toFront(true);
    };

    // M12 — MIDI device selection popup
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
                }
            });
    };

    setWantsKeyboardFocus(true);
    startTimerHz(30);
    setSize(1600, 900);
}

MainComponent::~MainComponent()
{
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
    // Save current channel rack steps into the currently active pattern
    if (auto* cur = findPattern(activePatternId))
        channelRack.saveToPattern(*cur);

    activePatternId = id;

    // Load new pattern into channel rack, samples, and sync steps to engine immediately
    if (auto* newPat = findPattern(activePatternId))
    {
        channelRack.loadPattern(*newPat);

        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            if (newPat->samplePaths[ch].isNotEmpty())
                audioEngine.loadSample(ch, juce::File(newPat->samplePaths[ch]));
            else
                audioEngine.unloadSample(ch);
        }

        // Sync step pattern to engine so it plays correctly immediately
        audioEngine.setPatternStepCount(newPat->stepCount);
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            for (int s = 0; s < newPat->stepCount; ++s)
                audioEngine.setStepPattern(ch, s, newPat->steps[ch][s]);
    }

    toolbar.updatePatternList(project.patterns, activePatternId);
    audioEngine.setActivePattern(activePatternId);
    audioEngine.updatePatternSnapshot();

    // Update open piano roll to the newly selected pattern
    if (pianoRollWindow != nullptr && pianoRollWindow->isVisible() && pianoRollChannel >= 0)
        if (auto* pat = findPattern(activePatternId))
            pianoRollWindow->content.pianoRoll.setPattern(pat, pianoRollChannel, project.bpm);
}

void MainComponent::syncPatternToEngine()
{
    auto* pat = findPattern(activePatternId);
    if (pat == nullptr) return;

    // Flush channel rack UI → pattern model
    channelRack.saveToPattern(*pat);

    audioEngine.setActivePattern(activePatternId);   // ensure NoteEvent loop uses current pattern

    if (toolbar.getPlayMode() == PlayMode::Pattern)
    {
        audioEngine.setPatternStepCount(pat->stepCount);
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            for (int s = 0; s < pat->stepCount; ++s)
                audioEngine.setStepPattern(ch, s, pat->steps[ch][s]);
    }

    audioEngine.updatePatternSnapshot();
}

// ---------------------------------------------------------------------------
// M4 — File operations
// ---------------------------------------------------------------------------

void MainComponent::markDirty()
{
    projectDirty = true;
    toolbar.setProjectTitle(currentFile.getFileNameWithoutExtension(), true);
}

void MainComponent::reloadProjectIntoUI()
{
    undoManager.clearUndoHistory();   // M6: stale actions reference old project data

    // Stop the audio engine before touching shared project data —
    // processSongMode reads project->patterns on the audio thread.
    audioEngine.stop();
    audioEngine.allSynthNotesOff();
    channelRack.setPlaybackStep(-1);
    playlist.setPlayheadBar(-1.0);

    // Reload launchpad samples
    for (int i = 0; i < 64; ++i)
    {
        const auto& pad = project.launchpadPads[(size_t)i];
        if (pad.filePath.isNotEmpty())
            audioEngine.loadLaunchpadSample(i, juce::File(pad.filePath));
    }
    if (launchpadWindow != nullptr)
        launchpadWindow->panel.setProject(&project);

    // Set active pattern to first in list
    activePatternId = project.patterns.empty() ? 1 : project.patterns.front().id;

    // Restore channel count (names/types come from the pattern via loadPattern)
    {
        const juce::String* names = project.patterns.empty()
            ? nullptr : project.patterns.front().channelNames;
        channelRack.resetToChannelCount(project.channelCount, names);
    }

    // Load first pattern into channel rack and restore its samples
    if (!project.patterns.empty())
    {
        const auto& firstPat = project.patterns.front();
        channelRack.loadPattern(firstPat);
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            if (firstPat.samplePaths[ch].isNotEmpty())
                audioEngine.loadSample(ch, juce::File(firstPat.samplePaths[ch]));
            else
                audioEngine.unloadSample(ch);
        }
    }

    // Refresh toolbar
    toolbar.updatePatternList(project.patterns, activePatternId);

    // Rebuild snapshot from the newly loaded active pattern
    audioEngine.setActivePattern(activePatternId);
    audioEngine.updatePatternSnapshot();

    // Sync BPM slider (don't fire callback — just visual update)
    // BPM is read from toolbar on Play, so we update the slider directly:
    // (Toolbar doesn't expose a setBPM setter — add a small approach via the slider)
    // We'll fire the BPM change so audioEngine is in sync too.
    audioEngine.setBPM(project.bpm);

    // Refresh playlist
    playlist.setProject(&project);

    // M5 — ensure mixer tracks exist, then sync UI
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
    if (!project.patterns.empty())
        mixer.updateRoutingLabels(project.patterns.front().channelMixerRouting);

    // M11 — ensure playlist tracks exist
    if (project.playlistTracks.empty())
        for (int t = 0; t < 8; ++t)
        {
            PlaylistTrack pt;
            pt.name = "Track " + juce::String(t + 1);
            project.playlistTracks.push_back(pt);
        }

    // M3 — channel types are now per-pattern; loadPattern (above) already applied them

    // M8 — unload any previously active plugins, then reload from project
    for (int ch = 0; ch < 16; ++ch)
    {
        if (pluginEditorWindows[(size_t)ch] != nullptr)
            pluginEditorWindows[(size_t)ch].reset();
        audioEngine.unloadPlugin(ch);
        channelRack.setChannelHasPlugin(ch, false);
    }

    for (int ch = 0; ch < 16; ++ch)
    {
        const auto& slot = project.channelInstrumentPlugins[(size_t)ch];
        if (slot.pluginId.isEmpty() || !slot.enabled) continue;

        // Find the PluginDescription in the known list by identifier string
        const auto types = PluginManager::getInstance().getKnownPlugins().getTypes();
        for (const auto& desc : types)
        {
            if (desc.createIdentifierString() == slot.pluginId)
            {
                juce::String err;
                audioEngine.loadPlugin(ch, desc, err);

                if (err.isEmpty())
                {
                    channelRack.setChannelHasPlugin(ch, true);

                    // Restore plugin state
                    if (slot.pluginStateBase64.isNotEmpty())
                    {
                        juce::MemoryBlock stateData;
                        stateData.fromBase64Encoding(slot.pluginStateBase64);
                        if (auto* plugin = audioEngine.getPlugin(ch))
                            plugin->setStateInformation(stateData.getData(),
                                                        (int)stateData.getSize());
                    }
                }
                break;
            }
        }
    }

    projectDirty = false;
    toolbar.setProjectTitle(currentFile.getFileNameWithoutExtension(), false);
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
                    project = Project{};
                    project.bpm = 70.0;
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

    project = Project{};
    project.bpm = 70.0;
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

void MainComponent::saveProject()
{
    if (currentFile.existsAsFile())
    {
        // Flush current channel rack → active pattern before saving
        if (auto* pat = findPattern(activePatternId))
            channelRack.saveToPattern(*pat);
        syncChannelRackToProject();

        // M8 — collect plugin state from audio engine before serialising
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

    // M8 — collect plugin state
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
// M6 — Keyboard shortcuts
// ---------------------------------------------------------------------------

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    // Forward to launchpad if the window is open (works even when main window is focused)
    if (launchpadWindow != nullptr && launchpadWindow->isVisible())
        if (launchpadWindow->panel.handleKey(key))
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
    // Tab — toggle Song / Pattern mode
    if (key == juce::KeyPress(juce::KeyPress::tabKey))
    {
        toolbar.togglePlayMode();
        audioEngine.setPlayMode(toolbar.getPlayMode());
        return true;
    }

    // Cmd+C — copy selected playlist clip
    if (key == juce::KeyPress('c', cmd, 0))
    {
        playlist.copySelectedClip();
        return true;
    }

    // Cmd+V — paste clipboard clip
    if (key == juce::KeyPress('v', cmd, 0))
    {
        playlist.pasteClipboard();
        return true;
    }

    // Space — play / stop
    if (key == juce::KeyPress(juce::KeyPress::spaceKey))
    {
        if (audioEngine.isPlaying())
        {
            audioEngine.stop();
            audioEngine.allSynthNotesOff();
            channelRack.setPlaybackStep(-1);
            playlist.setPlayheadBar(-1.0);
            if (pianoRollWindow != nullptr)
                pianoRollWindow->content.pianoRoll.setPlayheadBeat(-1.0);
        }
        else
        {
            syncPatternToEngine();
            audioEngine.play();
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
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Toolbar is now 80px (two rows)
    toolbar.setBounds(area.removeFromTop(80));

    // M5 — Mixer panel anchored at bottom, visible only when toggled
    mixer.setVisible(showMixer);
    if (showMixer)
        mixer.setBounds(area.removeFromBottom(160));

    // M15 — sample browser: left panel, collapsible
    constexpr int kBrowserW  = 220;
    constexpr int kReopenW   = 22;
    constexpr int kReopenH   = 32;

    browserViewport.setVisible(isBrowserOpen);
    if (isBrowserOpen)
    {
        // Collapse button is INSIDE the browser header — hide the external tab
        browserCollapseBtn.setVisible(false);

        auto browserArea = area.removeFromLeft(kBrowserW);
        browserViewport.setBounds(browserArea);
        sampleBrowser.setSize(kBrowserW, juce::jmax(browserArea.getHeight(), 800));
    }
    else
    {
        // Show the reopen ">" tab at the top-left, flush with toolbar bottom
        browserCollapseBtn.setVisible(true);
        browserCollapseBtn.setBounds(0, area.getY(), kReopenW, kReopenH);
        browserCollapseBtn.toFront(false);
    }

    const int playlistHeight = (int)(area.getHeight() * 0.38f);
    auto playlistArea = area.removeFromTop(playlistHeight);

    // Snap + zoom controls anchored to top-right, outside the scrollable area
    const int ctrlY = playlistArea.getY() + 2;
    const int ctrlR = playlistArea.getRight() - 2;
    playlistSnapBox    .setBounds(ctrlR - 114,       ctrlY, 112, 20);
    playlistZoomInBtn  .setBounds(ctrlR - 114 - 46,  ctrlY,  22, 20);
    playlistZoomOutBtn .setBounds(ctrlR - 114 - 70,  ctrlY,  22, 20);

    playlistViewport.setBounds(playlistArea);
    playlist.setSize(juce::jmax(playlistArea.getWidth(),  playlist.getNeededWidth()),
                     juce::jmax(playlistArea.getHeight(), playlist.getNeededHeight()));

    channelRackViewport.setBounds(area);

    const int viewW = area.getWidth();
    const int viewH = area.getHeight();
    if (channelRack.getWidth() != viewW)
        channelRack.setSize(viewW, juce::jmax(viewH, channelRack.getNeededHeight()));
}

void MainComponent::timerCallback()
{
    if (!audioEngine.isPlaying()) return;

    const double bpm = project.bpm > 0.0 ? project.bpm : 70.0;
    const double sr  = audioEngine.getSampleRate();

    if (toolbar.getPlayMode() == PlayMode::Song)
    {
        const double samplesPerBar = (sr * 60.0 / bpm) * 4.0;
        playlist.setPlayheadBar(audioEngine.getSongSamplePosition() / samplesPerBar);
    }

    // M3 — update piano roll playhead (wrap to pattern length)
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
