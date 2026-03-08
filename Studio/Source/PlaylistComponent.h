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
    void mouseWheelMove  (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    double getTotalBars() const;

    void setProject(Project* projectToUse);

    void setPlayheadBar(double bar)
    {
        if (playheadBar != bar) { playheadBar = bar; repaint(); }
    }

    void setSnapDivisor(int d) { snapDivisor = d; repaint(); }

    int getNeededWidth()  const { return (int)(juce::jmax(200.0, getTotalBars() + 8.0)) * barWidth; }
    int getNeededHeight() const
    {
        const int autoH = project ? (int)project->automationLanes.size() * autoLaneHeight : 0;
        return headerHeight + getTrackCount() * (trackHeight + trackGap) + autoH;
    }

    // Clip copy/paste (called from MainComponent on Cmd+C / Cmd+V)
    void copySelectedClip()
    {
        if (auto* c = findClipById(selectedClipId))
        {
            clipboardClip = *c;
            hasClipboard  = true;
        }
    }
    void pasteClipboard()
    {
        if (!hasClipboard) return;
        auto& list = clipList();
        int newId = 1;
        for (const auto& c : list) newId = juce::jmax(newId, c.id + 1);
        PlaylistClip nc  = clipboardClip;
        nc.id            = newId;
        nc.startBar      = clipboardClip.startBar + clipboardClip.lengthBars;
        list.push_back(nc);
        repaint();
        if (onClipAdded) onClipAdded(nc);
    }

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

    // Seek callback — fired when user clicks on the time ruler
    std::function<void(double bar)> onSeekToBar;

    // Zoom callback — fired when barWidth changes so the host can resize the viewport
    std::function<void()> onZoomChanged;

    // M6 — undo/redo hooks
    std::function<void(PlaylistClip)>                                           onClipAdded;
    std::function<void(PlaylistClip)>                                           onClipDeleted;
    std::function<void(int id, float oldBar, int oldTrack, float newBar, int newTrack)> onClipMoved;
    std::function<void(int id, float oldLen, float newLen)>                     onClipResized;

    // M11 — track management callbacks
    std::function<void()>        onTrackAdded;
    std::function<void(int idx)> onTrackRenamed;
    std::function<void(int idx)> onTrackDeleted;

    // Pattern copy — fired when user chooses "Detach to New Pattern" on a clip.
    // Host should duplicate the clip's patternId to a new pattern and update clip.patternId.
    std::function<void(int clipId)> onClipDetach;

private:
    Project* project = nullptr;

    std::vector<PlaylistClip> localDemoClips;

    static constexpr int headerHeight  = 24;
    static constexpr int trackHeight   = 40;
    static constexpr int trackGap      = 4;
    static constexpr int resizeHotspot = 10; // px from right edge = resize handle
    static constexpr int autoLaneHeight = 60; // M9 — height of each automation lane

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

    // Clip clipboard (copy/paste)
    PlaylistClip clipboardClip;
    bool         hasClipboard  = false;
    int          selectedClipId = -1;

    // Drawing
    void drawBackground    (juce::Graphics& g);
    void drawTimeRuler     (juce::Graphics& g);
    void drawTracks        (juce::Graphics& g);
    void drawClips         (juce::Graphics& g);
    void drawPlayhead      (juce::Graphics& g);
    void drawAutomationLanes(juce::Graphics& g);   // M9
    void addAutomationLane(const juce::String& paramId, float minVal, float maxVal,
                           const juce::String& displayName);
    void removeAutomationLane(int laneIdx);
    int  autoLanesY() const;

    // M9 — automation editing state
    int   dragLaneIdx   = -1;
    int   dragPointIdx  = -1;

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
    drawAutomationLanes(g);
    drawPlayhead(g);
}

inline void PlaylistComponent::drawBackground(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0f0f0f));
    g.setColour(juce::Colour(0xff1c1c1e));
    g.fillRect(0, 0, getWidth(), headerHeight);
}

inline void PlaylistComponent::drawTimeRuler(juce::Graphics& g)
{
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
    const int totalBars = juce::jmax(getWidth() / barWidth + 1, 32);

    // Sub-bar grid lines
    if (snapDivisor > 1)
    {
        g.setColour(juce::Colour(0xffb0b0b8).withAlpha(0.08f));
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
        g.setColour(bar % 4 == 0 ? juce::Colour(0xffb0b0b8).withAlpha(0.2f)
                                  : juce::Colour(0xff383838).withAlpha(0.8f));
        g.drawLine((float)x, 0.0f, (float)x, (float)getHeight(), 1.0f);

        g.setColour(bar % 4 == 0 ? juce::Colour(0xfff0f0f2) : juce::Colour(0xff888892));
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

        g.setColour(t % 2 == 0 ? juce::Colour(0xff1c1c1e) : juce::Colour(0xff161618));
        g.fillRect(0, trackY, getWidth(), trackHeight);

        // Track header label area — slightly elevated
        g.setColour(juce::Colour(0xff2c2c2e));
        g.fillRect(0, trackY, 80, trackHeight);
        // Right separator line
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawLine(79.5f, (float)trackY, 79.5f, (float)(trackY + trackHeight), 1.0f);

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
        juce::Colour(0xff5c7090), juce::Colour(0xff4e6888),
        juce::Colour(0xff6870a0), juce::Colour(0xff527080),
        juce::Colour(0xff486890), juce::Colour(0xff687898),
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

    // Click on time ruler → seek
    if (pos.y < headerHeight)
    {
        if (!e.mods.isRightButtonDown())
        {
            const double bar = (double)pos.x / barWidth;
            if (onSeekToBar) onSeekToBar(bar);
            setPlayheadBar(bar);
        }
        return;
    }

    PlaylistClip* clip = findClipAt(pos.x, pos.y);

    // Right-click → context or track menu
    if (e.mods.isRightButtonDown())
    {
        if (pos.x < 80)
        {
            const int t = (pos.y - headerHeight) / (trackHeight + trackGap);
            if (t >= 0 && t < getTrackCount())
                showTrackContextMenu(t);
        }
        else if (clip != nullptr)
        {
            selectedClipId = clip->id;
            showContextMenu(clip->id);
        }
        else if (pos.y < autoLanesY())
        {
            // Right-click on empty track area → paste clip or add automation lane
            const float pasteBar   = (float)pos.x / barWidth;
            const int   pasteTrack = trackIndexAt(pos.y);
            juce::PopupMenu m;
            if (hasClipboard) m.addItem(1, "Paste Clip");
            m.addSeparator();

            juce::PopupMenu autoMenu;
            autoMenu.addItem(10, "Master Volume  (0–1)");
            autoMenu.addItem(11, "BPM  (60–200)");
            autoMenu.addItem(12, "Ch 1 Volume");
            autoMenu.addItem(13, "Ch 2 Volume");
            autoMenu.addItem(14, "Ch 3 Volume");
            m.addSubMenu("Add Automation Lane", autoMenu);

            m.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
                [this, pasteBar, pasteTrack](int r)
                {
                    if (r == 1 && hasClipboard)
                    {
                        auto& list = clipList();
                        int newId = 1;
                        for (const auto& c : list) newId = juce::jmax(newId, c.id + 1);
                        PlaylistClip nc  = clipboardClip;
                        nc.id            = newId;
                        nc.startBar      = pasteBar;
                        nc.trackIndex    = pasteTrack;
                        list.push_back(nc);
                        repaint();
                        if (onClipAdded) onClipAdded(nc);
                    }
                    else if (r == 10) addAutomationLane("masterVolume", 0.0f, 1.0f, "Master Volume");
                    else if (r == 11) addAutomationLane("bpm",          60.0f, 200.0f, "BPM");
                    else if (r == 12) addAutomationLane("ch0vol",       0.0f, 1.0f, "Ch 1 Volume");
                    else if (r == 13) addAutomationLane("ch1vol",       0.0f, 1.0f, "Ch 2 Volume");
                    else if (r == 14) addAutomationLane("ch2vol",       0.0f, 1.0f, "Ch 3 Volume");
                });
        }
        return;
    }

    // M9 — automation lane interaction (below the track rows)
    if (project != nullptr && pos.y >= autoLanesY())
    {
        const int relY  = pos.y - autoLanesY();
        const int li    = relY / autoLaneHeight;
        if (li >= 0 && li < (int)project->automationLanes.size())
        {
            auto& lane = project->automationLanes[(size_t)li];

            if (e.mods.isRightButtonDown())
            {
                // Right-click → remove lane
                juce::PopupMenu m;
                m.addItem(1, "Remove Lane: " + lane.paramId);
                m.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
                    [this, li](int r) { if (r == 1) removeAutomationLane(li); });
                return;
            }

            const float beatPos  = (float)pos.x / barWidth * 4.0f;
            const int   laneRelY = relY % autoLaneHeight;
            const float value    = 1.0f - juce::jlimit(0.0f, 1.0f,
                                       (float)(laneRelY - 4) / (float)(autoLaneHeight - 8));

            // Check if clicking near an existing point
            dragLaneIdx  = li;
            dragPointIdx = -1;
            for (int pi = 0; pi < (int)lane.points.size(); ++pi)
            {
                const int px = (int)(lane.points[(size_t)pi].beat * 0.25 * barWidth);
                if (std::abs(pos.x - px) < 8)
                {
                    dragPointIdx = pi;
                    break;
                }
            }

            if (dragPointIdx < 0)
            {
                // Add new point
                AutomationPoint pt;
                pt.beat  = (double)beatPos;
                pt.value = value;
                auto it  = lane.points.begin();
                while (it != lane.points.end() && it->beat < pt.beat) ++it;
                const int insertIdx = (int)(it - lane.points.begin());
                lane.points.insert(it, pt);
                dragPointIdx = insertIdx;
            }

            repaint();
            return;
        }
    }
    dragLaneIdx = -1;

    if (clip != nullptr)
    {
        selectedClipId  = clip->id;
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
    // M9 — drag automation breakpoint
    if (dragLaneIdx >= 0 && dragPointIdx >= 0 && project != nullptr)
    {
        if (dragLaneIdx < (int)project->automationLanes.size())
        {
            auto& lane = project->automationLanes[(size_t)dragLaneIdx];
            if (dragPointIdx < (int)lane.points.size())
            {
                auto& pt       = lane.points[(size_t)dragPointIdx];
                const int laneY = autoLanesY() + dragLaneIdx * autoLaneHeight;
                const int relY  = e.getPosition().y - laneY;
                pt.beat  = juce::jmax(0.0, (double)e.getPosition().x / barWidth * 4.0);
                pt.value = 1.0f - juce::jlimit(0.0f, 1.0f,
                               (float)(relY - 4) / (float)(autoLaneHeight - 8));
                repaint();
            }
        }
        return;
    }

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
    dragLaneIdx = dragPointIdx = -1;

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
    menu.addItem(3, "Copy Clip");
    menu.addItem(4, "Detach to New Pattern");
    menu.addItem(2, "Delete");

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [this, clipId](int result)
        {
            if (result == 1)
            {
                showRenameDialog(clipId);
            }
            else if (result == 3)
            {
                if (auto* c = findClipById(clipId))
                {
                    clipboardClip  = *c;
                    hasClipboard   = true;
                    selectedClipId = clipId;
                }
            }
            else if (result == 4)
            {
                // Detach: duplicate the assigned pattern, reassign this clip to the copy
                if (onClipDetach) onClipDetach(clipId);
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

inline void PlaylistComponent::mouseWheelMove(const juce::MouseEvent& e,
                                               const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        // Ctrl/Cmd + scroll wheel → horizontal zoom
        const int step = (w.deltaY > 0.0f) ? 16 : -16;
        barWidth = juce::jlimit(20, 256, barWidth + step);
        if (onZoomChanged) onZoomChanged();
        repaint();
    }
    else
    {
        juce::Component::mouseWheelMove(e, w);
    }
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

// ---------------------------------------------------------------------------
// M9 — Automation lane helpers
// ---------------------------------------------------------------------------

inline int PlaylistComponent::autoLanesY() const
{
    return headerHeight + getTrackCount() * (trackHeight + trackGap);
}

inline void PlaylistComponent::addAutomationLane(const juce::String& paramId,
                                                  float minVal, float maxVal,
                                                  const juce::String& /*displayName*/)
{
    if (project == nullptr) return;
    AutomationLane lane;
    lane.paramId = paramId;
    lane.minVal  = minVal;
    lane.maxVal  = maxVal;
    project->automationLanes.push_back(lane);
    repaint();
}

inline void PlaylistComponent::removeAutomationLane(int laneIdx)
{
    if (project == nullptr) return;
    if (laneIdx < 0 || laneIdx >= (int)project->automationLanes.size()) return;
    project->automationLanes.erase(project->automationLanes.begin() + laneIdx);
    repaint();
}

inline void PlaylistComponent::drawAutomationLanes(juce::Graphics& g)
{
    if (project == nullptr || project->automationLanes.empty()) return;

    static const juce::Colour laneColours[] = {
        juce::Colour(0xffe67e22), juce::Colour(0xff9b59b6),
        juce::Colour(0xff1abc9c), juce::Colour(0xffe74c3c),
    };
    constexpr int numColours = (int)(sizeof(laneColours) / sizeof(laneColours[0]));
    const int totalBars = juce::jmax(getWidth() / barWidth + 1, 32);

    for (int li = 0; li < (int)project->automationLanes.size(); ++li)
    {
        const auto& lane = project->automationLanes[(size_t)li];
        const int   laneY = autoLanesY() + li * autoLaneHeight;
        const juce::Colour col = laneColours[li % numColours];

        // Background
        g.setColour(juce::Colour(0xff0d0d1a));
        g.fillRect(0, laneY, getWidth(), autoLaneHeight);

        // Top separator
        g.setColour(juce::Colour(0xff1a1a2e));
        g.drawLine(0, (float)laneY, (float)getWidth(), (float)laneY, 1.5f);

        // Label
        g.setColour(col);
        g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        g.drawText(lane.paramId, 4, laneY + 2, 120, 14, juce::Justification::centredLeft);

        // Remove hint
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
        g.drawText("[right-click to remove]", getWidth() - 150, laneY + 2,
                   146, 14, juce::Justification::centredRight);

        // Bar grid lines
        g.setColour(juce::Colour(0xff1e1e32));
        for (int bar = 0; bar <= totalBars; ++bar)
            g.drawLine((float)(bar * barWidth), (float)(laneY + 16),
                       (float)(bar * barWidth), (float)(laneY + autoLaneHeight), 0.5f);

        // Mid-value guide line
        g.setColour(col.withAlpha(0.12f));
        g.drawLine(0, (float)(laneY + autoLaneHeight / 2),
                   (float)getWidth(), (float)(laneY + autoLaneHeight / 2), 0.5f);

        // Automation curve
        if (lane.points.size() >= 2)
        {
            juce::Path curvePath;
            bool started = false;
            for (const auto& pt : lane.points)
            {
                const float px = (float)(pt.beat * 0.25 * barWidth);
                const float py = (float)(laneY + autoLaneHeight - 4)
                                 - pt.value * (float)(autoLaneHeight - 8);
                if (!started) { curvePath.startNewSubPath(px, py); started = true; }
                else          curvePath.lineTo(px, py);
            }
            g.setColour(col.withAlpha(0.7f));
            g.strokePath(curvePath, juce::PathStrokeType(1.5f));
        }

        // Breakpoints (circles)
        for (int pi = 0; pi < (int)lane.points.size(); ++pi)
        {
            const auto& pt = lane.points[(size_t)pi];
            const float px = (float)(pt.beat * 0.25 * barWidth);
            const float py = (float)(laneY + autoLaneHeight - 4)
                             - pt.value * (float)(autoLaneHeight - 8);
            const bool dragging = (dragLaneIdx == li && dragPointIdx == pi);
            g.setColour(dragging ? juce::Colours::white : col.brighter(0.4f));
            g.fillEllipse(px - 5.0f, py - 5.0f, 10.0f, 10.0f);
            g.setColour(col.darker(0.3f));
            g.drawEllipse(px - 5.0f, py - 5.0f, 10.0f, 10.0f, 1.0f);
        }
    }
}
