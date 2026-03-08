/*
  ==============================================================================

    PlaylistComponent.h
    Created: 6 Mar 2026
    Author:  홍준영

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ProjectModel.h"

class PlaylistComponent : public juce::Component
{
public:
    PlaylistComponent();
    ~PlaylistComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown       (const juce::MouseEvent& e) override;
    void mouseDrag       (const juce::MouseEvent& e) override;
    void mouseUp         (const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    double getTotalBars() const;

    void setProject(Project* projectToUse);

    void setPlayheadBar(double bar)
    {
        if (playheadBar != bar) { playheadBar = bar; repaint(); }
    }

    void setSnapDivisor(int d) { snapDivisor = d; repaint(); }

    int getNeededWidth()  const { return 200 * barWidth; }
    int getNeededHeight() const { return headerHeight + getTrackCount() * (trackHeight + trackGap); }

    // M11 — horizontal zoom (pixels per bar)
    void setBarWidth(int w) { barWidth = juce::jlimit(20, 256, w); repaint(); }
    int  getBarWidth() const { return barWidth; }
    int getTrackCount()   const
    {
        if (project != nullptr && !project->playlistTracks.empty())
            return (int)project->playlistTracks.size();
        return 8;
    }

    // M2.2 — provide the currently selected pattern ID when creating a clip
    std::function<int()> getActivePatternId;

    // M6 — undo/redo hooks
    std::function<void(PlaylistClip)>                                           onClipAdded;
    std::function<void(PlaylistClip)>                                           onClipDeleted;
    std::function<void(int id, float oldBar, int oldTrack, float newBar, int newTrack)> onClipMoved;
    std::function<void(int id, float oldLen, float newLen)>                     onClipResized;

    // M11 — track management callbacks
    std::function<void()>        onTrackAdded;
    std::function<void(int idx)> onTrackRenamed;
    std::function<void(int idx)> onTrackDeleted;

private:
    Project* project = nullptr;

    std::vector<PlaylistClip> localDemoClips;

    static constexpr int headerHeight  = 24;
    static constexpr int trackHeight   = 40;
    static constexpr int trackGap      = 4;
    static constexpr int resizeHotspot = 10; // px from right edge = resize handle

    int barWidth = 64;  // M11 zoom: pixels per bar (variable)

    // Snap
    int snapDivisor = 1;    // 1=1bar, 2=½bar, 4=¼bar, 0=free

    // Drag state
    int   draggingClipId  = -1;
    bool  resizingClip    = false;
    float dragStartBar    = 0.0f;
    int   dragStartTrack  = 0;
    float dragStartLength = 1.0f;
    int   dragStartMouseX = 0;
    int   dragStartMouseY = 0;

    double playheadBar = -1.0;

    // Drawing
    void drawBackground(juce::Graphics& g);
    void drawTimeRuler (juce::Graphics& g);
    void drawTracks    (juce::Graphics& g);
    void drawClips     (juce::Graphics& g);
    void drawPlayhead  (juce::Graphics& g);

    // Hit testing
    PlaylistClip* findClipAt(int x, int y);
    bool          isOnRightEdge(const PlaylistClip& clip, int mouseX) const;
    int           trackIndexAt(int mouseY) const;

    // Clip operations
    void showContextMenu      (int clipId);
    void showRenameDialog     (int clipId);
    void showTrackContextMenu (int trackIdx);   // M11
    void showTrackRenameDialog(int trackIdx);   // M11

    std::vector<PlaylistClip>& clipList()
    {
        return (project != nullptr) ? project->playlistClips : localDemoClips;
    }
    const std::vector<PlaylistClip>& clipList() const
    {
        return (project != nullptr) ? project->playlistClips : localDemoClips;
    }
    PlaylistClip* findClipById(int id)
    {
        for (auto& c : clipList()) if (c.id == id) return &c;
        return nullptr;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistComponent)
};

// ===========================================================================
// Inline implementations
// ===========================================================================

inline PlaylistComponent::PlaylistComponent()
{
    PlaylistClip c1; c1.id=1; c1.patternId=1; c1.name="Intro Beat";
    c1.trackIndex=0; c1.startBar=0;  c1.lengthBars=4; localDemoClips.push_back(c1);

    PlaylistClip c2; c2.id=2; c2.patternId=1; c2.name="Main Beat";
    c2.trackIndex=0; c2.startBar=4;  c2.lengthBars=8; localDemoClips.push_back(c2);

    PlaylistClip c3; c3.id=3; c3.patternId=1; c3.name="Break";
    c3.trackIndex=1; c3.startBar=8;  c3.lengthBars=4; localDemoClips.push_back(c3);

    PlaylistClip c4; c4.id=4; c4.patternId=1; c4.name="Fill";
    c4.trackIndex=2; c4.startBar=12; c4.lengthBars=2; localDemoClips.push_back(c4);
}

inline void PlaylistComponent::resized() {}

inline double PlaylistComponent::getTotalBars() const
{
    double maxBar = 0.0;
    for (const auto& c : clipList())
        maxBar = juce::jmax(maxBar, (double)(c.startBar + c.lengthBars));
    return maxBar;
}

inline void PlaylistComponent::setProject(Project* p)
{
    project = p;
    repaint();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

inline void PlaylistComponent::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawTimeRuler(g);
    drawTracks(g);
    drawClips(g);
    drawPlayhead(g);
}

inline void PlaylistComponent::drawBackground(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff11111f));
    g.setColour(juce::Colour(0xff0f3460));
    g.fillRect(0, 0, getWidth(), headerHeight);
}

inline void PlaylistComponent::drawTimeRuler(juce::Graphics& g)
{
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    const int totalBars = juce::jmax(getWidth() / barWidth + 1, 32);

    // Sub-bar grid lines
    if (snapDivisor > 1)
    {
        g.setColour(juce::Colour(0xff3498db).withAlpha(0.15f));
        for (int bar = 0; bar < totalBars; ++bar)
            for (int sub = 1; sub < snapDivisor; ++sub)
            {
                const float x = (bar + (float)sub / snapDivisor) * barWidth;
                g.drawLine(x, (float)headerHeight, x, (float)getHeight(), 0.5f);
            }
    }

    for (int bar = 0; bar < totalBars; ++bar)
    {
        const int x = bar * barWidth;
        g.setColour(bar % 4 == 0 ? juce::Colour(0xff3498db).withAlpha(0.4f)
                                  : juce::Colour(0xff2c3e50).withAlpha(0.6f));
        g.drawLine((float)x, 0.0f, (float)x, (float)getHeight(), 1.0f);

        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawText(juce::String(bar + 1), x + 2, 0, barWidth - 4, headerHeight,
                   juce::Justification::centredLeft);
    }
}

inline void PlaylistComponent::drawTracks(juce::Graphics& g)
{
    const int count = getTrackCount();
    for (int t = 0; t < count; ++t)
    {
        const int trackY = headerHeight + t * (trackHeight + trackGap);

        g.setColour(t % 2 == 0 ? juce::Colour(0xff1b1b30) : juce::Colour(0xff151528));
        g.fillRect(0, trackY, getWidth(), trackHeight);

        // Track header label area
        g.setColour(juce::Colour(0xff0f3460));
        g.fillRect(0, trackY, 80, trackHeight);

        const juce::String name = (project != nullptr && t < (int)project->playlistTracks.size())
                                  ? project->playlistTracks[(size_t)t].name
                                  : ("Track " + juce::String(t + 1));

        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        g.drawText(name, 6, trackY, 72, trackHeight, juce::Justification::centredLeft);

        // Subtle right-click hint (three dots)
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        for (int d = 0; d < 3; ++d)
            g.fillEllipse(66.0f + d * 4.0f, (float)(trackY + trackHeight / 2 - 1), 2.0f, 2.0f);
    }
}

inline void PlaylistComponent::drawClips(juce::Graphics& g)
{
    // Colour palette: cycle per patternId so different patterns look distinct
    static const juce::Colour palette[] = {
        juce::Colour(0xff27ae60), juce::Colour(0xff2980b9),
        juce::Colour(0xff8e44ad), juce::Colour(0xffd35400),
        juce::Colour(0xff16a085), juce::Colour(0xffc0392b),
    };
    constexpr int paletteSize = (int)(sizeof(palette) / sizeof(palette[0]));

    const int tc = getTrackCount();
    for (const auto& clip : clipList())
    {
        if (clip.trackIndex < 0 || clip.trackIndex >= tc) continue;

        const int x  = (int)(clip.startBar  * barWidth);
        const int w  = (int)(clip.lengthBars * barWidth) - 2;
        const int ty = headerHeight + clip.trackIndex * (trackHeight + trackGap) + 3;
        const int h  = trackHeight - 6;

        const juce::Colour fill   = palette[(clip.patternId - 1) % paletteSize];
        const juce::Colour border = fill.brighter(0.4f);

        g.setColour(fill.withAlpha(clip.id == draggingClipId ? 0.6f : 0.85f));
        g.fillRoundedRectangle((float)x + 1, (float)ty, (float)w, (float)h, 4.0f);

        g.setColour(border);
        g.drawRoundedRectangle((float)x + 1, (float)ty, (float)w, (float)h, 4.0f, 1.3f);

        // Resize handle stripe
        g.setColour(border.withAlpha(0.5f));
        g.fillRect(x + w - 4, ty + 2, 4, h - 4);

        // M11.5 — mini step-grid preview (first 3 channels, bottom half of clip)
        if (project != nullptr && w > 20)
        {
            const Pattern* pat = nullptr;
            for (const auto& p : project->patterns)
                if (p.id == clip.patternId) { pat = &p; break; }

            if (pat != nullptr && pat->stepCount > 0)
            {
                const int   previewX = x + 4;
                const int   previewW = w - 8;
                const int   previewY = ty + h - 13;
                const float stepW    = (float)previewW / (float)pat->stepCount;

                for (int ch = 0; ch < juce::jmin(3, Pattern::kMaxChannels); ++ch)
                {
                    const int rowY = previewY + ch * 4;
                    for (int s = 0; s < pat->stepCount; ++s)
                    {
                        if (pat->steps[ch][s])
                        {
                            g.setColour(fill.brighter(0.8f).withAlpha(0.85f));
                            g.fillRect(previewX + (int)(s * stepW), rowY,
                                       juce::jmax(1, (int)stepW - 1), 3);
                        }
                    }
                }
            }
        }

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        g.drawText(clip.name, x + 6, ty, w - 14, h - 14, juce::Justification::centredLeft);
    }
}

inline void PlaylistComponent::drawPlayhead(juce::Graphics& g)
{
    if (playheadBar < 0.0) return;
    const int x = (int)(playheadBar * barWidth);
    g.setColour(juce::Colour(0xffff3333));
    g.drawLine((float)x, 0.0f, (float)x, (float)getHeight(), 2.0f);
    juce::Path tri;
    tri.addTriangle((float)x - 6, 0.0f, (float)x + 6, 0.0f, (float)x, 12.0f);
    g.fillPath(tri);
}

// ---------------------------------------------------------------------------
// Hit testing helpers
// ---------------------------------------------------------------------------

inline PlaylistClip* PlaylistComponent::findClipAt(int x, int y)
{
    const int tc = getTrackCount();
    for (auto& clip : clipList())
    {
        if (clip.trackIndex < 0 || clip.trackIndex >= tc) continue;
        const int cx = (int)(clip.startBar   * barWidth);
        const int cw = (int)(clip.lengthBars * barWidth) - 2;
        const int cy = headerHeight + clip.trackIndex * (trackHeight + trackGap) + 3;
        const int ch = trackHeight - 6;
        if (juce::Rectangle<int>(cx, cy, cw, ch).contains(x, y))
            return &clip;
    }
    return nullptr;
}

inline bool PlaylistComponent::isOnRightEdge(const PlaylistClip& clip, int mouseX) const
{
    const int rightEdge = (int)((clip.startBar + clip.lengthBars) * barWidth);
    return mouseX >= (rightEdge - resizeHotspot) && mouseX <= (rightEdge + 2);
}

inline int PlaylistComponent::trackIndexAt(int mouseY) const
{
    const int relY = mouseY - headerHeight;
    if (relY < 0) return -1;
    const int t = relY / (trackHeight + trackGap);
    return juce::jlimit(0, getTrackCount() - 1, t);
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

inline void PlaylistComponent::mouseDown(const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (pos.y < headerHeight) return;

    PlaylistClip* clip = findClipAt(pos.x, pos.y);

    // Right-click → context or track menu
    if (e.mods.isRightButtonDown())
    {
        if (pos.x < 80 && pos.y >= headerHeight)
        {
            const int t = (pos.y - headerHeight) / (trackHeight + trackGap);
            if (t >= 0 && t < getTrackCount())
                showTrackContextMenu(t);
        }
        else if (clip != nullptr)
        {
            showContextMenu(clip->id);
        }
        return;
    }

    if (clip != nullptr)
    {
        draggingClipId  = clip->id;
        dragStartBar    = clip->startBar;
        dragStartTrack  = clip->trackIndex;
        dragStartLength = clip->lengthBars;
        dragStartMouseX = pos.x;
        dragStartMouseY = pos.y;
        resizingClip    = isOnRightEdge(*clip, pos.x);
    }
    else
    {
        draggingClipId = -1;
    }
}

inline void PlaylistComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingClipId < 0) return;

    const auto  pos    = e.getPosition();
    const float deltaX = (float)(pos.x - dragStartMouseX);

    PlaylistClip* clip = findClipById(draggingClipId);
    if (clip == nullptr) return;

    // Snap helper: round to nearest snap unit (0 = free)
    auto snap = [this](float bars) -> float
    {
        if (snapDivisor == 0) return bars;
        const float unit = 1.0f / (float)snapDivisor;
        return unit * std::round(bars / unit);
    };

    const float minLength = (snapDivisor == 0) ? 0.01f : 1.0f / (float)snapDivisor;

    if (resizingClip)
    {
        clip->lengthBars = juce::jmax(minLength,
                                      snap(dragStartLength + deltaX / (float)barWidth));
    }
    else
    {
        clip->startBar = juce::jmax(0.0f,
                                    snap(dragStartBar + deltaX / (float)barWidth));

        const int deltaY     = pos.y - dragStartMouseY;
        const int deltaTrack = (int)std::round((double)deltaY / (double)(trackHeight + trackGap));
        clip->trackIndex = juce::jlimit(0, getTrackCount() - 1, dragStartTrack + deltaTrack);
    }

    repaint();
}

inline void PlaylistComponent::mouseUp(const juce::MouseEvent&)
{
    if (draggingClipId >= 0)
    {
        if (auto* clip = findClipById(draggingClipId))
        {
            if (resizingClip)
            {
                if (clip->lengthBars != dragStartLength && onClipResized)
                    onClipResized(draggingClipId, dragStartLength, clip->lengthBars);
            }
            else
            {
                if ((clip->startBar != dragStartBar || clip->trackIndex != dragStartTrack) && onClipMoved)
                    onClipMoved(draggingClipId, dragStartBar, dragStartTrack, clip->startBar, clip->trackIndex);
            }
        }
    }
    draggingClipId = -1;
}

inline void PlaylistComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (pos.y < headerHeight) return;

    PlaylistClip* existing = findClipAt(pos.x, pos.y);
    if (existing != nullptr)
    {
        // Double-click on existing clip → rename
        showRenameDialog(existing->id);
        return;
    }

    // Double-click on empty space → create new clip
    const int track = trackIndexAt(pos.y);
    if (track < 0 || track >= getTrackCount()) return;

    const float rawBar  = (float)pos.x / barWidth;
    const float snapUnit = (snapDivisor == 0) ? 0.0f : 1.0f / (float)snapDivisor;
    const float bar = (snapDivisor == 0)
                        ? juce::jmax(0.0f, rawBar)
                        : snapUnit * std::round(rawBar / snapUnit);

    auto& list = clipList();
    int newId = 1;
    for (const auto& c : list) newId = juce::jmax(newId, c.id + 1);

    PlaylistClip clip;
    clip.id         = newId;
    clip.patternId  = (getActivePatternId ? getActivePatternId() : 1);
    clip.trackIndex = track;
    clip.startBar   = bar;
    clip.lengthBars = 4.0f;
    clip.name       = "Clip " + juce::String(newId);
    list.push_back(clip);
    repaint();
    if (onClipAdded) onClipAdded(clip);
}

// ---------------------------------------------------------------------------
// Clip operations
// ---------------------------------------------------------------------------

inline void PlaylistComponent::showContextMenu(int clipId)
{
    PlaylistClip* clip = findClipById(clipId);
    if (clip == nullptr) return;

    juce::PopupMenu menu;
    menu.addItem(1, "Rename");

    // Assign Pattern submenu — lists all patterns in the project
    if (project != nullptr && !project->patterns.empty())
    {
        juce::PopupMenu patMenu;
        for (const auto& pat : project->patterns)
            patMenu.addItem(100 + pat.id, pat.name, true, clip->patternId == pat.id);
        menu.addSubMenu("Assign Pattern", patMenu);
    }

    menu.addSeparator();
    menu.addItem(2, "Delete");

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [this, clipId](int result)
        {
            if (result == 1)
            {
                showRenameDialog(clipId);
            }
            else if (result == 2)
            {
                auto& list = clipList();
                PlaylistClip deleted;
                bool found = false;
                for (const auto& c : list) { if (c.id == clipId) { deleted = c; found = true; break; } }
                list.erase(std::remove_if(list.begin(), list.end(),
                    [clipId](const PlaylistClip& c){ return c.id == clipId; }), list.end());
                repaint();
                if (found && onClipDeleted) onClipDeleted(deleted);
            }
            else if (result >= 100)
            {
                const int patId = result - 100;
                if (auto* c = findClipById(clipId)) c->patternId = patId;
            }
        });
}

inline void PlaylistComponent::showRenameDialog(int clipId)
{
    PlaylistClip* clip = findClipById(clipId);
    if (clip == nullptr) return;

    auto* dialog = new juce::AlertWindow("Rename Clip", "Enter new name:",
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", clip->name);
    dialog->addButton("OK",     1);
    dialog->addButton("Cancel", 0);

    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create([this, clipId, dialog](int result)
        {
            if (result == 1)
            {
                if (auto* c = findClipById(clipId))
                    c->name = dialog->getTextEditorContents("name");
                repaint();
            }
            delete dialog;
        }),
        false);
}

// ---------------------------------------------------------------------------
// M11 — Track management
// ---------------------------------------------------------------------------

inline void PlaylistComponent::showTrackContextMenu(int trackIdx)
{
    juce::PopupMenu menu;
    menu.addItem(1, "Rename Track");
    menu.addItem(2, "Add Track Below");
    menu.addSeparator();
    menu.addItem(3, "Delete Track", getTrackCount() > 1);

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [this, trackIdx](int result)
        {
            if (result == 1)
            {
                showTrackRenameDialog(trackIdx);
            }
            else if (result == 2)
            {
                if (project != nullptr)
                {
                    PlaylistTrack nt;
                    nt.name = "Track " + juce::String(project->playlistTracks.size() + 1);
                    project->playlistTracks.insert(
                        project->playlistTracks.begin() + trackIdx + 1, nt);
                    // Shift clips below the insertion point down by one track
                    for (auto& c : clipList())
                        if (c.trackIndex > trackIdx) c.trackIndex++;
                    repaint();
                    if (onTrackAdded) onTrackAdded();
                }
            }
            else if (result == 3)
            {
                if (project != nullptr && getTrackCount() > 1)
                {
                    // Remove clips on the deleted track, shift the rest up
                    auto& clips = clipList();
                    clips.erase(std::remove_if(clips.begin(), clips.end(),
                        [trackIdx](const PlaylistClip& c)
                        { return c.trackIndex == trackIdx; }), clips.end());
                    for (auto& c : clips)
                        if (c.trackIndex > trackIdx) c.trackIndex--;
                    project->playlistTracks.erase(
                        project->playlistTracks.begin() + (size_t)trackIdx);
                    repaint();
                    if (onTrackDeleted) onTrackDeleted(trackIdx);
                }
            }
        });
}

inline void PlaylistComponent::showTrackRenameDialog(int trackIdx)
{
    if (project == nullptr || trackIdx >= (int)project->playlistTracks.size()) return;

    const juce::String current = project->playlistTracks[(size_t)trackIdx].name;
    auto* dialog = new juce::AlertWindow("Rename Track", "Enter new name:",
                                          juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", current);
    dialog->addButton("OK",     1);
    dialog->addButton("Cancel", 0);

    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create([this, trackIdx, dialog](int result)
        {
            if (result == 1)
            {
                if (project != nullptr && trackIdx < (int)project->playlistTracks.size())
                {
                    const auto newName = dialog->getTextEditorContents("name").trim();
                    if (newName.isNotEmpty())
                        project->playlistTracks[(size_t)trackIdx].name = newName;
                    repaint();
                    if (onTrackRenamed) onTrackRenamed(trackIdx);
                }
            }
            delete dialog;
        }),
        false);
}
