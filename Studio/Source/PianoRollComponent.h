/*
  ==============================================================================
    PianoRollComponent.h  —  M3 Piano Roll editor
    Self-sizing content component; host inside a juce::Viewport.
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "ProjectModel.h"

class PianoRollComponent : public juce::Component
{
public:
    PianoRollComponent()
    {
        setSize(getNeededWidth(), getNeededHeight());
        setWantsKeyboardFocus(true);
    }

    // Call whenever the active pattern / channel changes
    void setPattern(Pattern* p, int ch, double bpmValue)
    {
        pattern = p;
        channel = ch;
        bpm     = bpmValue;
        setSize(getNeededWidth(), getNeededHeight());
        repaint();
    }

    void setPlayheadBeat(double beat)
    {
        if (playheadBeat != beat) { playheadBeat = beat; repaint(); }
    }

    void setSnapBeats(float s) { snapBeats = s; repaint(); }

    // Fired after any note edit so MainComponent can mark dirty
    std::function<void()> onNotesChanged;

    // Fired when a note is deleted — wire to AudioEngine::noteOffChannel
    std::function<void(int midiPitch)> onNoteDeleted;

    // Fired when the user clicks a piano key — wire to AudioEngine::previewNote
    std::function<void(int midiPitch)> onKeyPreview;

    // Fired when Space is pressed — wire to MainComponent play/stop toggle
    std::function<void()> onPlayStopToggle;

    // ---- Recording --------------------------------------------------------
    void setRecording(bool r)
    {
        recording_ = r;
        if (!r) pendingRecNotes_.fill({});  // discard any held notes
        repaint();
    }
    bool isRecording() const { return recording_; }

    // -----------------------------------------------------------------------
    int getNeededWidth() const
    {
        const double beats = pattern ? pattern->stepCount * 0.25 : 4.0;
        return keyWidth + (int)(beats * pixelsPerBeat) + 128;
    }
    int getNeededHeight() const
    {
        return headerH + (maxPitch - minPitch + 1) * noteH + velLaneSep + velLaneH;
    }

    // -----------------------------------------------------------------------
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1a1a2e));
        drawPianoKeys(g);
        drawGrid(g);
        drawHoverStep(g);
        drawCursor(g);
        drawRuler(g);
        drawNotes(g);
        drawPlayhead(g);
        drawVelocityLane(g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (pattern == nullptr) return;
        const auto pos = e.getPosition();

        // Velocity lane — click to start velocity edit
        if (pos.x >= keyWidth && pos.y >= velLaneY())
        {
            draggingVelIdx = findNoteAtBeat(beatFromX(pos.x));
            if (draggingVelIdx >= 0)
                setVelocityFromY(draggingVelIdx, pos.y);
            return;
        }

        // Piano key area — preview note
        if (pos.x < keyWidth)
        {
            const int p = pitchFromY(pos.y);
            if (p >= minPitch && p <= maxPitch && onKeyPreview)
                onKeyPreview(p);
            return;
        }

        auto& noteList = pattern->notes[channel];

        // Right-click → delete
        if (e.mods.isRightButtonDown())
        {
            for (int i = (int)noteList.size() - 1; i >= 0; --i)
            {
                if (noteHitRect(noteList[(size_t)i]).contains(pos))
                {
                    const int deletedPitch = noteList[(size_t)i].pitch;
                    noteList.erase(noteList.begin() + i);
                    if (onNoteDeleted)   onNoteDeleted(deletedPitch);
                    if (onNotesChanged) onNotesChanged();
                    repaint();
                    return;
                }
            }
            return;
        }

        // Check existing note
        draggingIdx = -1;
        for (int i = (int)noteList.size() - 1; i >= 0; --i)
        {
            const auto r = noteHitRect(noteList[(size_t)i]);
            if (r.contains(pos))
            {
                draggingIdx    = i;
                resizingNote   = (pos.x >= r.getRight() - resizeZone);
                dragStartBeat  = noteList[(size_t)i].startBeat;
                dragStartPitch = noteList[(size_t)i].pitch;
                dragStartLen   = noteList[(size_t)i].lengthBeats;
                dragStartMouseX = pos.x;
                dragStartMouseY = pos.y;
                return;
            }
        }

        // Empty space → move cursor + add new note
        const int   pitch     = pitchFromY(pos.y);
        const float beat      = snapBeat(beatFromX(pos.x));
        if (pitch < minPitch || pitch > maxPitch) return;

        // Move keyboard cursor to clicked position
        cursorBeat  = beat;
        cursorPitch = pitch;

        NoteEvent n;
        n.pitch       = pitch;
        n.startBeat   = beat;
        n.lengthBeats = snapBeats;
        n.velocity    = 0.8f;
        noteList.push_back(n);
        draggingIdx    = (int)noteList.size() - 1;
        resizingNote   = true;
        dragStartBeat  = beat;
        dragStartPitch = pitch;
        dragStartLen   = snapBeats;
        dragStartMouseX = pos.x;
        dragStartMouseY = pos.y;
        if (onNotesChanged) onNotesChanged();
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (pattern == nullptr) return;

        // Velocity lane drag
        if (draggingVelIdx >= 0)
        {
            setVelocityFromY(draggingVelIdx, e.getPosition().y);
            return;
        }

        if (draggingIdx < 0) return;
        auto& noteList = pattern->notes[channel];
        if (draggingIdx >= (int)noteList.size()) return;

        auto& note     = noteList[(size_t)draggingIdx];
        const float dx = (float)(e.getPosition().x - dragStartMouseX) / pixelsPerBeat;
        const float dy = (e.getPosition().y - dragStartMouseY) / noteH;

        if (resizingNote)
        {
            // Alt = free resize (no snap); otherwise snap to grid
            // Minimum is always 1/64 note regardless of snap setting
            const float raw = dragStartLen + dx;
            const float snapped = e.mods.isAltDown()
                                  ? raw
                                  : snapBeat(raw);
            note.lengthBeats = juce::jmax(kMinNoteLen, snapped);
        }
        else
        {
            note.startBeat = juce::jmax(0.0f, snapBeat(dragStartBeat + dx));
            note.pitch     = juce::jlimit(minPitch, maxPitch,
                                          dragStartPitch - (int)std::round(dy));
        }
        if (onNotesChanged) onNotesChanged();
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override { draggingIdx = -1; draggingVelIdx = -1; }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        bool changed = false;

        const int p = (pos.x < keyWidth) ? pitchFromY(pos.y) : -1;
        if (p != hoverPitch) { hoverPitch = p; changed = true; }

        // Track which 1/16 step column the mouse is over (grid area only)
        float newStep = -1.0f;
        if (pos.x >= keyWidth && pos.y >= headerH && pos.y < velLaneY())
        {
            const float beat = beatFromX(pos.x);
            if (beat >= 0.0f)
                newStep = std::floor(beat / 0.25f) * 0.25f;
        }
        if (newStep != hoverStepBeat) { hoverStepBeat = newStep; changed = true; }

        if (changed) repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        bool changed = false;
        if (hoverPitch   != -1)    { hoverPitch   = -1;    changed = true; }
        if (hoverStepBeat >= 0.0f) { hoverStepBeat = -1.0f; changed = true; }
        if (changed) repaint();
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const int kc = key.getKeyCode();

        // Space — play / stop (forward to main window)
        if (kc == juce::KeyPress::spaceKey)
        {
            if (onPlayStopToggle) onPlayStopToggle();
            return true;
        }

        // Octave shift
        if (kc == 'z' || kc == 'Z')
        {
            octaveOffset = juce::jmax(-3, octaveOffset - 1);
            repaint();
            return true;
        }
        if (kc == 'x' || kc == 'X')
        {
            octaveOffset = juce::jmin(3, octaveOffset + 1);
            repaint();
            return true;
        }

        // Cursor navigation — Arrow keys
        if (kc == juce::KeyPress::leftKey)
        {
            cursorBeat = juce::jmax(0.0f, cursorBeat - snapBeats);
            repaint();
            scrollToCursor();
            return true;
        }
        if (kc == juce::KeyPress::rightKey)
        {
            const float maxBeat = pattern ? (float)(pattern->stepCount) * 0.25f : 4.0f;
            cursorBeat = juce::jmin(maxBeat - snapBeats, cursorBeat + snapBeats);
            repaint();
            scrollToCursor();
            return true;
        }
        if (kc == juce::KeyPress::upKey)
        {
            cursorPitch = juce::jmin(maxPitch, cursorPitch + 1);
            repaint();
            scrollToCursor();
            return true;
        }
        if (kc == juce::KeyPress::downKey)
        {
            cursorPitch = juce::jmax(minPitch, cursorPitch - 1);
            repaint();
            scrollToCursor();
            return true;
        }

        // Enter — place note at cursor
        if (kc == juce::KeyPress::returnKey)
        {
            if (pattern == nullptr) return true;
            auto& noteList = pattern->notes[channel];
            // Only add if no note already exists at this exact beat+pitch
            bool exists = false;
            for (const auto& n : noteList)
            {
                if (n.pitch == cursorPitch &&
                    cursorBeat >= n.startBeat &&
                    cursorBeat <  n.startBeat + n.lengthBeats)
                { exists = true; break; }
            }
            if (!exists)
            {
                NoteEvent n;
                n.pitch       = cursorPitch;
                n.startBeat   = cursorBeat;
                n.lengthBeats = snapBeats;
                n.velocity    = 0.8f;
                noteList.push_back(n);
                if (onNotesChanged) onNotesChanged();
                repaint();
            }
            // Advance cursor by one snap unit
            const float maxBeat = (float)(pattern->stepCount) * 0.25f;
            cursorBeat = juce::jmin(maxBeat - snapBeats, cursorBeat + snapBeats);
            scrollToCursor();
            return true;
        }

        // Delete / Backspace — remove note at cursor
        if (kc == juce::KeyPress::deleteKey || kc == juce::KeyPress::backspaceKey)
        {
            if (pattern == nullptr) return true;
            auto& noteList = pattern->notes[channel];
            for (int i = (int)noteList.size() - 1; i >= 0; --i)
            {
                const auto& n = noteList[(size_t)i];
                if (n.pitch == cursorPitch &&
                    cursorBeat >= n.startBeat &&
                    cursorBeat <  n.startBeat + n.lengthBeats)
                {
                    const int deletedPitch = n.pitch;
                    noteList.erase(noteList.begin() + i);
                    if (onNoteDeleted)   onNoteDeleted(deletedPitch);
                    if (onNotesChanged) onNotesChanged();
                    repaint();
                    break;
                }
            }
            return true;
        }

        return false;
    }

    bool keyStateChanged(bool /*isKeyDown*/) override
    {
        // Map: key char → semitone offset from C4 (60)
        struct KeyMap { int keyCode; int semitone; };
        static const KeyMap kmap[] = {
            { 'a', 0  }, { 'w', 1  }, { 's', 2  }, { 'e', 3  },
            { 'd', 4  }, { 'f', 5  }, { 't', 6  }, { 'g', 7  },
            { 'y', 8  }, { 'h', 9  }, { 'u', 10 }, { 'j', 11 },
            { 'k', 12 }, { 'o', 13 }, { 'l', 14 }, { 'p', 15 },
        };

        bool needRepaint = false;
        for (const auto& km : kmap)
        {
            const bool down  = juce::KeyPress::isKeyCurrentlyDown(km.keyCode);
            const int  idx   = km.keyCode & 0xff;
            const int  pitch = 60 + octaveOffset * 12 + km.semitone;
            const int  pidx  = juce::jlimit(0, 127, pitch);

            if (down && !heldKeyState[idx])
            {
                heldKeyState[idx]       = true;
                keyboardHeldPitch[pidx] = true;
                needRepaint = true;
                if (pitch >= minPitch && pitch <= maxPitch && onKeyPreview)
                    onKeyPreview(pitch);

                // Recording: note-on
                if (recording_ && playheadBeat >= 0.0 && pattern != nullptr)
                {
                    auto& prn     = pendingRecNotes_[(size_t)pidx];
                    prn.active    = true;
                    prn.startBeat = snapBeat((float)playheadBeat);
                }
            }
            else if (!down && heldKeyState[idx])
            {
                heldKeyState[idx]       = false;
                keyboardHeldPitch[pidx] = false;
                needRepaint = true;

                // Recording: note-off → commit NoteEvent
                if (recording_ && pattern != nullptr)
                {
                    auto& prn = pendingRecNotes_[(size_t)pidx];
                    if (prn.active)
                    {
                        prn.active = false;
                        float len  = (float)playheadBeat - prn.startBeat;
                        // Handle pattern loop wraparound
                        if (len <= 0.0f)
                            len += (float)(pattern->stepCount) * 0.25f;
                        len = juce::jmax(kMinNoteLen, len);

                        NoteEvent n;
                        n.pitch       = pidx;
                        n.startBeat   = prn.startBeat;
                        n.lengthBeats = len;
                        n.velocity    = 0.8f;
                        pattern->notes[channel].push_back(n);
                        if (onNotesChanged) onNotesChanged();
                        needRepaint = true;
                    }
                }
            }
        }
        if (needRepaint) repaint();
        return true;
    }

    // -----------------------------------------------------------------------
private:
    Pattern* pattern      = nullptr;
    int      channel      = 0;
    double   bpm          = 70.0;
    double   playheadBeat = -1.0;
    float    snapBeats    = 0.25f;   // default: 1/16 note

    static constexpr int   keyWidth      = 60;
    static constexpr int   headerH      = 24;
    static constexpr int   noteH        = 12;
    static constexpr int   pixelsPerBeat = 80;
    static constexpr int   minPitch     = 21;   // A0
    static constexpr int   maxPitch     = 108;  // C8
    static constexpr int   resizeZone   = 8;    // px from right edge
    static constexpr int   velLaneH     = 72;   // velocity lane height
    static constexpr int   velLaneSep   = 4;    // gap between note area and vel lane
    static constexpr float kMinNoteLen  = 0.0625f; // 1/64 note minimum length

    int   octaveOffset    = 0;
    bool  heldKeyState[256] = {};
    bool  keyboardHeldPitch[128] = {};

    // Keyboard cursor for note entry (arrow key navigation)
    float cursorBeat  = 0.0f;
    int   cursorPitch = 60;

    // ---- Recording state --------------------------------------------------
    bool recording_ = false;
    struct PendingRecNote { bool active = false; float startBeat = 0.0f; };
    std::array<PendingRecNote, 128> pendingRecNotes_ {};

    int   hoverPitch      = -1;
    float hoverStepBeat   = -1.0f;   // beat position of hovered 1/16 column (-1 = none)
    int   draggingIdx     = -1;
    int   draggingVelIdx  = -1;   // index of note being velocity-edited
    bool  resizingNote    = false;
    float dragStartBeat   = 0.0f;
    int   dragStartPitch  = 0;
    float dragStartLen    = 0.0f;
    int   dragStartMouseX = 0;
    int   dragStartMouseY = 0;

    // ---- coordinate helpers -----------------------------------------------
    int   yFromPitch(int pitch)  const { return headerH + (maxPitch - pitch) * noteH; }
    int   pitchFromY(int y)      const { return maxPitch - (y - headerH) / noteH; }
    int   xFromBeat(float beat)  const { return keyWidth + (int)(beat * pixelsPerBeat); }
    float beatFromX(int x)       const { return (float)(x - keyWidth) / pixelsPerBeat; }
    float snapBeat(float b)      const { return std::round(b / snapBeats) * snapBeats; }
    int   velLaneY()             const { return headerH + (maxPitch - minPitch + 1) * noteH + velLaneSep; }
    bool  isBlackKey(int pitch)  const
    {
        const int m = pitch % 12;
        return m == 1 || m == 3 || m == 6 || m == 8 || m == 10;
    }

    // Find the topmost note whose time range contains `beat` (for vel lane hit)
    int findNoteAtBeat(float beat) const
    {
        if (pattern == nullptr) return -1;
        const auto& nl = pattern->notes[channel];
        for (int i = (int)nl.size() - 1; i >= 0; --i)
        {
            if (beat >= nl[(size_t)i].startBeat &&
                beat <  nl[(size_t)i].startBeat + nl[(size_t)i].lengthBeats)
                return i;
        }
        return -1;
    }

    // Set note velocity from a Y position in the velocity lane
    void setVelocityFromY(int idx, int mouseY)
    {
        if (pattern == nullptr || idx < 0 ||
            idx >= (int)pattern->notes[channel].size()) return;

        const float vel = 1.0f - juce::jlimit(0.0f, 1.0f,
                                   (float)(mouseY - velLaneY()) / (float)(velLaneH - 2));
        pattern->notes[channel][(size_t)idx].velocity = vel;
        if (onNotesChanged) onNotesChanged();
        repaint();
    }

    juce::Rectangle<int> noteHitRect(const NoteEvent& n) const
    {
        return { xFromBeat(n.startBeat),
                 yFromPitch(n.pitch),
                 juce::jmax(4, (int)(n.lengthBeats * pixelsPerBeat) - 1),
                 noteH - 1 };
    }

    // ---- drawing ----------------------------------------------------------
    void drawPianoKeys(juce::Graphics& g)
    {
        for (int pitch = minPitch; pitch <= maxPitch; ++pitch)
        {
            const int y = yFromPitch(pitch);
            const bool black = isBlackKey(pitch);

            const bool hovered  = (pitch == hoverPitch);
            const bool kbActive = (pitch >= 0 && pitch < 128 && keyboardHeldPitch[pitch]);
            g.setColour(kbActive        ? juce::Colour(0xffff9500)
                        : hovered       ? juce::Colour(0xff3498db)
                        : black         ? juce::Colour(0xff2a2a2a)
                                        : juce::Colour(0xff3a3a4a));
            g.fillRect(0, y, keyWidth, noteH);

            g.setColour(juce::Colour(0xff111122));
            g.drawLine(0.0f, (float)(y + noteH), (float)keyWidth, (float)(y + noteH), 0.5f);

            // Label C notes
            if (pitch % 12 == 0)
            {
                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
                g.drawText("C" + juce::String(pitch / 12 - 1),
                           2, y, keyWidth - 4, noteH, juce::Justification::centredLeft);
            }
        }
        // Right border
        g.setColour(juce::Colour(0xff0f3460));
        g.drawLine((float)keyWidth, 0.0f, (float)keyWidth, (float)getHeight(), 1.0f);
    }

    void drawGrid(juce::Graphics& g)
    {
        if (pattern == nullptr) return;
        const double beats = pattern->stepCount * 0.25;
        const int    gridBottom = velLaneY();   // vertical lines stop before vel lane

        // Horizontal pitch rows — black keys slightly darker
        for (int pitch = minPitch; pitch <= maxPitch; ++pitch)
        {
            const int y = yFromPitch(pitch);
            g.setColour(isBlackKey(pitch)
                ? juce::Colour(0xff161628)
                : juce::Colour(0xff1e1e3a));
            g.fillRect(keyWidth, y, getWidth() - keyWidth, noteH);
        }

        // 3-level vertical grid hierarchy
        for (double b = 0.0; b <= beats + 0.001; b += 0.25)
        {
            const int  x      = xFromBeat((float)b);
            const bool isBar  = std::fmod(b, 4.0) < 0.001;
            const bool isBeat = std::fmod(b, 1.0) < 0.001;

            if (isBar)
            {
                // Bar line: solid blue, 1px
                g.setColour(juce::Colour(0xff3498db).withAlpha(0.55f));
                g.drawLine((float)x, (float)headerH, (float)x, (float)gridBottom, 1.0f);
            }
            else if (isBeat)
            {
                // Beat line: white, moderate alpha, 0.7px
                g.setColour(juce::Colour(0xffffffff).withAlpha(0.15f));
                g.drawLine((float)x, (float)headerH, (float)x, (float)gridBottom, 0.7f);
            }
            else
            {
                // Sub-beat (1/16 step) line: white, very faint, 0.5px
                g.setColour(juce::Colour(0xffffffff).withAlpha(0.05f));
                g.drawLine((float)x, (float)headerH, (float)x, (float)gridBottom, 0.5f);
            }
        }
    }

    // Hover step column highlight — drawn between grid and notes
    void drawHoverStep(juce::Graphics& g)
    {
        if (hoverStepBeat < 0.0f || pattern == nullptr) return;

        const float maxBeat = (float)(pattern->stepCount) * 0.25f;
        if (hoverStepBeat > maxBeat) return;

        const int x = xFromBeat(hoverStepBeat);
        const int w = (int)(0.25f * pixelsPerBeat);   // one 1/16-step wide
        const int gridBottom = velLaneY();

        g.setColour(juce::Colour(0xffffffff).withAlpha(0.03f));
        g.fillRect(x, headerH, w, gridBottom - headerH);
    }

    void drawRuler(juce::Graphics& g)
    {
        // Background — red tint when recording, dark blue otherwise
        g.setColour(recording_ ? juce::Colour(0xff3a0a0a) : juce::Colour(0xff0f3460));
        g.fillRect(0, 0, getWidth(), headerH);

        // Hover step tick mark in the ruler
        if (hoverStepBeat >= 0.0f)
        {
            const int hx = xFromBeat(hoverStepBeat);
            const int hw = (int)(0.25f * pixelsPerBeat);
            g.setColour(juce::Colour(0xffffffff).withAlpha(0.08f));
            g.fillRect(hx, 0, hw, headerH);
        }

        // REC indicator
        if (recording_ && playheadBeat >= 0.0)
        {
            g.setColour(juce::Colour(0xffff2222));
            g.fillEllipse(4.0f, 5.0f, 8.0f, 8.0f);
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
            g.drawText("REC", 15, 0, 36, headerH, juce::Justification::centredLeft);
        }

        if (pattern == nullptr) return;
        const double beats = pattern->stepCount * 0.25;

        // Draw bar numbers (large, bright) and beat numbers (small, dim)
        for (double b = 0.0; b <= beats + 0.001; b += 1.0)   // every beat
        {
            const int  x       = xFromBeat((float)b);
            const bool isBar   = std::fmod(b, 4.0) < 0.001;
            const int  barNum  = (int)(b / 4.0) + 1;
            const int  beatNum = (int)std::fmod(b, 4.0) + 1;

            if (isBar)
            {
                // Bar number — large (12px), bright white
                g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)
                                     .withStyle("Bold")));
                g.setColour(juce::Colours::white.withAlpha(0.90f));
                g.drawText(juce::String(barNum), x + 3, 0, 32, headerH,
                           juce::Justification::centredLeft);
            }
            else
            {
                // Beat number within bar — small (9px), dim
                g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
                g.setColour(juce::Colours::white.withAlpha(0.38f));
                g.drawText("." + juce::String(beatNum), x + 2, 0, 18, headerH,
                           juce::Justification::centredLeft);
            }
        }
    }

    void drawNotes(juce::Graphics& g)
    {
        if (pattern == nullptr) return;
        const auto& noteList = pattern->notes[channel];

        for (int i = 0; i < (int)noteList.size(); ++i)
        {
            const auto& n = noteList[(size_t)i];
            const auto  r = noteHitRect(n);

            // Velocity-driven colour:
            // low  (0.0) = dark cool blue   0xff2a3a6a
            // mid  (0.5) = mid blue-violet  0xff5060c8
            // high (1.0) = bright periwinkle 0xff9898f8
            const float t    = n.velocity;
            const juce::Colour baseLow  = juce::Colour(0xff2a3a6a);
            const juce::Colour baseMid  = juce::Colour(0xff5060c8);
            const juce::Colour baseHigh = juce::Colour(0xff9898f8);
            const juce::Colour fill = (t < 0.5f)
                ? baseLow.interpolatedWith(baseMid,  t * 2.0f)
                : baseMid.interpolatedWith(baseHigh, (t - 0.5f) * 2.0f);

            const bool isVelDragging = (i == draggingVelIdx);
            g.setColour((i == draggingIdx || isVelDragging) ? fill.brighter(0.35f) : fill);
            g.fillRoundedRectangle(r.toFloat(), 2.0f);
            g.setColour(fill.darker(0.3f));
            g.drawRoundedRectangle(r.toFloat(), 2.0f, 1.0f);

            // Resize handle stripe
            g.setColour(fill.brighter(0.5f).withAlpha(0.6f));
            g.fillRect(r.getRight() - 3, r.getY() + 2, 2, r.getHeight() - 4);

            // Velocity value hint when dragging this note's velocity
            if (isVelDragging)
            {
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
                g.drawText(juce::String((int)std::round(n.velocity * 127)),
                           r.getX(), r.getY() - 11, 30, 10,
                           juce::Justification::centredLeft);
            }
        }
    }

    void drawVelocityLane(juce::Graphics& g)
    {
        if (pattern == nullptr) return;

        const int laneY = velLaneY();

        // Background + separator
        g.setColour(juce::Colour(0xff0d0d1a));
        g.fillRect(keyWidth, laneY, getWidth() - keyWidth, velLaneH);
        g.setColour(juce::Colour(0xff3a3a5a));
        g.drawLine((float)keyWidth, (float)laneY,
                   (float)getWidth(), (float)laneY, 1.0f);

        // "VELOCITY" label on the left key stub
        g.setColour(juce::Colour(0xff888892));
        g.fillRect(0, laneY, keyWidth, velLaneH);
        g.setColour(juce::Colour(0xffb0b0b8));
        g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
        g.drawText("VELOCITY", 2, laneY + 2, keyWidth - 4, 11,
                   juce::Justification::centredLeft);

        // 25 / 50 / 75 % guide lines
        for (float pct : { 0.25f, 0.5f, 0.75f })
        {
            const int guideY = laneY + (int)((1.0f - pct) * velLaneH);
            g.setColour(juce::Colour(0xff3a3a5a));
            g.drawLine((float)keyWidth, (float)guideY,
                       (float)getWidth(), (float)guideY, 0.5f);
        }

        // Velocity bars
        const auto& noteList = pattern->notes[channel];
        for (int i = 0; i < (int)noteList.size(); ++i)
        {
            const auto& n   = noteList[(size_t)i];
            const int barX  = xFromBeat(n.startBeat);
            const int barW  = juce::jmax(3, juce::jmin(
                                  (int)(n.lengthBeats * pixelsPerBeat) - 2, 14));
            const int barH  = juce::jmax(2, (int)(n.velocity * (velLaneH - 4)));
            const int barY  = laneY + (velLaneH - 4) - barH + 2;

            // Same velocity colour as the note
            const float t       = n.velocity;
            const juce::Colour baseLow  = juce::Colour(0xff2a3a6a);
            const juce::Colour baseMid  = juce::Colour(0xff5060c8);
            const juce::Colour baseHigh = juce::Colour(0xff9898f8);
            const juce::Colour col = (t < 0.5f)
                ? baseLow.interpolatedWith(baseMid,  t * 2.0f)
                : baseMid.interpolatedWith(baseHigh, (t - 0.5f) * 2.0f);

            const bool active = (i == draggingVelIdx);
            g.setColour(active ? col.brighter(0.4f) : col.withAlpha(0.85f));
            g.fillRect(barX, barY, barW, barH);
            g.setColour(active ? col.brighter(0.8f) : col.brighter(0.2f));
            g.drawRect(barX, barY, barW, barH, 1);

            // Value label when actively dragging
            if (active)
            {
                g.setColour(juce::Colours::white);
                g.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
                g.drawText(juce::String((int)std::round(n.velocity * 127)),
                           barX, barY - 12, 28, 11,
                           juce::Justification::centredLeft);
            }
        }
    }

    void drawCursor(juce::Graphics& g)
    {
        // Draw keyboard cursor as a bright outline rectangle in the grid
        const int x = xFromBeat(cursorBeat);
        const int y = yFromPitch(cursorPitch);
        const int w = juce::jmax(4, (int)(snapBeats * pixelsPerBeat));
        const int h = noteH;

        // Soft fill
        g.setColour(juce::Colour(0xffb0b0ff).withAlpha(0.18f));
        g.fillRect(x, y, w, h);

        // Bright outline
        g.setColour(juce::Colour(0xffb0b0ff).withAlpha(0.85f));
        g.drawRect(x, y, w, h, 1);

        // Small triangle marker at top-left corner
        juce::Path marker;
        marker.addTriangle((float)x, (float)y,
                           (float)(x + 5), (float)y,
                           (float)x, (float)(y + 5));
        g.fillPath(marker);
    }

    void scrollToCursor()
    {
        // Scroll the parent Viewport to keep cursor visible
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            const int cx     = xFromBeat(cursorBeat);
            const int cy     = yFromPitch(cursorPitch);
            const int margin = 40;
            const auto va    = vp->getViewArea();
            int nx = vp->getViewPositionX();
            int ny = vp->getViewPositionY();

            if (cx < nx + margin)                        nx = juce::jmax(0, cx - margin);
            else if (cx > nx + va.getWidth() - margin)   nx = cx - va.getWidth() + margin;

            if (cy < ny + margin)                        ny = juce::jmax(0, cy - margin);
            else if (cy + noteH > ny + va.getHeight() - margin)
                ny = cy + noteH - va.getHeight() + margin;

            vp->setViewPosition(nx, ny);
        }
    }

    void drawPlayhead(juce::Graphics& g)
    {
        if (playheadBeat < 0.0) return;

        // Active step background tint — highlight the 1/16-step column being played
        const float stepBeat = std::floor(playheadBeat / 0.25) * 0.25f;
        const int   sx = xFromBeat(stepBeat);
        const int   sw = (int)(0.25f * pixelsPerBeat);
        const int   gridBottom = velLaneY();
        g.setColour(juce::Colour(0xffff3333).withAlpha(0.10f));
        g.fillRect(sx, headerH, sw, gridBottom - headerH);

        // Playhead line — drawn on top of notes and step tint
        const int x = xFromBeat((float)playheadBeat);
        g.setColour(juce::Colour(0xffff3333).withAlpha(0.92f));
        g.drawLine((float)x, 0.0f, (float)x, (float)getHeight(), 2.0f);

        // Small triangle cap at the top of the playhead line
        juce::Path cap;
        cap.addTriangle((float)(x - 5), 0.0f,
                        (float)(x + 5), 0.0f,
                        (float)x,       6.0f);
        g.setColour(juce::Colour(0xffff3333));
        g.fillPath(cap);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

// ===========================================================================
// Floating window that hosts the PianoRollComponent inside a Viewport
// ===========================================================================

class PianoRollWindow : public juce::DocumentWindow
{
    // DocumentWindow only accepts a single content component, so we wrap
    // the viewport + snap selector together in a plain Component.
    struct ContentPane : public juce::Component
    {
        PianoRollComponent pianoRoll;
        juce::Viewport     viewport;
        juce::ComboBox     snapBox;
        juce::TextButton   recBtn { "REC" };

        ContentPane()
        {
            viewport.setViewedComponent(&pianoRoll, false);
            viewport.setScrollBarsShown(true, true);
            viewport.setScrollBarThickness(8);
            addAndMakeVisible(viewport);

            snapBox.addItem("1/16",  1);
            snapBox.addItem("1/8",   2);
            snapBox.addItem("1/4",   3);
            snapBox.addItem("1/2",   4);
            snapBox.addItem("1 Bar", 5);
            snapBox.setSelectedId(1, juce::dontSendNotification);
            snapBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0f3460));
            snapBox.setColour(juce::ComboBox::textColourId,       juce::Colours::white);
            snapBox.setColour(juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
            snapBox.onChange = [this]
            {
                static const float beats[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                const int idx = snapBox.getSelectedId() - 1;
                if (idx >= 0 && idx < 5)
                    pianoRoll.setSnapBeats(beats[idx]);
            };
            addAndMakeVisible(snapBox);

            // REC button — toggle recording mode
            recBtn.setClickingTogglesState(true);
            recBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2a1010));
            recBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffcc1111));
            recBtn.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xffcc6666));
            recBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
            recBtn.onClick = [this]
            {
                pianoRoll.setRecording(recBtn.getToggleState());
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(recBtn);
        }

        void resized() override
        {
            const int snapH = 26;
            recBtn .setBounds(getWidth() - 96 - 52, 0, 48, snapH);
            snapBox.setBounds(getWidth() - 96,       0, 90, snapH);
            viewport.setBounds(0, snapH, getWidth(), getHeight() - snapH);

            const int cw = juce::jmax(viewport.getWidth(),  pianoRoll.getNeededWidth());
            const int ch = juce::jmax(viewport.getHeight(), pianoRoll.getNeededHeight());
            pianoRoll.setSize(cw, ch);
        }
    };

public:
    ContentPane content;   // public: MainComponent accesses content.pianoRoll

    PianoRollWindow()
        : juce::DocumentWindow("Piano Roll",
                               juce::Colour(0xff16213e),
                               juce::DocumentWindow::allButtons)
    {
        setContentNonOwned(&content, false);
        setResizable(true, false);
        setSize(1000, 520);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollWindow)
};
