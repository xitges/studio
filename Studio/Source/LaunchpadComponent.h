/*
  ==============================================================================
    LaunchpadComponent.h — True launchpad: each pad = one sample (one-shot)
                           Record pad sequence → convert to pattern
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "ProjectModel.h"

// ============================================================================
// LaunchpadPanel
// ============================================================================
class LaunchpadPanel : public juce::Component,
                       public juce::Timer,
                       public juce::FileDragAndDropTarget,
                       public juce::DragAndDropTarget
{
public:
    static constexpr int kRows = 8;
    static constexpr int kCols = 8;
    static constexpr int kPads = kRows * kCols;

    struct RecordedHit { int padIdx; double beatPos; };

    LaunchpadPanel()
    {
        setWantsKeyboardFocus(true);

        recBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff8b0000));
        recBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffdd0000));
        recBtn.setClickingTogglesState(true);
        recBtn.onClick = [this] { onRecButtonClicked(); };
        addAndMakeVisible(recBtn);

        barsBox.addItem("1 Bar",  1);
        barsBox.addItem("2 Bars", 2);
        barsBox.addItem("4 Bars", 4);
        barsBox.setSelectedId(2, juce::dontSendNotification);
        addAndMakeVisible(barsBox);

        convertBtn.setEnabled(false);
        convertBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a4060));
        convertBtn.onClick = [this] { onConvertClicked(); };
        addAndMakeVisible(convertBtn);

        saveDefaultBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a3a1a));
        saveDefaultBtn.onClick = [this] { saveDefaultPads(); };
        addAndMakeVisible(saveDefaultBtn);

        loadDefaultBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a2a3a));
        loadDefaultBtn.onClick = [this] { loadDefaultPads(); };
        addAndMakeVisible(loadDefaultBtn);

        stopAllBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff8b2020));
        stopAllBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        stopAllBtn.onClick = [this] { if (onStopAll) onStopAll(); };
        addAndMakeVisible(stopAllBtn);

        clearAllBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4a1a1a));
        clearAllBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        clearAllBtn.onClick = [this] { clearAllPads(); };
        addAndMakeVisible(clearAllBtn);

        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
        statusLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        statusLabel.setText("Drop samples onto pads", juce::dontSendNotification);
        addAndMakeVisible(statusLabel);
    }

    void setProject(Project* p) { project = p; repaint(); }

    bool handleKey(const juce::KeyPress& key)
    {
        // Escape → Stop All
        if (key == juce::KeyPress::escapeKey)
        {
            if (onStopAll) onStopAll();
            return true;
        }

        const int idx = padForKey(key);
        if (idx < 0) return false;
        triggerPad(idx);
        return true;
    }

    bool keyPressed(const juce::KeyPress& key) override { return handleKey(key); }

    void timerCallback() override
    {
        if (!isRecording) { stopTimer(); return; }

        const double elapsedBeats = getElapsedBeats();
        const int numBars = barsBox.getSelectedId();
        const double totalBeats = numBars * 4.0;

        statusLabel.setText("REC  " + juce::String(elapsedBeats, 2) + " / "
                            + juce::String(totalBeats, 1) + " beats  |  "
                            + juce::String(recordedHits.size()) + " hits",
                            juce::dontSendNotification);

        // Auto-stop after the specified bar count
        if (elapsedBeats >= totalBeats)
            stopRecording();
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent&)  override;

    // FileDragAndDropTarget (OS file drag from Finder etc.)
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void fileDragEnter(const juce::StringArray&, int x, int y) override
        { dragOverPad = padIndexAt(x, y - topBarHeight()); repaint(); }
    void fileDragMove(const juce::StringArray&, int x, int y) override
        { dragOverPad = padIndexAt(x, y - topBarHeight()); repaint(); }
    void fileDragExit(const juce::StringArray&) override
        { dragOverPad = -1; repaint(); }
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // DragAndDropTarget (internal JUCE drag from SampleBrowser)
    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& d) override
    {
        return juce::File(d.description.toString()).existsAsFile();
    }
    void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& d) override
        { dragOverPad = padIndexAt(d.localPosition.x, d.localPosition.y - topBarHeight()); repaint(); }
    void itemDragMove(const juce::DragAndDropTarget::SourceDetails& d) override
        { dragOverPad = padIndexAt(d.localPosition.x, d.localPosition.y - topBarHeight()); repaint(); }
    void itemDragExit(const juce::DragAndDropTarget::SourceDetails&) override
        { dragOverPad = -1; repaint(); }
    void itemDropped(const juce::DragAndDropTarget::SourceDetails& d) override
    {
        dragOverPad = -1;
        if (project == nullptr) return;
        const int idx = padIndexAt(d.localPosition.x, d.localPosition.y - topBarHeight());
        if (idx < 0) return;
        const juce::File file(d.description.toString());
        project->launchpadPads[(size_t)idx].filePath = file.getFullPathName();
        repaint();
        if (onSampleDropped) onSampleDropped(idx, file);
    }

    // Callbacks set by MainComponent
    std::function<void(int padIdx)>                                      onPadTriggered;
    std::function<void(int padIdx)>                                      onPadStopped;
    std::function<void()>                                                onStopAll;
    std::function<void()>                                                onClearAll;
    std::function<void()>                                                onDefaultsLoaded;
    std::function<void(int padIdx, juce::File)>                          onSampleDropped;
    std::function<void(const std::vector<RecordedHit>&, int numBars)>    onConvertToPattern;
    std::function<double()>                                              getBPM;

    // Called from MainComponent::keyPressed when window isn't focused
    static int padForKey(const juce::KeyPress& key)
    {
        static const char rows[4][9] = { "12345678", "qwertyui", "asdfghjk", "zxcvbnm," };
        const int ch = std::tolower((int)(unsigned char)key.getTextCharacter());
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 8; ++c)
                if (rows[r][c] == ch) return r * 8 + c;
        return -1;
    }

    static juce::String keyLabelForPad(int idx)
    {
        static const char rows[4][9] = { "12345678", "qwertyui", "asdfghjk", "zxcvbnm," };
        if (idx < 0 || idx >= kPads) return {};
        const int r = idx / kCols, c = idx % kCols;
        if (r >= 4) return {};
        return juce::String::charToString((juce::juce_wchar)rows[r][c]).toUpperCase();
    }

    static void closeSettings() { getProps().closeFiles(); }

private:
    Project* project   = nullptr;
    int hoveredPad     = -1;
    int dragOverPad    = -1;

    bool isRecording       = false;
    double recordStartTime = 0.0;
    std::vector<RecordedHit> recordedHits;

    juce::TextButton recBtn        { "REC" };
    juce::TextButton convertBtn   { "> Pattern" };
    juce::TextButton saveDefaultBtn { "Save Default" };
    juce::TextButton loadDefaultBtn { "Load Default" };
    juce::TextButton stopAllBtn     { "Stop All" };
    juce::TextButton clearAllBtn    { "Clear All" };
    juce::ComboBox   barsBox;
    juce::Label      statusLabel;

    // ---- default pad persistence ----------------------------------------
    static juce::ApplicationProperties& getProps()
    {
        static juce::ApplicationProperties props;
        static bool init = false;
        if (!init)
        {
            juce::PropertiesFile::Options o;
            o.applicationName     = "Studio";
            o.filenameSuffix      = ".settings";
            o.osxLibrarySubFolder = "Application Support";
            props.setStorageParameters(o);
            init = true;
        }
        return props;
    }

    static juce::PropertiesFile* getSettings() { return getProps().getUserSettings(); }

    void saveDefaultPads()
    {
        if (project == nullptr) return;
        auto* s = getSettings();
        if (s == nullptr) return;
        for (int i = 0; i < kPads; ++i)
        {
            const auto& pad = project->launchpadPads[(size_t)i];
            s->setValue("launchpadDefault" + juce::String(i), pad.filePath);
            s->setValue("launchpadDefaultMode" + juce::String(i), (int)pad.playMode);
        }
        s->save();
        statusLabel.setText("Default saved!", juce::dontSendNotification);
    }

    void loadDefaultPads()
    {
        if (project == nullptr) return;
        auto* s = getSettings();
        if (s == nullptr) return;
        for (int i = 0; i < kPads; ++i)
        {
            project->launchpadPads[(size_t)i].filePath =
                s->getValue("launchpadDefault" + juce::String(i), {});
            project->launchpadPads[(size_t)i].playMode =
                static_cast<PadPlayMode>(s->getIntValue("launchpadDefaultMode" + juce::String(i), 0));
        }
        repaint();
        if (onDefaultsLoaded) onDefaultsLoaded();
        statusLabel.setText("Defaults loaded!", juce::dontSendNotification);
    }

    static constexpr int kTopBarRowH = 30;
    static constexpr int kGap       = 4;

    int topBarHeight() const
    {
        // Two-row layout when width < 500, single row when wider
        return (getWidth() < 500) ? kTopBarRowH * 2 + 4 : kTopBarRowH + 8;
    }

    // ---- layout -------------------------------------------------------
    int padSize() const
    {
        const int avW = getWidth()  - 20;
        const int avH = getHeight() - topBarHeight() - 10;
        return juce::jmax(20, juce::jmin(avW / kCols, avH / kRows));
    }

    juce::Rectangle<int> padBounds(int idx) const
    {
        const int ps     = padSize();
        const int totalW = kCols * ps + (kCols - 1) * kGap;
        const int totalH = kRows * ps + (kRows - 1) * kGap;
        const int ox     = (getWidth()  - totalW) / 2;
        const int oy     = topBarHeight() + 6;  // 상단 바 바로 아래에 붙임
        return { ox + (idx % kCols) * (ps + kGap),
                 oy + (idx / kCols) * (ps + kGap), ps, ps };
    }

    int padIndexAt(int x, int y) const   // y is relative to pad area (below top bar)
    {
        for (int i = 0; i < kPads; ++i)
        {
            const auto b = padBounds(i);
            if (b.contains(x, y + topBarHeight())) return i;
        }
        return -1;
    }

    // ---- recording helpers --------------------------------------------
    double getElapsedBeats() const
    {
        const double bpm = (getBPM ? getBPM() : 120.0);
        const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - recordStartTime;
        return (elapsedMs / 1000.0) * (bpm / 60.0);
    }

    void triggerPad(int idx)
    {
        if (project == nullptr) return;
        if (project->launchpadPads[(size_t)idx].filePath.isEmpty()) return;

        if (isRecording)
            recordedHits.push_back({ idx, getElapsedBeats() });

        if (onPadTriggered) onPadTriggered(idx);
        repaint();
    }

    void stopPad(int idx)
    {
        if (onPadStopped) onPadStopped(idx);
    }

    void onRecButtonClicked()
    {
        if (recBtn.getToggleState())
            startRecording();
        else
            stopRecording();
    }

    void startRecording()
    {
        recordedHits.clear();
        recordStartTime = juce::Time::getMillisecondCounterHiRes();
        isRecording = true;
        convertBtn.setEnabled(false);
        statusLabel.setText("Recording...", juce::dontSendNotification);
        startTimer(50);
    }

    void stopRecording()
    {
        isRecording = false;
        recBtn.setToggleState(false, juce::dontSendNotification);
        stopTimer();
        const bool hasHits = !recordedHits.empty();
        convertBtn.setEnabled(hasHits);
        statusLabel.setText(hasHits
            ? "Recorded " + juce::String(recordedHits.size()) + juce::String::fromUTF8(" hits \xe2\x80\x94 ready to convert")
            : "Recording stopped (no hits)",
            juce::dontSendNotification);
    }

    void onConvertClicked()
    {
        if (recordedHits.empty()) return;
        const int numBars = barsBox.getSelectedId();
        if (onConvertToPattern) onConvertToPattern(recordedHits, numBars);
        convertBtn.setEnabled(false);
        recordedHits.clear();
        statusLabel.setText("Pattern created!", juce::dontSendNotification);
    }

    void clearAllPads()
    {
        if (project == nullptr) return;
        for (int i = 0; i < kPads; ++i)
            project->launchpadPads[(size_t)i].filePath = {};
        repaint();
        if (onClearAll) onClearAll();
        statusLabel.setText("All pads cleared", juce::dontSendNotification);
    }

    void showContextMenu(int padIdx);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaunchpadPanel)
};

// ============================================================================
// Inline implementations
// ============================================================================

inline void LaunchpadPanel::resized()
{
    const bool narrow = getWidth() < 500;

    if (narrow)
    {
        // Two-row layout for inline panel
        auto topArea = getLocalBounds().removeFromTop(topBarHeight()).reduced(4, 2);
        auto row1 = topArea.removeFromTop(kTopBarRowH);
        auto row2 = topArea;

        // Row 1: REC, bars, convert, stop all
        recBtn         .setBounds(row1.removeFromLeft(44).reduced(1));
        row1.removeFromLeft(3);
        barsBox        .setBounds(row1.removeFromLeft(62).reduced(1));
        row1.removeFromLeft(3);
        convertBtn     .setBounds(row1.removeFromLeft(72).reduced(1));
        row1.removeFromLeft(3);
        stopAllBtn     .setBounds(row1.removeFromLeft(58).reduced(1));
        row1.removeFromLeft(4);
        statusLabel    .setBounds(row1);

        // Row 2: save, load, clear
        saveDefaultBtn .setBounds(row2.removeFromLeft(78).reduced(1));
        row2.removeFromLeft(3);
        loadDefaultBtn .setBounds(row2.removeFromLeft(78).reduced(1));
        row2.removeFromLeft(3);
        clearAllBtn    .setBounds(row2.removeFromLeft(68).reduced(1));
    }
    else
    {
        // Single-row layout (original — for floating window / wide panel)
        auto bar = getLocalBounds().removeFromTop(topBarHeight()).reduced(6, 6);
        recBtn         .setBounds(bar.removeFromLeft(50).reduced(1));
        bar.removeFromLeft(4);
        barsBox        .setBounds(bar.removeFromLeft(68).reduced(1));
        bar.removeFromLeft(4);
        convertBtn     .setBounds(bar.removeFromLeft(80).reduced(1));
        bar.removeFromLeft(4);
        saveDefaultBtn .setBounds(bar.removeFromLeft(84).reduced(1));
        bar.removeFromLeft(4);
        loadDefaultBtn .setBounds(bar.removeFromLeft(84).reduced(1));
        bar.removeFromLeft(4);
        clearAllBtn    .setBounds(bar.removeFromLeft(64).reduced(1));
        bar.removeFromLeft(4);
        stopAllBtn     .setBounds(bar.removeFromLeft(64).reduced(1));
        bar.removeFromLeft(6);
        statusLabel    .setBounds(bar);
    }
}

inline void LaunchpadPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d0d1a));

    // Top bar background
    g.setColour(juce::Colour(0xff16213e));
    g.fillRect(0, 0, getWidth(), topBarHeight());
    g.setColour(juce::Colour(0xff0f3460));
    g.drawLine(0.0f, (float)topBarHeight(), (float)getWidth(), (float)topBarHeight(), 1.0f);

    // Subtle divider after keyboard rows (rows 0-3)
    {
        const int ps     = padSize();
        const int totalW = kCols * ps + (kCols - 1) * kGap;
        const int ox     = (getWidth() - totalW) / 2;
        const int divY   = padBounds(4 * kCols).getY() - kGap / 2;
        g.setColour(juce::Colour(0xff3498db).withAlpha(0.15f));
        g.drawLine((float)ox, (float)divY, (float)(ox + totalW), (float)divY, 1.5f);
    }

    // Draw pads
    for (int i = 0; i < kPads; ++i)
    {
        const auto b = padBounds(i).toFloat();
        const bool hasFile = (project != nullptr) &&
                             !project->launchpadPads[(size_t)i].filePath.isEmpty();
        const bool hover    = (i == hoveredPad);
        const bool dragOver = (i == dragOverPad);

        // Colour: unique per-pad hue so all 64 look distinct
        juce::Colour col;
        if (hasFile)
        {
            const float hue = (float)(i % 16) / 16.0f;
            col = juce::Colour::fromHSV(hue, 0.7f, 0.6f, 1.0f);
        }
        else
        {
            col = juce::Colour(0xff1a1a2e);
        }

        if (dragOver) col = juce::Colour(0xff3498db);
        if (hover && !dragOver) col = col.brighter(0.18f);

        // Recording indicator on last-hit pad
        const bool justHit = (!recordedHits.empty() &&
                               recordedHits.back().padIdx == i &&
                               isRecording);
        if (justHit) col = col.brighter(0.5f);

        g.setColour(col);
        g.fillRoundedRectangle(b, 6.0f);

        const juce::Colour border = dragOver ? juce::Colour(0xff2980b9)
                                             : (hasFile ? col.brighter(0.35f)
                                                        : juce::Colour(0xff2c2c54));
        g.setColour(border);
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);

        // Sample name (filename without extension)
        if (hasFile)
        {
            const auto fname = juce::File(project->launchpadPads[(size_t)i].filePath)
                               .getFileNameWithoutExtension();
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            const int ps = padSize();
            g.setFont(juce::Font(juce::FontOptions().withHeight(
                                 (float)juce::jmax(8, juce::jmin(11, ps / 6)))));
            g.drawFittedText(fname, b.reduced(3.0f, 8.0f).toNearestInt(),
                             juce::Justification::centred, 2);
        }
        else if (dragOver)
        {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
            g.drawFittedText("Drop", b.reduced(3.0f).toNearestInt(),
                             juce::Justification::centred, 1);
        }

        // Play mode indicator (bottom-right corner)
        if (hasFile && project->launchpadPads[(size_t)i].playMode != PadPlayMode::OneShot)
        {
            const auto mode = project->launchpadPads[(size_t)i].playMode;
            const juce::String ml = (mode == PadPlayMode::Loop) ? "L" : "G";
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            g.drawText(ml, (int)b.getRight() - 14, (int)b.getBottom() - 13, 11, 11,
                       juce::Justification::centredRight);
        }

        // Keyboard shortcut label (top-left, pads 0-31)
        const juce::String kl = keyLabelForPad(i);
        if (kl.isNotEmpty())
        {
            g.setColour(juce::Colours::white.withAlpha(hasFile ? 0.4f : 0.15f));
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            g.drawText(kl, (int)b.getX() + 3, (int)b.getY() + 2, 14, 10,
                       juce::Justification::centredLeft);
        }
    }

    // Recording indicator overlay
    if (isRecording)
    {
        g.setColour(juce::Colour(0xffdd0000).withAlpha(0.12f));
        g.fillRect(0, topBarHeight(), getWidth(), getHeight() - topBarHeight());
    }
}

inline void LaunchpadPanel::mouseDown(const juce::MouseEvent& e)
{
    const int idx = padIndexAt(e.x, e.y - topBarHeight());
    if (idx < 0) return;

    if (e.mods.isRightButtonDown()) { showContextMenu(idx); return; }

    triggerPad(idx);
}

inline void LaunchpadPanel::mouseMove(const juce::MouseEvent& e)
{
    const int idx = padIndexAt(e.x, e.y - topBarHeight());
    if (idx != hoveredPad) { hoveredPad = idx; repaint(); }
}

inline void LaunchpadPanel::mouseExit(const juce::MouseEvent&)
{
    hoveredPad = -1; repaint();
}

inline void LaunchpadPanel::filesDropped(const juce::StringArray& files, int x, int y)
{
    dragOverPad = -1;
    if (files.isEmpty() || project == nullptr) return;

    const int idx = padIndexAt(x, y - topBarHeight());
    if (idx < 0) return;

    const juce::File file(files[0]);
    project->launchpadPads[(size_t)idx].filePath = file.getFullPathName();
    repaint();

    if (onSampleDropped) onSampleDropped(idx, file);
}

inline void LaunchpadPanel::showContextMenu(int padIdx)
{
    if (project == nullptr) return;

    const bool hasFile = !project->launchpadPads[(size_t)padIdx].filePath.isEmpty();

    juce::PopupMenu menu;
    menu.addItem(1, "Load Sample...");
    if (hasFile)
    {
        menu.addSeparator();
        menu.addItem(2, "Clear");

        // Play mode submenu
        const auto currentMode = project->launchpadPads[(size_t)padIdx].playMode;
        juce::PopupMenu modeMenu;
        modeMenu.addItem(10, "One-Shot", true, currentMode == PadPlayMode::OneShot);
        modeMenu.addItem(11, "Loop",     true, currentMode == PadPlayMode::Loop);
        modeMenu.addItem(12, "Gate",     true, currentMode == PadPlayMode::Gate);
        menu.addSubMenu("Play Mode", modeMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [this, padIdx](int result)
        {
            if (result == 1)
            {
                auto chooser = std::make_shared<juce::FileChooser>(
                    "Select Sample", juce::File{},
                    "*.wav;*.aif;*.aiff;*.mp3;*.flac");

                chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                     juce::FileBrowserComponent::canSelectFiles,
                    [this, padIdx, chooser](const juce::FileChooser& fc)
                    {
                        if (fc.getResults().isEmpty()) return;
                        const juce::File file = fc.getResult();
                        if (project != nullptr)
                            project->launchpadPads[(size_t)padIdx].filePath =
                                file.getFullPathName();
                        repaint();
                        if (onSampleDropped) onSampleDropped(padIdx, file);
                    });
            }
            else if (result == 2)
            {
                if (project != nullptr)
                    project->launchpadPads[(size_t)padIdx].filePath = {};
                repaint();
            }
            else if (result >= 10 && result <= 12 && project != nullptr)
            {
                project->launchpadPads[(size_t)padIdx].playMode =
                    static_cast<PadPlayMode>(result - 10);
                repaint();
            }
        });
}

// ============================================================================
// LaunchpadWindow
// ============================================================================
class LaunchpadWindow : public juce::DocumentWindow
{
public:
    LaunchpadPanel panel;

    LaunchpadWindow()
        : juce::DocumentWindow("Launchpad",
                               juce::Colour(0xff11111f),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(false);
        setContentNonOwned(&panel, false);
        setResizable(true, false);
        setSize(600, 660);
        setTopLeftPosition(200, 100);
    }

    void closeButtonPressed() override { setVisible(false); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaunchpadWindow)
};
