/*
  ==============================================================================
    SampleBrowserComponent.h  —  M15 Sample Browser panel
    Header-only inline implementation.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "StudioLookAndFeel.h"

class SampleBrowserComponent : public juce::Component
{
public:
    // Fired when the user clicks a file — wire to audioEngine.previewBrowserFile()
    std::function<void(juce::File)> onPreviewFile;

    // Fired when the Stop button is clicked — wire to audioEngine.stopBrowserPreview()
    std::function<void()> onStopPreview;

    // Fired when the in-header collapse button is clicked
    std::function<void()> onCollapseClicked;

    SampleBrowserComponent()
    {
        addFolderBtn.setButtonText("+ FOLDER");
        addFolderBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
        addFolderBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kText));
        addFolderBtn.onClick = [this] { chooseAndAddBookmark(); };
        addAndMakeVisible(addFolderBtn);

        stopPreviewBtn.setButtonText("STOP");
        stopPreviewBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kAccent).withAlpha(0.18f));
        stopPreviewBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kAccent));
        stopPreviewBtn.onClick = [this] { if (onStopPreview) onStopPreview(); };
        addAndMakeVisible(stopPreviewBtn);

        searchField.setTextToShowWhenEmpty("Search...", juce::Colour(StudioLookAndFeel::kTextFaint));
        searchField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(StudioLookAndFeel::kDisplayBg));
        searchField.setColour(juce::TextEditor::textColourId,       juce::Colour(StudioLookAndFeel::kDisplayFg));
        searchField.setColour(juce::TextEditor::outlineColourId,    juce::Colour(StudioLookAndFeel::kPanelRim));
        searchField.onTextChange = [this] { rebuildList(); };
        addAndMakeVisible(searchField);

        collapseBtn.setButtonText("<");
        collapseBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(StudioLookAndFeel::kChassis));
        collapseBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextDim));
        collapseBtn.onClick = [this] { if (onCollapseClicked) onCollapseClicked(); };
        addAndMakeVisible(collapseBtn);

        for (auto* b : { &samplesTabBtn, &patchesTabBtn, &loopsTabBtn })
        {
            addAndMakeVisible(*b);
            b->setClickingTogglesState(false);
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis).darker(0.03f));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(StudioLookAndFeel::kTextDim));
        }
        samplesTabBtn.onClick = [this] { activeTab_ = 0; updateTabButtons(); };
        patchesTabBtn.onClick = [this] { activeTab_ = 1; updateTabButtons(); };
        loopsTabBtn.onClick   = [this] { activeTab_ = 2; updateTabButtons(); };
        updateTabButtons();

        listViewport.setViewedComponent(&listContent, false);
        listViewport.setScrollBarsShown(true, false);
        listViewport.setScrollBarThickness(6);
        addAndMakeVisible(listViewport);

        loadBookmarks();
        rebuildList();
    }

    void paint(juce::Graphics& g) override
    {
        using LF = StudioLookAndFeel;

        juce::ColourGradient panelGrad(juce::Colour(LF::kPanel), 0.0f, 0.0f,
                                       juce::Colour(LF::kChassis), 0.0f, (float)getHeight(),
                                       false);
        g.setGradientFill(panelGrad);
        g.fillAll();

        auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.drawRoundedRectangle(bounds, 7.0f, 1.0f);

        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine(0.0f, 29.0f, (float)getWidth(), 29.0f, 1.0f);

        g.setColour(juce::Colour(LF::kText));
        g.setFont(StudioLookAndFeel::monoFont(10.0f, juce::Font::bold));
        g.drawText("SAMPLE BROWSER", 10, 6, getWidth() - 20, 12, juce::Justification::centredLeft);

        auto tag = juce::Rectangle<int>(getWidth() - 88, 5, 78, 14);
        g.setColour(juce::Colour(LF::kChassis));
        g.fillRoundedRectangle(tag.toFloat(), 3.0f);
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawRoundedRectangle(tag.toFloat(), 3.0f, 1.0f);
        g.setColour(juce::Colour(LF::kTextFaint));
        g.setFont(StudioLookAndFeel::monoFont(8.0f, juce::Font::bold));
        g.drawText("1,284 SOUNDS", tag, juce::Justification::centred);

        auto footer = getLocalBounds().removeFromBottom(74);
        g.setColour(juce::Colour(LF::kPanelRim));
        g.drawLine(0.0f, (float)footer.getY(), (float)getWidth(), (float)footer.getY(), 1.0f);

        g.setColour(juce::Colour(LF::kTextFaint));
        g.setFont(StudioLookAndFeel::monoFont(8.0f, juce::Font::bold));
        g.drawText("NOW PREVIEWING", 10, footer.getY() + 6, 120, 10, juce::Justification::centredLeft);
        g.setColour(juce::Colour(LF::kText));
        g.setFont(StudioLookAndFeel::monoFont(10.0f, juce::Font::bold));
        g.drawText(currentPreviewName_.isNotEmpty() ? currentPreviewName_ : "none selected",
                   10, footer.getY() + 18, getWidth() - 34, 12, juce::Justification::centredLeft, true);

        g.setColour(juce::Colour(LF::kLedGreen));
        g.fillEllipse((float)getWidth() - 20.0f, (float)footer.getY() + 18.0f, 7.0f, 7.0f);

        auto display = juce::Rectangle<float>(10.0f, (float)footer.getY() + 34.0f,
                                              (float)getWidth() - 20.0f, 28.0f);
        g.setColour(juce::Colour(LF::kDisplayBg));
        g.fillRoundedRectangle(display, 3.0f);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawRoundedRectangle(display, 3.0f, 1.0f);
        g.setColour(juce::Colour(LF::kDisplayFg).withAlpha(0.75f));
        for (int i = 0; i < (int)display.getWidth() - 8; i += 3)
        {
            const float amp = std::sin((float)i * 0.14f + 0.8f) * 6.0f + std::sin((float)i * 0.05f) * 2.0f;
            const float cx = display.getX() + 4.0f + (float)i;
            const float mid = display.getCentreY();
            g.drawLine(cx, mid - amp, cx, mid + amp, 1.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto hdr  = area.removeFromTop(28).reduced(2, 2);
        addFolderBtn  .setBounds(hdr.removeFromLeft(84).reduced(1));
        stopPreviewBtn.setBounds(hdr.removeFromLeft(48).reduced(1));
        collapseBtn   .setBounds(hdr.removeFromRight(24).reduced(1)); // collapse tab on right
        searchField   .setBounds(hdr.reduced(2, 0));                  // search fills middle

        auto tabs = area.removeFromTop(24).reduced(8, 2);
        const int tabW = (tabs.getWidth() - 4) / 3;
        samplesTabBtn.setBounds(tabs.removeFromLeft(tabW));
        tabs.removeFromLeft(2);
        patchesTabBtn.setBounds(tabs.removeFromLeft(tabW));
        tabs.removeFromLeft(2);
        loopsTabBtn.setBounds(tabs);

        area.removeFromBottom(74);
        listViewport.setBounds(area);
        updateListSize();
    }

    static void closeSettings() { getProps().closeFiles(); }

private:
    // ---- data types -------------------------------------------------------
    struct Item
    {
        enum class Type { Folder, AudioFile } type;
        juce::File file;
        int indent = 0;
    };

    juce::Array<juce::File> bookmarks;
    juce::StringArray       expandedPaths;   // full paths of expanded folders
    std::vector<Item>       items;

    static constexpr int itemH = 24;

    // ---- widgets ----------------------------------------------------------
    juce::TextButton addFolderBtn   { "+ FOLDER" };
    juce::TextButton stopPreviewBtn { "STOP" };
    juce::TextButton collapseBtn    { "<" };
    juce::TextButton samplesTabBtn  { "SAMPLES" };
    juce::TextButton patchesTabBtn  { "PATCHES" };
    juce::TextButton loopsTabBtn    { "LOOPS" };
    juce::TextEditor searchField;
    juce::Viewport   listViewport;

    std::shared_ptr<juce::FileChooser> fileChooser;
    int activeTab_ = 0;
    juce::String currentPreviewName_;

    void updateTabButtons()
    {
        auto apply = [](juce::TextButton& b, bool on)
        {
            b.setColour(juce::TextButton::buttonColourId,
                        on ? juce::Colour(StudioLookAndFeel::kAccent)
                           : juce::Colour(StudioLookAndFeel::kChassis).darker(0.03f));
            b.setColour(juce::TextButton::textColourOffId,
                        on ? juce::Colours::white : juce::Colour(StudioLookAndFeel::kTextDim));
        };
        apply(samplesTabBtn, activeTab_ == 0);
        apply(patchesTabBtn, activeTab_ == 1);
        apply(loopsTabBtn,   activeTab_ == 2);
        repaint();
    }

    // ---- inner list component ---------------------------------------------
    struct ListContent : public juce::Component
    {
        SampleBrowserComponent& owner;
        int hoveredIdx   = -1;
        int mouseDownIdx = -1;
        juce::Point<int> mouseDownPos;

        explicit ListContent(SampleBrowserComponent& o) : owner(o) {}

        void paint(juce::Graphics& g) override
        {
            using LF = StudioLookAndFeel;

            juce::ColourGradient listGrad(juce::Colour(LF::kPanel), 0.0f, 0.0f,
                                          juce::Colour(LF::kChassis2), 0.0f, (float)getHeight(),
                                          false);
            g.setGradientFill(listGrad);
            g.fillAll();

            for (int i = 0; i < (int)owner.items.size(); ++i)
            {
                const auto& item    = owner.items[(size_t)i];
                const bool isFolder = (item.type == Item::Type::Folder);
                const int  rowY     = i * itemH;
                const int  indentX  = item.indent * 14;

                // Row background
                g.setColour(i == hoveredIdx
                    ? juce::Colour(LF::kAccent).withAlpha(0.08f)
                    : isFolder ? juce::Colour(LF::kChassis).brighter(0.03f)
                               : juce::Colour(LF::kPanel));
                g.fillRect(0, rowY, getWidth(), itemH);

                if (isFolder)
                {
                    const bool exp = owner.expandedPaths.contains(
                                         item.file.getFullPathName());
                    g.setColour(juce::Colour(LF::kTextFaint));
                    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
                    g.drawText(exp ? juce::String::fromUTF8("\xe2\x96\xbc")
                                   : juce::String::fromUTF8("\xe2\x96\xb6"),
                               4 + indentX, rowY, 12, itemH,
                               juce::Justification::centredLeft);
                    g.setColour(juce::Colour(LF::kAccent));
                    g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f).withStyle("Bold")));
                    g.drawText("DIR", 16 + indentX, rowY, 20, itemH,
                               juce::Justification::centredLeft);

                    g.setColour(juce::Colour(LF::kText));
                    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
                    g.drawText(item.file.getFileName(),
                               34 + indentX, rowY,
                               getWidth() - 34 - indentX - 4, itemH,
                               juce::Justification::centredLeft, true);
                }
                else
                {
                    g.setColour(juce::Colour(LF::kDisplayFg));
                    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
                    g.drawText("WAV",
                               22 + indentX, rowY, 14, itemH,
                               juce::Justification::centredLeft);

                    g.setColour(juce::Colour(LF::kTextDim));
                    g.drawText(item.file.getFileName(),
                               38 + indentX, rowY,
                               getWidth() - 38 - indentX - 4, itemH,
                               juce::Justification::centredLeft, true);
                }

                // Separator
                g.setColour(juce::Colour(LF::kPanelRim).withAlpha(0.5f));
                g.drawLine(0.0f, (float)(rowY + itemH - 1),
                           (float)getWidth(), (float)(rowY + itemH - 1), 0.5f);
            }
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            const int h = e.getPosition().y / itemH;
            if (h != hoveredIdx) { hoveredIdx = h; repaint(); }
        }

        void mouseExit(const juce::MouseEvent&) override
        {
            hoveredIdx = -1; repaint();
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            mouseDownIdx = e.getPosition().y / itemH;
            mouseDownPos = e.getPosition();
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            const int idx = e.getPosition().y / itemH;
            if (idx != mouseDownIdx ||
                idx < 0 || idx >= (int)owner.items.size()) return;

            const auto& item = owner.items[(size_t)idx];
            if (item.type == Item::Type::Folder)
            {
                const juce::String path = item.file.getFullPathName();
                if (owner.expandedPaths.contains(path))
                    owner.expandedPaths.removeString(path);
                else
                    owner.expandedPaths.add(path);
                owner.rebuildList();
            }
            else if (owner.onPreviewFile)
            {
                owner.currentPreviewName_ = item.file.getFileName();
                owner.repaint();
                owner.onPreviewFile(item.file);
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (mouseDownIdx < 0 ||
                mouseDownIdx >= (int)owner.items.size()) return;
            if (e.getPosition().getDistanceFrom(mouseDownPos) < 8) return;

            const auto& item = owner.items[(size_t)mouseDownIdx];
            if (item.type != Item::Type::AudioFile) return;

            if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                // Small drag image showing just the filename
                const juce::String name = item.file.getFileNameWithoutExtension();
                const int imgW = juce::jmin(180, name.length() * 7 + 20);
                juce::Image dragImg(juce::Image::ARGB, imgW, 20, true);
                {
                    juce::Graphics dg(dragImg);
                    dg.setColour(juce::Colour(StudioLookAndFeel::kAccent).withAlpha(0.9f));
                    dg.fillRoundedRectangle(dragImg.getBounds().toFloat(), 4.0f);
                    dg.setColour(juce::Colour(StudioLookAndFeel::kPanel));
                    dg.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
                    dg.drawText(name, dragImg.getBounds(), juce::Justification::centred);
                }
                dc->startDragging(item.file.getFullPathName(), this,
                                  juce::ScaledImage(dragImg), true);
            }

            mouseDownIdx = -1;
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ListContent)

    } listContent { *this };

    friend struct ListContent;

    // ---- bookmark persistence ---------------------------------------------
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

    void loadBookmarks()
    {
        bookmarks.clear();
        if (auto* settings = getSettings())
        {
            const int n = settings->getIntValue("sampleBrowserCount", 0);
            for (int i = 0; i < n; ++i)
            {
                juce::File f(settings->getValue(
                    "sampleBrowserPath" + juce::String(i)));
                if (f.isDirectory()) bookmarks.add(f);
            }
        }
    }

    void saveBookmarks()
    {
        auto* settings = getSettings();
        if (settings == nullptr) return;
        settings->setValue("sampleBrowserCount", bookmarks.size());
        for (int i = 0; i < bookmarks.size(); ++i)
            settings->setValue("sampleBrowserPath" + juce::String(i),
                               bookmarks[i].getFullPathName());
        settings->save();
    }

    void chooseAndAddBookmark()
    {
        fileChooser = std::make_shared<juce::FileChooser>(
            "Select Sample Folder",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory));

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f.isDirectory() && !bookmarks.contains(f))
                {
                    bookmarks.add(f);
                    saveBookmarks();
                    rebuildList();
                }
            });
    }

    // ---- list building (recursive) ----------------------------------------
    static const juce::StringArray& audioExts()
    {
        static const juce::StringArray exts {
            ".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg"
        };
        return exts;
    }

    void addFolderContents(const juce::File& folder,
                           int indent,
                           const juce::String& filter)
    {
        // Subfolders first
        juce::Array<juce::File> subDirs;
        folder.findChildFiles(subDirs, juce::File::findDirectories, false, "*");
        subDirs.sort();
        for (const auto& dir : subDirs)
        {
            Item item;
            item.type   = Item::Type::Folder;
            item.file   = dir;
            item.indent = indent;
            items.push_back(item);

            if (expandedPaths.contains(dir.getFullPathName()))
                addFolderContents(dir, indent + 1, filter);
        }

        // Audio files
        juce::Array<juce::File> allFiles;
        folder.findChildFiles(allFiles, juce::File::findFiles, false, "*");
        allFiles.sort();
        for (const auto& file : allFiles)
        {
            if (!audioExts().contains(file.getFileExtension().toLowerCase()))
                continue;
            if (!filter.isEmpty() &&
                !file.getFileName().toLowerCase().contains(filter))
                continue;

            Item item;
            item.type   = Item::Type::AudioFile;
            item.file   = file;
            item.indent = indent;
            items.push_back(item);
        }
    }

    void rebuildList()
    {
        const juce::String filter = searchField.getText().toLowerCase();
        items.clear();

        for (int fi = 0; fi < bookmarks.size(); ++fi)
        {
            Item folderItem;
            folderItem.type   = Item::Type::Folder;
            folderItem.file   = bookmarks[fi];
            folderItem.indent = 0;
            items.push_back(folderItem);

            if (expandedPaths.contains(bookmarks[fi].getFullPathName()))
                addFolderContents(bookmarks[fi], 1, filter);
        }

        updateListSize();
        listContent.repaint();
    }

    void updateListSize()
    {
        const int w = listViewport.getWidth() > 0 ? listViewport.getWidth() : 220;
        const int h = juce::jmax(listViewport.getHeight(),
                                 (int)items.size() * itemH);
        listContent.setSize(w, h);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBrowserComponent)
};
