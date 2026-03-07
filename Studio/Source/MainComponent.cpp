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
    // ---- Seed project with initial data
    project.bpm = 70.0;

    // M5 — default 8 mixer tracks
    for (int t = 0; t < 8; ++t)
    {
        MixerTrack mt;
        mt.name = "Track " + juce::String(t + 1);
        project.mixerTracks.push_back(mt);
    }
    // M5 — default routing: channel N → track N (mod 8)
    for (int ch = 0; ch < 16; ++ch)
        project.channelMixerRouting[ch] = ch % 8;

    // M11 — default 8 playlist tracks
    for (int t = 0; t < 8; ++t)
    {
        PlaylistTrack pt;
        pt.name = "Track " + juce::String(t + 1);
        project.playlistTracks.push_back(pt);
    }

    Pattern p1;
    p1.id = 1; p1.name = "Pattern 1"; p1.lengthBars = 1; p1.stepCount = 16;
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
        // Save current state first
        if (auto* cur = findPattern(activePatternId))
            channelRack.saveToPattern(*cur);

        Pattern np;
        np.id        = nextPatternId();
        np.name      = "Pattern " + juce::String(project.patterns.size() + 1);
        np.stepCount = 16;
        project.patterns.push_back(np);

        selectPattern(np.id);
    };

    toolbar.onDuplicatePattern = [this]
    {
        if (auto* src = findPattern(activePatternId))
        {
            channelRack.saveToPattern(*src);

            Pattern dup = *src;
            dup.id   = nextPatternId();
            dup.name = src->name + " (copy)";
            project.patterns.push_back(dup);

            selectPattern(dup.id);
        }
    };

    // M4 — mark dirty on any pattern management operation
    auto markDirtyFn = [this] { markDirty(); };

    toolbar.onDeletePattern = [this, markDirtyFn]
    {
        if (project.patterns.size() <= 1) return; // always keep at least one

        auto& pats = project.patterns;
        pats.erase(std::remove_if(pats.begin(), pats.end(),
            [this](const Pattern& p){ return p.id == activePatternId; }), pats.end());

        selectPattern(pats.front().id);
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

    playlistViewport.setViewedComponent(&playlist, false);
    playlistViewport.setScrollBarsShown(true, true);
    playlistViewport.setScrollBarThickness(8);
    addAndMakeVisible(playlistViewport);

    // Snap box — floats outside the viewport so it never scrolls away
    playlistSnapBox.addItem("1 Bar",   1);
    playlistSnapBox.addItem("1/2 Bar", 2);
    playlistSnapBox.addItem("1/4 Bar", 4);
    playlistSnapBox.addItem("Free",    99);
    playlistSnapBox.setSelectedId(1, juce::dontSendNotification);
    playlistSnapBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0f3460));
    playlistSnapBox.setColour(juce::ComboBox::textColourId,       juce::Colours::white);
    playlistSnapBox.setColour(juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
    playlistSnapBox.onChange = [this]
    {
        const int id = playlistSnapBox.getSelectedId();
        playlist.setSnapDivisor(id == 99 ? 0 : id);
    };
    addAndMakeVisible(playlistSnapBox);

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
                    if (patId == activePatternId) channelRack.loadPattern(*p);
                }
                markDirty(); return true;
            },
            [this, patId, ch, step, oldState]() -> bool {
                if (auto* p = findPattern(patId)) {
                    p->steps[ch][step] = oldState;
                    if (patId == activePatternId) channelRack.loadPattern(*p);
                }
                markDirty(); return true;
            }));
    };

    // ---- M6: Playlist clip undo
    // M11 — track management callbacks
    playlist.onTrackAdded   = [this]()      { markDirty(); resized(); };
    playlist.onTrackRenamed = [this](int)   { markDirty(); };
    playlist.onTrackDeleted = [this](int)   { markDirty(); resized(); };

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
            pianoRollWindow->content.pianoRoll.onKeyPreview   = [this, ch](int pitch)
            {
                audioEngine.previewNote(ch, pitch);
            };
        }

        pianoRollWindow->setVisible(true);
        pianoRollWindow->toFront(true);
    };

    channelRack.onChannelTypeChanged = [this](int ch, ChannelType t)
    {
        project.channelTypes[ch] = t;
        channelRack.setChannelType(ch, t);
        markDirty();
    };

    // ---- M5: Mixer
    addAndMakeVisible(mixer);
    mixer.setVisible(false);   // hidden until toolbar toggle
    mixer.loadFromProject(project);
    mixer.updateRoutingLabels(project.channelMixerRouting);

    mixer.onTrackVolumeChanged  = [this](int t, float v) { audioEngine.setMixerTrackVolume(t, v); markDirty(); };
    mixer.onTrackPanChanged     = [this](int t, float p) { audioEngine.setMixerTrackPan(t, p);    markDirty(); };
    mixer.onTrackMuteChanged    = [this](int t, bool m)  { audioEngine.setMixerTrackMuted(t, m);  markDirty(); };
    mixer.onTrackSoloChanged    = [this](int t, bool s)  { audioEngine.setMixerTrackSoloed(t, s); markDirty(); };
    mixer.onMasterVolumeChanged = [this](float v)        { audioEngine.setMasterVolume(v);         markDirty(); };
    mixer.onMasterPanChanged    = [this](float p)        { audioEngine.setMasterPan(p);            markDirty(); };

    toolbar.onToggleMixer = [this] { showMixer = !showMixer; resized(); };

    setWantsKeyboardFocus(true);
    startTimerHz(30);
    setSize(1280, 780);
}

MainComponent::~MainComponent()
{
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

    // Load new pattern into channel rack
    if (auto* newPat = findPattern(activePatternId))
        channelRack.loadPattern(*newPat);

    toolbar.updatePatternList(project.patterns, activePatternId);
    audioEngine.setActivePattern(activePatternId);

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

    if (toolbar.getPlayMode() == PlayMode::Pattern)
    {
        audioEngine.setPatternStepCount(pat->stepCount);
        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            for (int s = 0; s < pat->stepCount; ++s)
                audioEngine.setStepPattern(ch, s, pat->steps[ch][s]);
    }
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
    channelRack.setPlaybackStep(-1);
    playlist.setPlayheadBar(-1.0);

    // Set active pattern to first in list
    activePatternId = project.patterns.empty() ? 1 : project.patterns.front().id;

    // Load first pattern into channel rack
    if (!project.patterns.empty())
        channelRack.loadPattern(project.patterns.front());

    // Refresh toolbar
    toolbar.updatePatternList(project.patterns, activePatternId);

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
        for (int ch = 0; ch < 16; ++ch)
            project.channelMixerRouting[ch] = ch % 8;
    }
    mixer.loadFromProject(project);
    mixer.updateRoutingLabels(project.channelMixerRouting);

    // M11 — ensure playlist tracks exist
    if (project.playlistTracks.empty())
        for (int t = 0; t < 8; ++t)
        {
            PlaylistTrack pt;
            pt.name = "Track " + juce::String(t + 1);
            project.playlistTracks.push_back(pt);
        }

    // M3 — sync channel types to channel rack
    for (int ch = 0; ch < 16; ++ch)
        channelRack.setChannelType(ch, project.channelTypes[ch]);

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

void MainComponent::saveProject()
{
    if (currentFile.existsAsFile())
    {
        // Flush current channel rack → active pattern before saving
        if (auto* pat = findPattern(activePatternId))
            channelRack.saveToPattern(*pat);

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
    // Space — play / stop
    if (key == juce::KeyPress(juce::KeyPress::spaceKey))
    {
        if (audioEngine.isPlaying())
        {
            audioEngine.stop();
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

    const int playlistHeight = (int)(area.getHeight() * 0.38f);
    auto playlistArea = area.removeFromTop(playlistHeight);

    // Snap box anchored to top-right, outside the scrollable area
    playlistSnapBox.setBounds(playlistArea.getRight() - 114, playlistArea.getY() + 2, 112, 20);

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
