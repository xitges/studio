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

    // Fired when the user clicks a piano key — wire to AudioEngine::previewNote
    std::function<void(int midiPitch)> onKeyPreview;

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
                    noteList.erase(noteList.begin() + i);
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

        // Empty space → add new note
        const int   pitch     = pitchFromY(pos.y);
        const float beat      = snapBeat(beatFromX(pos.x));
        if (pitch < minPitch || pitch > maxPitch) return;

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
            note.lengthBeats = juce::jmax(snapBeats, snapBeat(dragStartLen + dx));
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
        const int p = (e.getPosition().x < keyWidth) ? pitchFromY(e.getPosition().y) : -1;
        if (p != hoverPitch) { hoverPitch = p; repaint(); }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (hoverPitch != -1) { hoverPitch = -1; repaint(); }
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const int kc = key.getKeyCode();
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
                heldKeyState[idx]      = true;
                keyboardHeldPitch[pidx] = true;
                needRepaint = true;
                if (pitch >= minPitch && pitch <= maxPitch && onKeyPreview)
                    onKeyPreview(pitch);
            }
            else if (!down && heldKeyState[idx])
            {
                heldKeyState[idx]      = false;
                keyboardHeldPitch[pidx] = false;
                needRepaint = true;
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

    static constexpr int keyWidth      = 60;
    static constexpr int headerH      = 24;
    static constexpr int noteH        = 12;
    static constexpr int pixelsPerBeat = 80;
    static constexpr int minPitch     = 21;   // A0
    static constexpr int maxPitch     = 108;  // C8
    static constexpr int resizeZone   = 8;    // px from right edge
    static constexpr int velLaneH     = 72;   // velocity lane height
    static constexpr int velLaneSep   = 4;    // gap between note area and vel lane

    int   octaveOffset    = 0;
    bool  heldKeyState[256] = {};
    bool  keyboardHeldPitch[128] = {};

    int   hoverPitch      = -1;
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

        // Horizontal pitch lines
        for (int pitch = minPitch; pitch <= maxPitch; ++pitch)
        {
            const int y = yFromPitch(pitch);
            g.setColour(isBlackKey(pitch)
                ? juce::Colour(0xff161628)
                : juce::Colour(0xff1e1e3a));
            g.fillRect(keyWidth, y, getWidth() - keyWidth, noteH);
        }

        // Vertical beat lines
        for (double b = 0.0; b <= beats + 0.001; b += 0.25)
        {
            const int x = xFromBeat((float)b);
            const bool isBar  = std::fmod(b, 4.0) < 0.001;
            const bool isBeat = std::fmod(b, 1.0) < 0.001;
            g.setColour(isBar  ? juce::Colour(0xff3498db).withAlpha(0.35f) :
                        isBeat ? juce::Colour(0xffffffff).withAlpha(0.08f) :
                                 juce::Colour(0xffffffff).withAlpha(0.03f));
            g.drawLine((float)x, (float)headerH, (float)x, (float)getHeight(), isBar ? 1.0f : 0.5f);
        }
    }

    void drawRuler(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff0f3460));
        g.fillRect(0, 0, getWidth(), headerH);
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));

        if (pattern == nullptr) return;
        const double beats = pattern->stepCount * 0.25;
        for (double b = 0.0; b <= beats + 0.001; b += 1.0)
        {
            const int x = xFromBeat((float)b);
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawText(juce::String((int)b + 1) + "." + juce::String(1),
                       x + 2, 0, 40, headerH, juce::Justification::centredLeft);
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

    void drawPlayhead(juce::Graphics& g)
    {
        if (playheadBeat < 0.0) return;
        const int x = xFromBeat((float)playheadBeat);
        g.setColour(juce::Colour(0xffff3333));
        g.drawLine((float)x, 0.0f, (float)x, (float)getHeight(), 2.0f);
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
        }

        void resized() override
        {
            const int snapH = 26;
            snapBox.setBounds(getWidth() - 96, 0, 90, snapH);
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
