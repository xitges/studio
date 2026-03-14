/*
  ==============================================================================
    SampleBrowserComponent.h  —  M15 Sample Browser panel
    Header-only inline implementation.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class SampleBrowserComponent : public juce::Component
{
public:
    // Fired when the user clicks a file — wire to audioEngine.previewBrowserFile()
    std::function<void(juce::File)> onPreviewFile;

    // Fired when the in-header collapse button is clicked
    std::function<void()> onCollapseClicked;

    SampleBrowserComponent()
    {
        addFolderBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0f3460));
        addFolderBtn.onClick = [this] { chooseAndAddBookmark(); };
        addAndMakeVisible(addFolderBtn);

        searchField.setTextToShowWhenEmpty("Search...", juce::Colours::grey);
        searchField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff11111f));
        searchField.setColour(juce::TextEditor::outlineColourId,    juce::Colour(0xff2c2c54));
        searchField.onTextChange = [this] { rebuildList(); };
        addAndMakeVisible(searchField);

        collapseBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1a1a2e));
        collapseBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8888aa));
        collapseBtn.onClick = [this] { if (onCollapseClicked) onCollapseClicked(); };
        addAndMakeVisible(collapseBtn);

        listViewport.setViewedComponent(&listContent, false);
        listViewport.setScrollBarsShown(true, false);
        listViewport.setScrollBarThickness(6);
        addAndMakeVisible(listViewport);

        loadBookmarks();
        rebuildList();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto hdr  = area.removeFromTop(28).reduced(2, 2);
        addFolderBtn.setBounds(hdr.removeFromLeft(64).reduced(1));   // slightly narrower
        collapseBtn .setBounds(hdr.removeFromRight(24).reduced(1));  // collapse tab on right
        searchField .setBounds(hdr.reduced(2, 0));                   // search fills middle
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

    static constexpr int itemH = 22;

    // ---- widgets ----------------------------------------------------------
    juce::TextButton addFolderBtn { "+ Folder" };
    juce::TextButton collapseBtn  { "<" };
    juce::TextEditor searchField;
    juce::Viewport   listViewport;

    std::shared_ptr<juce::FileChooser> fileChooser;

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
            g.fillAll(juce::Colour(0xff0e0e1a));

            for (int i = 0; i < (int)owner.items.size(); ++i)
            {
                const auto& item    = owner.items[(size_t)i];
                const bool isFolder = (item.type == Item::Type::Folder);
                const int  rowY     = i * itemH;
                const int  indentX  = item.indent * 14;

                // Row background
                g.setColour(i == hoveredIdx
                    ? juce::Colour(0xff1b2c3d)
                    : isFolder ? juce::Colour(0xff141422)
                               : juce::Colour(0xff0e0e1a));
                g.fillRect(0, rowY, getWidth(), itemH);

                if (isFolder)
                {
                    const bool exp = owner.expandedPaths.contains(
                                         item.file.getFullPathName());
                    g.setColour(juce::Colour(0xff3498db));
                    g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
                    g.drawText(exp ? juce::String::fromUTF8("\xe2\x96\xbc")
                                   : juce::String::fromUTF8("\xe2\x96\xb6"),
                               4 + indentX, rowY, 12, itemH,
                               juce::Justification::centredLeft);
                    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
                    g.drawText(juce::String::fromUTF8("\xf0\x9f\x93\x81"),
                               16 + indentX, rowY, 16, itemH,
                               juce::Justification::centredLeft);

                    g.setColour(juce::Colours::white.withAlpha(0.95f));
                    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
                    g.drawText(item.file.getFileName(),
                               34 + indentX, rowY,
                               getWidth() - 34 - indentX - 4, itemH,
                               juce::Justification::centredLeft, true);
                }
                else
                {
                    g.setColour(juce::Colour(0xff2ecc71));
                    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
                    g.drawText(juce::String::fromUTF8("\xe2\x99\xaa"),
                               22 + indentX, rowY, 14, itemH,
                               juce::Justification::centredLeft);

                    g.setColour(juce::Colours::white.withAlpha(0.78f));
                    g.drawText(item.file.getFileName(),
                               38 + indentX, rowY,
                               getWidth() - 38 - indentX - 4, itemH,
                               juce::Justification::centredLeft, true);
                }

                // Separator
                g.setColour(juce::Colour(0xff16161e));
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
                    dg.setColour(juce::Colour(0xff2ecc71).withAlpha(0.9f));
                    dg.fillRoundedRectangle(dragImg.getBounds().toFloat(), 4.0f);
                    dg.setColour(juce::Colours::white);
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
