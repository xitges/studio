/*
  ==============================================================================
    SampleBrowserComponent.h  —  Sample Browser (reference: browser.jsx)
    Header-only inline implementation. Phase 2 design.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "StudioLookAndFeel.h"

class SampleBrowserComponent : public juce::Component
{
public:
    std::function<void(juce::File)> onPreviewFile;
    std::function<void()>           onStopPreview;
    std::function<void()>           onCollapseClicked;
    std::function<void(juce::File)> onDropToTrack;

    SampleBrowserComponent()
    {
        using LF = StudioLookAndFeel;

        // ---- Header: + bookmark button ----
        addBookmarkBtn_.setButtonText("+");
        addBookmarkBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
        addBookmarkBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(LF::kTextDim));
        addBookmarkBtn_.setTooltip("Add sample folder bookmark");
        addBookmarkBtn_.onClick = [this] { chooseAndAddBookmark(); };
        addAndMakeVisible(addBookmarkBtn_);

        // ---- Header: refresh / collapse buttons ----
        refreshBtn_.setButtonText(juce::String::fromUTF8("\xe2\x86\xbb")); // ↻
        refreshBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
        refreshBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(LF::kTextDim));
        refreshBtn_.setTooltip("Refresh");
        refreshBtn_.onClick = [this] { rebuildList(); };
        addAndMakeVisible(refreshBtn_);

        collapseBtn_.setButtonText(juce::String::fromUTF8("\xc2\xab")); // «
        collapseBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
        collapseBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(LF::kTextDim));
        collapseBtn_.onClick = [this] { if (onCollapseClicked) onCollapseClicked(); };
        addAndMakeVisible(collapseBtn_);

        // ---- Search prefix "›" ----
        searchPrefix_.setText(juce::String::fromUTF8("\xe2\x80\xba"), juce::dontSendNotification);
        searchPrefix_.setFont(LF::displayFont(15.0f));
        searchPrefix_.setColour(juce::Label::textColourId,
                                juce::Colour(LF::kDisplayFg).withAlpha(0.9f));
        searchPrefix_.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        searchPrefix_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(searchPrefix_);

        // ---- Clear search × ----
        clearSearchBtn_.setButtonText(juce::String::fromUTF8("\xc3\x97")); // ×
        clearSearchBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
        clearSearchBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(LF::kTextFaint));
        clearSearchBtn_.onClick = [this] { searchField_.clear(); rebuildList(); };
        addAndMakeVisible(clearSearchBtn_);

        // ---- Search field ----
        searchField_.setTextToShowWhenEmpty("search sounds...",
                                            juce::Colour(LF::kDisplayFg).withAlpha(0.3f));
        searchField_.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        searchField_.setColour(juce::TextEditor::textColourId,       juce::Colour(LF::kDisplayFg));
        searchField_.setColour(juce::TextEditor::outlineColourId,    juce::Colours::transparentBlack);
        searchField_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        searchField_.setFont(LF::displayFont(13.0f));
        searchField_.onTextChange = [this] { rebuildList(); };
        addAndMakeVisible(searchField_);

        // ---- Footer: DRAG TO RACK ----
        dropToTrackBtn_.setButtonText("DRAG TO RACK");
        dropToTrackBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(LF::kAccent));
        dropToTrackBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        dropToTrackBtn_.onClick = [this]
        {
            if (currentPreviewFile_.existsAsFile() && onDropToTrack)
                onDropToTrack(currentPreviewFile_);
        };
        addAndMakeVisible(dropToTrackBtn_);

        // ---- Footer: ▦ show in finder ----
        footerGridBtn_.setButtonText(juce::String::fromUTF8("\xe2\x96\xa6")); // ▦
        footerGridBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
        footerGridBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(LF::kTextDim));
        footerGridBtn_.setTooltip("Reveal in Finder");
        footerGridBtn_.onClick = [this]
        {
            if (currentPreviewFile_.existsAsFile())
                currentPreviewFile_.revealToUser();
        };
        addAndMakeVisible(footerGridBtn_);

        // ---- Footer: ↻ replay preview ----
        footerReplayBtn_.setButtonText(juce::String::fromUTF8("\xe2\x86\xbb")); // ↻
        footerReplayBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colours::transparentBlack);
        footerReplayBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(LF::kTextDim));
        footerReplayBtn_.setTooltip("Replay preview");
        footerReplayBtn_.onClick = [this]
        {
            if (currentPreviewFile_.existsAsFile() && onPreviewFile)
                onPreviewFile(currentPreviewFile_);
        };
        addAndMakeVisible(footerReplayBtn_);

        // ---- List viewport ----
        listViewport_.setViewedComponent(&listContent_, false);
        listViewport_.setScrollBarsShown(true, false);
        listViewport_.setScrollBarThickness(5);
        addAndMakeVisible(listViewport_);

        loadBookmarks();
        rebuildList();
    }

    void setCurrentPreviewName(const juce::String& name)
    {
        currentPreviewName_ = name;
        repaint();
    }

    static void closeSettings() { getProps().closeFiles(); }

    void paint(juce::Graphics& g) override
    {
        using LF = StudioLookAndFeel;
        const float W   = (float)getWidth();
        const float H   = (float)getHeight();
        const float hdr = (float)kHdrH;
        const float ftr = (float)kFtrH;

        // Browser-local lime green palette
        constexpr juce::uint32 brPanel   = 0xfff7f7f8u;
        constexpr juce::uint32 brChassis = 0xffebebeeu;
        constexpr juce::uint32 brChassis2= 0xffe0e0e4u;
        constexpr juce::uint32 brRim     = 0xffccccd0u;

        // ---- Container ----
        {
            const juce::Rectangle<float> bounds(0.5f, 0.5f, W - 1.0f, H - 1.0f);
            g.setColour(juce::Colour(brPanel));
            g.fillRoundedRectangle(bounds, 8.0f);
            g.setColour(juce::Colour(brRim));
            g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawLine(8.0f, 1.0f, W - 8.0f, 1.0f, 1.0f);
        }

        // ---- Header ----
        {
            juce::ColourGradient hdrBg(juce::Colour(brChassis),  0.0f, 0.0f,
                                       juce::Colour(brChassis2), 0.0f, hdr, false);
            g.setGradientFill(hdrBg);
            g.fillRoundedRectangle(0.5f, 0.5f, W - 1.0f, hdr, 8.0f);
            g.fillRect(0.5f, hdr * 0.5f, W - 1.0f, hdr * 0.5f);
            g.setColour(juce::Colour(brRim));
            g.drawLine(1.0f, hdr, W - 1.0f, hdr, 1.0f);

            // Row 1: accent square + "BROWSER" + count tag
            const int by = 10;
            g.setColour(juce::Colour(LF::kAccent));
            g.fillRoundedRectangle(12.0f, (float)by + 3.0f, 8.0f, 8.0f, 2.0f);

            g.setFont(LF::monoFont(10.0f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kText));
            g.drawText("BROWSER", 28, by, 80, 14, juce::Justification::centredLeft);

            const juce::Rectangle<float> tag(114.0f, (float)by, 52.0f, 14.0f);
            g.setColour(juce::Colour(brChassis2).withAlpha(0.7f));
            g.fillRoundedRectangle(tag, 3.0f);
            g.setColour(juce::Colour(brRim));
            g.drawRoundedRectangle(tag, 3.0f, 0.5f);
            g.setFont(LF::monoFont(7.0f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kTextFaint));
            g.drawText(juce::String(bookmarks.size()) + " BKM",
                       tag.reduced(2.0f, 0.0f).toNearestInt(), juce::Justification::centred);

            // Search box bg
            if (searchBoxBounds_.getWidth() > 0)
            {
                const juce::Rectangle<float> sb(searchBoxBounds_.toFloat());
                g.setColour(juce::Colour(LF::kDisplayBg));
                g.fillRoundedRectangle(sb, 3.0f);
                g.setColour(juce::Colour(0x30000000));
                g.fillRoundedRectangle(sb.withHeight(4.0f), 3.0f);
                g.setColour(juce::Colour(0x80000000));
                g.drawRoundedRectangle(sb.reduced(0.5f), 3.0f, 1.0f);
            }
        }

        // ---- Footer ----
        {
            const float fy = H - ftr;
            juce::ColourGradient ftrBg(juce::Colour(brChassis2), 0.0f, fy,
                                       juce::Colour(brChassis),  0.0f, H, false);
            g.setGradientFill(ftrBg);
            g.fillRoundedRectangle(0.5f, fy, W - 1.0f, ftr - 0.5f, 8.0f);
            g.fillRect(0.5f, fy, W - 1.0f, ftr * 0.5f);
            g.setColour(juce::Colour(brRim));
            g.drawLine(1.0f, fy, W - 1.0f, fy, 1.0f);

            const int px = 10, pw = (int)W - 20;
            int ry = (int)fy + 6;

            g.setFont(LF::monoFont(7.5f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kTextFaint));
            g.drawText("NOW PREVIEWING", px, ry, pw - 18, 10, juce::Justification::centredLeft);

            const juce::Colour ledCol = currentPreviewName_.isNotEmpty()
                ? juce::Colour(LF::kLedGreen) : juce::Colour(LF::kLedOff);
            g.setColour(ledCol);
            g.fillEllipse(W - 18.0f, (float)ry + 1.5f, 7.0f, 7.0f);

            ry += 13;
            g.setFont(LF::monoFont(10.0f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kText));
            const juce::String name = currentPreviewName_.isNotEmpty()
                                     ? currentPreviewName_ : "none selected";
            g.drawText(name, px, ry, pw, 13, juce::Justification::centredLeft, true);

            ry += 16;
            const juce::Rectangle<float> wdBox((float)px, (float)ry, (float)pw, 30.0f);
            g.setColour(juce::Colour(LF::kDisplayBg));
            g.fillRoundedRectangle(wdBox, 3.0f);
            g.setColour(juce::Colour(0x28000000));
            g.fillRoundedRectangle(wdBox.withHeight(5.0f), 3.0f);
            g.setColour(juce::Colour(0x80000000));
            g.drawRoundedRectangle(wdBox.reduced(0.5f), 3.0f, 1.0f);
            drawMiniWave(g, wdBox.reduced(6.0f, 4.0f),
                         previewSeed_,
                         juce::Colour(LF::kDisplayFg).withAlpha(0.85f));
        }
    }

    void resized() override
    {
        const int W = getWidth(), H = getHeight();

        // Header Row 1: right-side buttons (right to left: «  ↻  +)
        collapseBtn_   .setBounds(W - 26, 8, 20, 18);
        refreshBtn_    .setBounds(W - 50, 8, 20, 18);
        addBookmarkBtn_.setBounds(W - 74, 8, 20, 18);

        // Header Row 2: search (y=36)
        {
            const int sy = 36, sh = 24, sx = 10, sw = W - 20;
            searchBoxBounds_ = { sx, sy, sw, sh };
            searchPrefix_   .setBounds(sx,           sy,     20,    sh);
            clearSearchBtn_ .setBounds(sx + sw - 22, sy + 2, 20,    sh - 4);
            searchField_    .setBounds(sx + 20,      sy + 3, sw - 44, sh - 6);
        }

        listViewport_.setBounds(0, kHdrH, W, H - kHdrH - kFtrH);
        updateListSize();

        // Footer buttons
        {
            const int btnY = H - kFtrH + 76;
            const int btnH = 22, px = 10, pw = W - 20;
            dropToTrackBtn_ .setBounds(px,             btnY, pw - 52, btnH);
            footerGridBtn_  .setBounds(px + pw - 50,   btnY, 24,      btnH);
            footerReplayBtn_.setBounds(px + pw - 24,   btnY, 24,      btnH);
        }
    }

private:
    static constexpr int kHdrH    = 66;
    static constexpr int kFtrH    = 106;
    static constexpr int kItemH   = 26;
    static constexpr int kFolderH = 22;

    struct Item
    {
        enum class Type { Folder, AudioFile } type;
        juce::File file;
        int indent = 0;
    };
    juce::Array<juce::File> bookmarks;
    juce::StringArray        expandedPaths;
    std::vector<Item>        items;

    juce::String         currentPreviewName_;
    juce::File           currentPreviewFile_;
    int                  activeItemIdx_ = -1;
    int                  previewSeed_   = 11;
    juce::Rectangle<int> searchBoxBounds_;

    juce::Label      searchPrefix_;
    juce::TextEditor searchField_;
    juce::TextButton addBookmarkBtn_;
    juce::TextButton refreshBtn_;
    juce::TextButton collapseBtn_;
    juce::TextButton clearSearchBtn_;
    juce::TextButton dropToTrackBtn_;
    juce::TextButton footerGridBtn_;
    juce::TextButton footerReplayBtn_;
    juce::Viewport   listViewport_;
    std::shared_ptr<juce::FileChooser> fileChooser_;

    static int hashSeed(const juce::String& s)
    {
        unsigned int h = 5381u;
        for (auto c : s) h = h * 31u + (unsigned)c;
        return (int)(h % 997u) + 7;
    }

    // 8-color hash palette
    static juce::Colour fileColor(const juce::String& name)
    {
        static const juce::uint32 pal[] = {
            0xffd8412a, 0xffe89c2b, 0xff7ab87a, 0xff5fa8d8,
            0xffb87ad6, 0xff5dd8c8, 0xffe87a7a, 0xffb8b87a
        };
        unsigned int h = 0;
        for (auto c : name) h = h * 31u + (unsigned)c;
        return juce::Colour(pal[h % 8]);
    }

    static void drawMiniWave(juce::Graphics& g,
                             const juce::Rectangle<float>& rect,
                             int seed, juce::Colour col)
    {
        const int N = 60;
        const float step = rect.getWidth() / (float)N;
        int s = seed;
        g.setColour(col);
        for (int i = 0; i < N; ++i)
        {
            s = (s * 9301 + 49297) % 233280;
            const float r   = (float)s / 233280.0f;
            const float env = std::sin((float)i / N * juce::MathConstants<float>::pi) * 0.7f + 0.3f;
            const float v   = ((r - 0.5f) * 1.6f + std::sin((float)i * 0.3f) * 0.4f) * env;
            const float bh  = std::abs(v) * (rect.getHeight() * 0.5f - 1.0f);
            const float bx  = rect.getX() + (float)i * step;
            const float by  = rect.getCentreY() - bh;
            g.fillRect(bx, by, 0.8f, bh * 2.0f);
        }
    }

    // ---- Inner list component ----
    struct ListContent : public juce::Component
    {
        SampleBrowserComponent& owner;
        int hoveredIdx   = -1;
        int mouseDownIdx = -1;
        juce::Point<int> mouseDownPos;

        explicit ListContent(SampleBrowserComponent& o) : owner(o) {}

        int rowHeight(int i) const
        {
            return (owner.items[(size_t)i].type == Item::Type::Folder) ? kFolderH : kItemH;
        }

        int yForRow(int idx) const
        {
            int y = 0;
            for (int i = 0; i < idx; ++i) y += rowHeight(i);
            return y;
        }

        int rowAtY(int my) const
        {
            int y = 0;
            for (int i = 0; i < (int)owner.items.size(); ++i)
            {
                const int h = rowHeight(i);
                if (my >= y && my < y + h) return i;
                y += h;
            }
            return -1;
        }

        void paint(juce::Graphics& g) override
        {
            using LF = StudioLookAndFeel;
            const int W = getWidth();

            g.setColour(juce::Colour(0xfff7f7f8u));
            g.fillAll();

            // Empty state: no bookmarks
            if (owner.bookmarks.isEmpty())
            {
                g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 26.0f, juce::Font::plain)));
                g.setColour(juce::Colour(LF::kAccent).withAlpha(0.35f));
                // ★
                g.drawText(juce::String::fromUTF8("\xe2\x98\x85"),
                           0, 18, W, 32, juce::Justification::centred);
                g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 9.0f, juce::Font::bold)));
                g.setColour(juce::Colour(LF::kText));
                g.drawText("ADD FOLDER", 0, 54, W, 14, juce::Justification::centred);
                g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 7.5f, juce::Font::plain)));
                g.setColour(juce::Colour(LF::kTextFaint));
                g.drawText("Click + to add a sample folder", 0, 70, W, 12, juce::Justification::centred);
                return;
            }

            // Empty state: search returned nothing
            if (owner.items.empty())
            {
                g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 9.0f, juce::Font::plain)));
                g.setColour(juce::Colour(LF::kTextFaint));
                g.drawText("No results.", 0, 40, W, 14, juce::Justification::centred);
                return;
            }

            int y = 0;
            for (int i = 0; i < (int)owner.items.size(); ++i)
            {
                const auto& item     = owner.items[(size_t)i];
                const bool  isFolder = (item.type == Item::Type::Folder);
                const bool  isActive = (i == owner.activeItemIdx_);
                const bool  isHover  = (i == hoveredIdx);
                const int   rh       = isFolder ? kFolderH : kItemH;
                const int   indent   = 8 + item.indent * 12;

                // Row bg
                if (isActive)
                {
                    g.setColour(juce::Colour(LF::kAccent).withAlpha(isFolder ? 0.12f : 0.14f));
                    g.fillRect(0, y, W, rh);
                }
                else if (isHover)
                {
                    g.setColour(juce::Colour(LF::kAccent).withAlpha(0.06f));
                    g.fillRect(0, y, W, rh);
                }

                // Left border
                if (isActive)
                {
                    g.setColour(juce::Colour(LF::kAccent));
                    g.fillRect(0, y, 2, rh);
                }

                if (isFolder)
                {
                    const bool exp = owner.expandedPaths.contains(item.file.getFullPathName());
                    const auto arrowStr = exp
                        ? juce::String::fromUTF8("\xe2\x96\xbe")   // ▾
                        : juce::String::fromUTF8("\xe2\x96\xb8");  // ▸

                    g.setFont(juce::Font(juce::FontOptions("VT323", 13.0f, juce::Font::plain)));

                    if (item.indent == 0)
                    {
                        // Bookmark root: ★ (accent) + expand arrow + name uppercase
                        g.setColour(isActive ? juce::Colour(LF::kAccent)
                                             : juce::Colour(LF::kAccent).withAlpha(0.75f));
                        g.drawText(juce::String::fromUTF8("\xe2\x98\x85"), // ★
                                   indent, y, 12, rh, juce::Justification::centredLeft);
                        g.setColour(juce::Colour(LF::kTextFaint));
                        g.drawText(arrowStr, indent + 13, y, 10, rh, juce::Justification::centredLeft);

                        g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 9.5f,
                                  isActive ? juce::Font::bold : juce::Font::plain)));
                        g.setColour(isActive ? juce::Colour(LF::kText) : juce::Colour(LF::kTextDim));
                        g.drawText(item.file.getFileName().toUpperCase(), indent + 26, y,
                                   W - indent - 56, rh, juce::Justification::centredLeft, true);

                        // File count badge
                        const int cnt = countAudioFiles(item.file);
                        g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 7.5f, juce::Font::bold)));
                        g.setColour(juce::Colour(LF::kTextFaint));
                        g.drawText(juce::String(cnt), W - 30, y, 24, rh, juce::Justification::centredRight);
                    }
                    else
                    {
                        // Sub-folder: ▣ + expand arrow + name
                        g.setColour(isActive ? juce::Colour(LF::kText) : juce::Colour(LF::kTextFaint));
                        g.drawText(juce::String::fromUTF8("\xe2\x96\xa3"), // ▣
                                   indent, y, 12, rh, juce::Justification::centredLeft);
                        g.drawText(arrowStr, indent + 13, y, 10, rh, juce::Justification::centredLeft);

                        g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 9.0f,
                                  isActive ? juce::Font::bold : juce::Font::plain)));
                        g.setColour(isActive ? juce::Colour(LF::kText) : juce::Colour(LF::kTextDim));
                        g.drawText(item.file.getFileName(), indent + 26, y,
                                   W - indent - 42, rh, juce::Justification::centredLeft, true);
                    }
                }
                else
                {
                    // ---- SampleRow ----
                    const juce::Colour col = fileColor(item.file.getFileName());

                    // ♪ icon in fileColor
                    {
                        g.setFont(juce::Font(juce::FontOptions("VT323", 14.0f, juce::Font::plain)));
                        g.setColour(col.withAlpha(isActive ? 1.0f : 0.7f));
                        g.drawText(juce::String::fromUTF8("\xe2\x99\xaa"), // ♪
                                   indent, y, 14, rh, juce::Justification::centredLeft);
                    }

                    // Layout: [♪] [name+meta] [wave 52px] [gap 4] [badge 30px] [6px margin]
                    const int colName = indent + 14;
                    const int badgeW  = 30;
                    const int badgeX  = W - 6 - badgeW;
                    const int waveW   = 52;
                    const int colWave = badgeX - 4 - waveW;

                    // Name
                    {
                        g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 9.0f, juce::Font::bold)));
                        g.setColour(juce::Colour(LF::kText));
                        g.drawText(item.file.getFileName(), colName, y + 4,
                                   colWave - colName - 4, 11,
                                   juce::Justification::centredLeft, true);
                    }

                    // Meta: duration · EXT
                    {
                        g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 7.0f, juce::Font::plain)));
                        g.setColour(juce::Colour(LF::kTextFaint));
                        const juce::String ext = item.file.getFileExtension().toUpperCase()
                                                            .trimCharactersAtStart(".");
                        const juce::String meta = juce::String::fromUTF8("0:01 \xc2\xb7 ") + ext;
                        g.drawText(meta, colName, y + rh - 11, colWave - colName - 4, 9,
                                   juce::Justification::centredLeft);
                    }

                    // Mini waveform
                    {
                        const int seed = hashSeed(item.file.getFileName());
                        const juce::Rectangle<float> waveRect(
                            (float)colWave, (float)y + 4.0f, (float)waveW, (float)rh - 8.0f);
                        drawMiniWave(g, waveRect, seed, col.withAlpha(0.8f));
                    }

                    // Extension badge (hash-based color, 8-palette)
                    {
                        const juce::String ext = item.file.getFileExtension().toUpperCase()
                                                            .trimCharactersAtStart(".");
                        const juce::Rectangle<float> badgeR(
                            (float)badgeX, (float)y + 5.0f, (float)badgeW, 16.0f);
                        g.setColour(col.withAlpha(0.18f));
                        g.fillRoundedRectangle(badgeR, 3.0f);
                        g.setColour(col.withAlpha(0.65f));
                        g.drawRoundedRectangle(badgeR.reduced(0.5f), 3.0f, 0.5f);
                        g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 6.5f, juce::Font::bold)));
                        g.setColour(col);
                        g.drawText(ext, badgeR.toNearestInt(), juce::Justification::centred);
                    }
                }

                // Separator
                g.setColour(juce::Colour(LF::kPanelRim).withAlpha(0.35f));
                g.drawLine(2.0f, (float)(y + rh - 1), (float)W - 2.0f, (float)(y + rh - 1), 0.5f);

                y += rh;
            }
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            const int h = rowAtY(e.getPosition().y);
            if (h != hoveredIdx) { hoveredIdx = h; repaint(); }
        }
        void mouseExit(const juce::MouseEvent&) override { hoveredIdx = -1; repaint(); }

        void mouseDown(const juce::MouseEvent& e) override
        {
            mouseDownIdx = rowAtY(e.getPosition().y);
            mouseDownPos = e.getPosition();
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            const int idx = rowAtY(e.getPosition().y);
            if (idx < 0 || idx != mouseDownIdx || idx >= (int)owner.items.size()) return;

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
            else
            {
                owner.activeItemIdx_      = idx;
                owner.currentPreviewName_ = item.file.getFileName();
                owner.currentPreviewFile_ = item.file;
                owner.previewSeed_        = hashSeed(item.file.getFileName());
                owner.repaint();

                if (owner.onPreviewFile)
                    owner.onPreviewFile(item.file);
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (mouseDownIdx < 0 || mouseDownIdx >= (int)owner.items.size()) return;
            if (e.getPosition().getDistanceFrom(mouseDownPos) < 8) return;

            const auto& item = owner.items[(size_t)mouseDownIdx];
            if (item.type != Item::Type::AudioFile) return;

            if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
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

    } listContent_ { *this };
    friend struct ListContent;

    // ---- Bookmark persistence ----
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
        if (auto* s = getSettings())
        {
            const int n = s->getIntValue("sampleBrowserCount", 0);
            for (int i = 0; i < n; ++i)
            {
                juce::File f(s->getValue("sampleBrowserPath" + juce::String(i)));
                if (f.isDirectory()) bookmarks.add(f);
            }
        }
    }

    void saveBookmarks()
    {
        auto* s = getSettings();
        if (!s) return;
        s->setValue("sampleBrowserCount", bookmarks.size());
        for (int i = 0; i < bookmarks.size(); ++i)
            s->setValue("sampleBrowserPath" + juce::String(i),
                        bookmarks[i].getFullPathName());
        s->save();
    }

    void chooseAndAddBookmark()
    {
        fileChooser_ = std::make_shared<juce::FileChooser>(
            "Select Sample Folder",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        fileChooser_->launchAsync(
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

    static const juce::StringArray& audioExts()
    {
        static const juce::StringArray exts {
            ".wav", ".aif", ".aiff", ".mp3", ".flac", ".ogg"
        };
        return exts;
    }

    static int countAudioFiles(const juce::File& dir)
    {
        juce::Array<juce::File> files;
        dir.findChildFiles(files, juce::File::findFiles, false, "*");
        int n = 0;
        for (auto& f : files)
            if (audioExts().contains(f.getFileExtension().toLowerCase())) ++n;
        return n;
    }

    void addFolderContents(const juce::File& folder, int indent, const juce::String& filter)
    {
        juce::Array<juce::File> subDirs;
        folder.findChildFiles(subDirs, juce::File::findDirectories, false, "*");
        subDirs.sort();
        for (const auto& dir : subDirs)
        {
            items.push_back({ Item::Type::Folder, dir, indent });
            if (expandedPaths.contains(dir.getFullPathName()))
                addFolderContents(dir, indent + 1, filter);
        }

        juce::Array<juce::File> files;
        folder.findChildFiles(files, juce::File::findFiles, false, "*");
        files.sort();
        for (const auto& file : files)
        {
            if (!audioExts().contains(file.getFileExtension().toLowerCase())) continue;
            if (!filter.isEmpty() && !file.getFileName().toLowerCase().contains(filter)) continue;
            items.push_back({ Item::Type::AudioFile, file, indent });
        }
    }

    void rebuildList()
    {
        const juce::String filter = searchField_.getText().toLowerCase();
        items.clear();
        activeItemIdx_ = -1;

        for (auto& bm : bookmarks)
        {
            items.push_back({ Item::Type::Folder, bm, 0 });
            if (expandedPaths.contains(bm.getFullPathName()))
                addFolderContents(bm, 1, filter);
        }

        updateListSize();
        listContent_.repaint();
        repaint();
    }

    void updateListSize()
    {
        const int w = listViewport_.getWidth() > 0 ? listViewport_.getWidth() : 220;
        int totalH = 0;
        for (auto& item : items)
            totalH += (item.type == Item::Type::Folder) ? kFolderH : kItemH;
        const int h = juce::jmax(listViewport_.getHeight(), totalH);
        listContent_.setSize(w, h);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBrowserComponent)
};
