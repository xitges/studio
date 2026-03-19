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
    enum class ChordVoicing { Close, Open, FirstInversion, SecondInversion, Drop2 };
    enum class BasslinePattern { RootNotes, OctaveBounce, Pulse8ths, PassingTone };

    PianoRollComponent()
    {
        setSize(getNeededWidth(), getNeededHeight());
        setWantsKeyboardFocus(true);
    }

    int variationIdx = 0;   // which variation (0=A..3=D) to read/write notes from

    void setPattern(Pattern* p, int ch, double bpmValue)
    {
        pattern = p;
        channel = ch;
        bpm     = bpmValue;
        clearSelection(false);
        resetInteractionState();
        cursorPitch = MusicTheory::snapPitchToScale(cursorPitch, keySignature);
        updateSizeForZoom();
        repaint();
    }

    void updateStepCount()
    {
        updateSizeForZoom();
        repaint();
    }

    void setPlayheadBeat(double beat)
    {
        if (playheadBeat != beat) { playheadBeat = beat; repaint(); }
    }

    void setSnapBeats(float s) { snapBeats = s; repaint(); }
    void setSelectToolEnabled(bool enabled)
    {
        if (selectToolEnabled == enabled)
            return;
        selectToolEnabled = enabled;
        resetInteractionState();
        if (onToolModeChanged) onToolModeChanged(selectToolEnabled);
        repaint();
    }
    bool isSelectToolEnabled() const { return selectToolEnabled; }
    void setKeySignature(const KeySignature& newKey)
    {
        keySignature = newKey;
        cursorPitch = MusicTheory::snapPitchToScale(cursorPitch, keySignature);
        repaint();
    }
    KeySignature getKeySignature() const { return keySignature; }
    int getKeyboardWidth() const { return keyWidth; }

    static constexpr int kKeyboardSpan = 16;   // # of keys: a,w,s,e,d,f,t,g,y,h,u,j,k,o,l,p
    static constexpr int kRangeBarW    = 6;    // px width of the draggable range strip
    Pattern* getPatternModel() const { return pattern; }
    int getChannelIndex() const { return channel; }

    void clearNotes()
    {
        if (pattern == nullptr) return;
        pattern->variations[variationIdx].notes[channel].clear();
        clearSelection(false);
        if (onNotesChanged) onNotesChanged();
        repaint();
    }

    bool fillWithChordProgression(const MusicTheory::ChordProgressionPreset& preset,
                                  ChordVoicing voicing,
                                  float strumStepBeats)
    {
        if (pattern == nullptr || preset.steps.empty() || preset.scale != keySignature.scale)
            return false;

        auto& noteList = pattern->variations[variationIdx].notes[channel];
        noteList.clear();
        selectedNoteIndices.clearQuick();
        sessionStartIdx = 0;
        sessionEndIdx = -1;

        const float totalBeats = pattern->stepCount * 0.25f;
        if (totalBeats <= 0.0f)
            return false;

        for (int chordIndex = 0; chordIndex < (int)preset.steps.size(); ++chordIndex)
        {
            const float startBeat = totalBeats * (float)chordIndex / (float)preset.steps.size();
            const float endBeat = totalBeats * (float)(chordIndex + 1) / (float)preset.steps.size();
            const auto pitches = buildChordPitches(preset.steps[(size_t)chordIndex], voicing);

            for (int noteIndex = 0; noteIndex < (int)pitches.size(); ++noteIndex)
            {
                const float noteOffset = juce::jmax(0.0f, strumStepBeats) * (float)noteIndex;
                const float noteStart = juce::jmin(startBeat + noteOffset, endBeat - kMinNoteLen);
                const float noteLength = juce::jmax(kMinNoteLen, endBeat - noteStart);
                const float velocity = juce::jlimit(0.5f, 1.0f, 0.84f - 0.03f * (float)noteIndex);
                selectedNoteIndices.add(appendNote(pitches[(size_t)noteIndex], noteStart, noteLength, velocity));
            }
        }

        notifySelectionChanged();
        if (onNotesChanged) onNotesChanged();
        repaint();
        return true;
    }

    bool generateBasslineToChannel(int targetChannel, BasslinePattern basslinePattern)
    {
        if (pattern == nullptr || targetChannel < 0 || targetChannel >= Pattern::kMaxChannels || targetChannel == channel)
            return false;

        const auto regions = extractChordRegions();
        if (regions.empty())
            return false;

        auto& targetNotes = pattern->variations[variationIdx].notes[targetChannel];
        targetNotes.clear();

        for (int regionIndex = 0; regionIndex < (int)regions.size(); ++regionIndex)
        {
            const auto& region = regions[(size_t)regionIndex];
            const int nextRoot = regionIndex + 1 < (int)regions.size()
                ? regions[(size_t)regionIndex + 1].rootPitch
                : region.rootPitch;
            appendBassRegion(targetNotes, region.startBeat, region.endBeat,
                             region.rootPitch, nextRoot, basslinePattern);
        }

        if (onNotesChanged) onNotesChanged();
        repaint();
        return true;
    }

    //Quantize
    void setQuantize(bool enabled)    { quantizeEnabled = enabled; }
    void setQuantizeGrid(float grid)  { quantizeGrid    = grid;    }
    bool isQuantizeEnabled() const    { return quantizeEnabled;    }

    void setZoom(int newPixelsPerBeat)
    {
        const int clamped = juce::jlimit(40, 400, newPixelsPerBeat);
        if (clamped == pixelsPerBeat) return;
        pixelsPerBeat = clamped;
        updateSizeForZoom();
        repaint();
    }
    int getZoom() const { return pixelsPerBeat; }

    // Fired after any note edit so MainComponent can mark dirty
    std::function<void()> onNotesChanged;

    // Fired when a note is deleted — wire to AudioEngine::noteOffChannel
    std::function<void(int midiPitch)> onNoteDeleted;

    // Fired when the user clicks a piano key — wire to AudioEngine::previewNote
    std::function<void(int midiPitch)> onKeyPreview;

    // Fired when Space is pressed — wire to MainComponent play/stop toggle
    std::function<void()> onPlayStopToggle;

    std::function<void()> onSelectionChanged;
    std::function<void(bool selectMode)> onToolModeChanged;

    // Fired when keyboard range changes (Z/X keys or drag) — wire to ContentPane label
    std::function<void(int basePitch)> onKeyboardRangeChanged;

    int  getKeyboardBasePitch() const { return keyboardBasePitch; }

    // Returns [topY, bottomY] of the range bar strip in this component's coordinates
    std::pair<int,int> getRangeBarContentY() const
    {
        const int topPitch = keyboardBasePitch + kKeyboardSpan - 1;
        const int topY     = headerH + (maxPitch - topPitch) * noteH;
        const int botY     = headerH + (maxPitch - keyboardBasePitch) * noteH + noteH;
        return { topY, botY };
    }

    // Public interface for KeyboardOverlay drag
    void startRangeDrag(int absoluteY)
    {
        rangeBarDragging    = true;
        rangeBarDragStartY  = absoluteY;
        rangeBarDragStartBase = keyboardBasePitch;
        repaint();
    }
    void updateRangeDrag(int absoluteY)
    {
        if (!rangeBarDragging) return;
        const int deltaSemitones = -(absoluteY - rangeBarDragStartY) / noteH;
        keyboardBasePitch = juce::jlimit(minPitch, maxPitch - (kKeyboardSpan - 1),
                                         rangeBarDragStartBase + deltaSemitones);
        if (onKeyboardRangeChanged) onKeyboardRangeChanged(keyboardBasePitch);
        repaint();
    }
    void stopRangeDrag()
    {
        rangeBarDragging = false;
        repaint();
    }

    // ---- Smart Record System -----------------------------------------------
    enum class RecState { Idle, Armed, Recording };

    // Fired when the Armed→Recording trigger fires (wire to transport start)
    std::function<void()> onStartTransport;

    // Fired whenever RecState changes (wire to ContentPane UI update)
    std::function<void()> onRecordingStateChanged;

    // Wire to audioEngine.isPlaying() for punch-in detection
    std::function<bool()> isPlayingCallback;

    void setRecording(bool r)
    {
        if (r)
        {
            // Punch-in: if transport already running, bypass trigger → record immediately
            const bool engineRunning = isPlayingCallback && isPlayingCallback();
            if (!engineRunning && triggerEnabled)
                enterArmed();
            else
                startRecording();
        }
        else
        {
            stopRecording();
        }
    }
    bool     isRecording()  const { return recording_; }
    RecState getRecState()  const { return currentRecState; }

    void setTriggerEnabled(bool t) { triggerEnabled = t; }
    bool isTriggerEnabled()  const { return triggerEnabled; }

    // Blink state driven by ContentPane timer during Armed state
    void setArmedBlink(bool b) { armedBlink = b; repaint(); }

    // ---- Session nudge / align (operates on last recorded block) -----------
    void nudgeSession(float deltaBeats)
    {
        if (pattern == nullptr) return;
        auto& nl = pattern->variations[variationIdx].notes[channel];
        const int endIdx = juce::jmin(sessionEndIdx, (int)nl.size() - 1);
        if (sessionStartIdx > endIdx) return;

        const float patLen = pattern->stepCount * 0.25f;
        for (int i = sessionStartIdx; i <= endIdx; ++i)
        {
            nl[(size_t)i].startBeat += deltaBeats;
            // Wrap within pattern range [0, patLen)
            while (nl[(size_t)i].startBeat < 0.0f)  nl[(size_t)i].startBeat += patLen;
            while (nl[(size_t)i].startBeat >= patLen) nl[(size_t)i].startBeat -= patLen;
        }
        if (onNotesChanged) onNotesChanged();
        repaint();
    }

    void alignSession(float gridBeats)
    {
        if (pattern == nullptr) return;
        auto& nl = pattern->variations[variationIdx].notes[channel];
        const int endIdx = juce::jmin(sessionEndIdx, (int)nl.size() - 1);
        if (sessionStartIdx > endIdx) return;

        const float firstBeat = nl[(size_t)sessionStartIdx].startBeat;
        const float aligned   = std::round(firstBeat / gridBeats) * gridBeats;
        nudgeSession(aligned - firstBeat);
    }

    void nudgeSessionPitch(int deltaSemitones)
    {
        if (pattern == nullptr) return;
        auto& nl = pattern->variations[variationIdx].notes[channel];
        const int endIdx = juce::jmin(sessionEndIdx, (int)nl.size() - 1);
        if (sessionStartIdx > endIdx) return;

        for (int i = sessionStartIdx; i <= endIdx; ++i)
            nl[(size_t)i].pitch = juce::jlimit(minPitch, maxPitch,
                                               nl[(size_t)i].pitch + deltaSemitones);

        if (onNotesChanged) onNotesChanged();
        repaint();
    }

    void nudgeActiveNotes(float deltaBeats)
    {
        if (hasSelectedNotes())
            nudgeSelectedNotes(deltaBeats);
        else
            nudgeSession(deltaBeats);
    }

    void nudgeActiveNotesPitch(int deltaSemitones)
    {
        if (hasSelectedNotes())
            nudgeSelectedNotesPitch(deltaSemitones);
        else
            nudgeSessionPitch(deltaSemitones);
    }

    bool hasSession() const
    {
        if (pattern == nullptr) return false;
        const int endIdx = juce::jmin(sessionEndIdx, (int)pattern->variations[variationIdx].notes[channel].size() - 1);
        return sessionStartIdx <= endIdx;
    }

    bool hasSelectedNotes() const { return selectedNoteIndices.size() > 0; }
    bool hasMovableNoteTarget() const { return hasSelectedNotes() || hasSession(); }

    void selectAllNotes()
    {
        if (pattern == nullptr) return;
        selectedNoteIndices.clearQuick();
        const auto& noteList = pattern->variations[variationIdx].notes[channel];
        for (int i = 0; i < (int)noteList.size(); ++i)
            selectedNoteIndices.add(i);
        notifySelectionChanged();
        repaint();
    }

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
        // Piano keys are drawn by KeyboardOverlay (fixed panel) — skip here
        drawGrid(g);
        drawHoverStep(g);
        drawCursor(g);
        drawRuler(g);
        drawNotes(g);
        drawMarqueeSelection(g);
        drawPlayhead(g);
        drawVelocityLane(g);
        drawArmedOverlay(g);
    }

    void paintKeyboardOverlay(juce::Graphics& g, int viewY, int overlayHeight)
    {
        g.fillAll(juce::Colour(0xff1a1a2e));
        g.setColour(currentRecState == RecState::Armed
                        ? juce::Colour(0xff2e1800)
                        : recording_
                            ? juce::Colour(0xff3a0a0a)
                            : juce::Colour(0xff0f3460));
        g.fillRect(0, 0, keyWidth, headerH);

        g.saveState();
        g.reduceClipRegion(0, headerH, keyWidth, overlayHeight - headerH);
        g.addTransform(juce::AffineTransform::translation(0.0f, (float)-viewY));
        drawPianoKeys(g);
        drawVelocityKeyStub(g);
        g.restoreState();

        g.setColour(juce::Colour(0xff0f3460));
        g.drawLine((float)keyWidth, 0.0f, (float)keyWidth, (float)overlayHeight, 1.0f);
    }

    void previewPitchAtVisibleY(int visibleY, int viewY)
    {
        const int pitch = pitchFromY(visibleY + viewY);
        if (pitch >= minPitch && pitch <= maxPitch && onKeyPreview)
            onKeyPreview(pitch);
    }

    void setKeyboardHoverVisibleY(int visibleY, int viewY)
    {
        const int pitch = pitchFromY(visibleY + viewY);
        const int newHover = (pitch >= minPitch && pitch <= maxPitch) ? pitch : -1;
        if (hoverPitch != newHover)
        {
            hoverPitch = newHover;
            repaint();
        }
    }

    void clearKeyboardHover()
    {
        if (hoverPitch != -1)
        {
            hoverPitch = -1;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (pattern == nullptr) return;
        const auto pos = e.getPosition();
        resetInteractionState();

        // Velocity lane — click to start velocity edit
        if (pos.x >= keyWidth && pos.y >= velLaneY())
        {
            draggingVelIdx = findNoteAtBeat(beatFromX(pos.x));
            if (draggingVelIdx >= 0)
                setVelocityFromY(draggingVelIdx, pos.y);
            return;
        }

        // Piano key area
        if (pos.x < keyWidth)
        {
            // Left strip → start range drag
            if (pos.x < kRangeBarW)
            {
                startRangeDrag(pos.y);
                return;
            }
            // Rest of key → preview note
            const int p = pitchFromY(pos.y);
            if (p >= minPitch && p <= maxPitch && onKeyPreview)
                onKeyPreview(p);
            return;
        }

        auto& noteList = pattern->variations[variationIdx].notes[channel];

        // Right-click → delete
        if (e.mods.isRightButtonDown())
        {
            for (int i = (int)noteList.size() - 1; i >= 0; --i)
            {
                if (noteHitRect(noteList[(size_t)i]).contains(pos))
                {
                    const int deletedPitch = noteList[(size_t)i].pitch;
                    noteList.erase(noteList.begin() + i);
                    removeSelectionIndex(i);
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
                const bool additiveSelect = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
                if (additiveSelect)
                {
                    toggleNoteSelection(i);
                    return;
                }

                if (! isNoteSelected(i))
                    selectSingleNote(i);

                draggingIdx    = i;
                resizingNote   = (pos.x >= r.getRight() - resizeZone);
                dragStartBeat  = noteList[(size_t)i].startBeat;
                dragStartPitch = noteList[(size_t)i].pitch;
                dragStartLen   = noteList[(size_t)i].lengthBeats;
                // Anchor resize from the actual note right edge, not the raw click position.
                // Using pos.x introduces an offset that misaligns the snap grid.
                dragStartMouseX = resizingNote ? xFromBeat(dragStartBeat + dragStartLen) : pos.x;
                dragStartMouseY = pos.y;
                captureSelectedNoteDragState();
                return;
            }
        }

        // Empty space → click/add in draw mode, click/select in select mode
        const int   pitch     = pitchFromY(pos.y);
        const float beat      = snapBeat(beatFromX(pos.x));
        if (pitch < minPitch || pitch > maxPitch) return;

        cursorBeat  = beat;
        cursorPitch = pitch;
        pendingEmptyGesture = true;
        pendingEmptyGestureAdditive = e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
        pendingEmptyPitch = pitch;
        pendingEmptyBeat = beat;
        pendingEmptyStartPos = pos;
        marqueeBaseSelection = selectedNoteIndices;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (pattern == nullptr) return;

        // Range bar drag
        if (rangeBarDragging)
        {
            updateRangeDrag(e.getPosition().y);
            return;
        }

        // Velocity lane drag
        if (draggingVelIdx >= 0)
        {
            setVelocityFromY(draggingVelIdx, e.getPosition().y);
            return;
        }

        if (pendingEmptyGesture)
        {
            const auto pos = e.getPosition();
            const int dxPixels = pos.x - pendingEmptyStartPos.x;
            const int dyPixels = pos.y - pendingEmptyStartPos.y;
            const bool crossedThreshold = std::abs(dxPixels) >= dragGestureThreshold
                                       || std::abs(dyPixels) >= dragGestureThreshold;

            if (! crossedThreshold)
                return;

            if (! selectToolEnabled)
            {
                const int newIndex = appendNote(pendingEmptyPitch, pendingEmptyBeat, snapBeats, 0.8f);
                selectSingleNote(newIndex);
                draggingIdx = newIndex;
                resizingNote = true;
                dragStartBeat = pendingEmptyBeat;
                dragStartPitch = pendingEmptyPitch;
                dragStartLen = snapBeats;
                // Anchor from the note's actual right edge (snapped beat + initial length),
                // not the raw click pixel — avoids pre-snap offset contaminating resize dx.
                dragStartMouseX = xFromBeat(pendingEmptyBeat + snapBeats);
                dragStartMouseY = pendingEmptyStartPos.y;
                pendingEmptyGesture = false;
            }
            else
            {
                marqueeSelecting = true;
                marqueeRect = makeMarqueeRect(pendingEmptyStartPos, pos);
                updateMarqueeSelection();
                repaint();
                return;
            }
        }

        if (marqueeSelecting)
        {
            marqueeRect = makeMarqueeRect(pendingEmptyStartPos, e.getPosition());
            updateMarqueeSelection();
            doAutoScroll(e);
            repaint();
            return;
        }

        if (draggingIdx < 0) return;
        auto& noteList = pattern->variations[variationIdx].notes[channel];
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
            moveDraggedSelection(snapBeat(dx), -(int)std::round(dy));
            doAutoScroll(e);
        }
        if (onNotesChanged) onNotesChanged();
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (rangeBarDragging) { stopRangeDrag(); return; }

        if (pendingEmptyGesture && ! marqueeSelecting)
        {
            if (selectToolEnabled)
            {
                if (! pendingEmptyGestureAdditive)
                    clearSelection();
            }
            else if (! pendingEmptyGestureAdditive)
            {
                const int newIndex = appendNote(pendingEmptyPitch, pendingEmptyBeat, snapBeats, 0.8f);
                selectSingleNote(newIndex);
            }
            resetInteractionState();
            if (onNotesChanged) onNotesChanged();
            repaint();
            return;
        }

        resetInteractionState();
    }

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

    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override
    {
        // Ctrl / Cmd + scroll → horizontal zoom anchored to cursor beat
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            const int   oldMouseX      = e.getPosition().x;
            const float beatUnderMouse = beatFromX(oldMouseX);

            // Delta sensitivity: ~25px per wheel notch
            const int newPPB = juce::jlimit(40, 400,
                                            pixelsPerBeat + (int)(wheel.deltaY * 25.0f));
            if (newPPB == pixelsPerBeat) return;

            pixelsPerBeat = newPPB;
            updateSizeForZoom();

            // Re-anchor: keep the beat under the cursor at the same screen x
            if (auto* vp = findParentComponentOfClass<juce::Viewport>())
            {
                const int newMouseX   = xFromBeat(beatUnderMouse);
                const int scrollDelta = newMouseX - oldMouseX;
                const int newScrollX  = juce::jmax(0, vp->getViewPositionX() + scrollDelta);
                vp->setViewPosition(newScrollX, vp->getViewPositionY());
            }

            repaint();
            return;
        }

        // No modifier — pass to Viewport for normal scrolling
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
            vp->mouseWheelMove(e.getEventRelativeTo(vp), wheel);
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        grabKeyboardFocus();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const int kc = key.getKeyCode();
        const auto mods = key.getModifiers();

        if ((mods.isCommandDown() || mods.isCtrlDown()) && (kc == 'a' || kc == 'A'))
        {
            selectAllNotes();
            return true;
        }

        // Cmd+C — copy selected notes
        if ((mods.isCommandDown() || mods.isCtrlDown()) && (kc == 'c' || kc == 'C'))
        {
            if (pattern != nullptr && hasSelectedNotes())
            {
                const auto& noteList = pattern->variations[variationIdx].notes[channel];
                noteClipboard.clear();
                float earliest = 1e9f;
                for (int i = 0; i < selectedNoteIndices.size(); ++i)
                {
                    const int idx = selectedNoteIndices[i];
                    if (juce::isPositiveAndBelow(idx, (int)noteList.size()))
                    {
                        noteClipboard.push_back(noteList[(size_t)idx]);
                        earliest = juce::jmin(earliest, noteList[(size_t)idx].startBeat);
                    }
                }
                clipboardBaseBeat = earliest;
            }
            return true;
        }

        // Cmd+V — paste notes at cursor
        if ((mods.isCommandDown() || mods.isCtrlDown()) && (kc == 'v' || kc == 'V'))
        {
            if (pattern != nullptr && !noteClipboard.empty())
            {
                auto& noteList = pattern->variations[variationIdx].notes[channel];
                selectedNoteIndices.clearQuick();
                for (const auto& src : noteClipboard)
                {
                    NoteEvent n = src;
                    n.startBeat = cursorBeat + (src.startBeat - clipboardBaseBeat);
                    if (n.startBeat < 0.0f) n.startBeat = 0.0f;
                    const int newIdx = (int)noteList.size();
                    noteList.push_back(n);
                    selectedNoteIndices.add(newIdx);
                }
                notifySelectionChanged();
                if (onNotesChanged) onNotesChanged();
                repaint();
            }
            return true;
        }

        // Cmd+D — duplicate selected notes (offset by rightmost extent)
        if ((mods.isCommandDown() || mods.isCtrlDown()) && (kc == 'd' || kc == 'D'))
        {
            if (pattern != nullptr && hasSelectedNotes())
            {
                auto& noteList = pattern->variations[variationIdx].notes[channel];
                float earliest = 1e9f, latestEnd = -1e9f;
                std::vector<NoteEvent> toDup;
                for (int i = 0; i < selectedNoteIndices.size(); ++i)
                {
                    const int idx = selectedNoteIndices[i];
                    if (juce::isPositiveAndBelow(idx, (int)noteList.size()))
                    {
                        const auto& n = noteList[(size_t)idx];
                        toDup.push_back(n);
                        earliest  = juce::jmin(earliest, n.startBeat);
                        latestEnd = juce::jmax(latestEnd, n.startBeat + n.lengthBeats);
                    }
                }
                const float offset = latestEnd - earliest;
                selectedNoteIndices.clearQuick();
                for (auto n : toDup)
                {
                    n.startBeat += offset;
                    const int newIdx = (int)noteList.size();
                    noteList.push_back(n);
                    selectedNoteIndices.add(newIdx);
                }
                notifySelectionChanged();
                if (onNotesChanged) onNotesChanged();
                repaint();
            }
            return true;
        }

        if (kc == juce::KeyPress::escapeKey)
        {
            clearSelection();
            return true;
        }

        // Space — play / stop (forward to main window)
        if (kc == juce::KeyPress::spaceKey)
        {
            if (onPlayStopToggle) onPlayStopToggle();
            return true;
        }

        // Tool mode shortcuts: B = draw, V = select
        if (kc == 'b' || kc == 'B')
        {
            setSelectToolEnabled(false);
            return true;
        }
        if (kc == 'v' || kc == 'V')
        {
            if (!mods.isCommandDown() && !mods.isCtrlDown())
            {
                setSelectToolEnabled(true);
                return true;
            }
        }

        // Octave shift (Z = down, X = up)
        if (kc == 'z' || kc == 'Z')
        {
            keyboardBasePitch = juce::jlimit(minPitch, maxPitch - (kKeyboardSpan - 1),
                                             keyboardBasePitch - 12);
            if (onKeyboardRangeChanged) onKeyboardRangeChanged(keyboardBasePitch);
            repaint();
            return true;
        }
        if (kc == 'x' || kc == 'X')
        {
            keyboardBasePitch = juce::jlimit(minPitch, maxPitch - (kKeyboardSpan - 1),
                                             keyboardBasePitch + 12);
            if (onKeyboardRangeChanged) onKeyboardRangeChanged(keyboardBasePitch);
            repaint();
            return true;
        }

        // Cursor navigation — Arrow keys
        if (kc == juce::KeyPress::leftKey)
        {
            if (mods.isShiftDown())
            {
                nudgeActiveNotes(-snapBeats);
                return true;
            }
            cursorBeat = juce::jmax(0.0f, cursorBeat - snapBeats);
            repaint();
            scrollToCursor();
            return true;
        }
        if (kc == juce::KeyPress::rightKey)
        {
            if (mods.isShiftDown())
            {
                nudgeActiveNotes(snapBeats);
                return true;
            }
            const float maxBeat = pattern ? (float)(pattern->stepCount) * 0.25f : 4.0f;
            cursorBeat = juce::jmin(maxBeat - snapBeats, cursorBeat + snapBeats);
            repaint();
            scrollToCursor();
            return true;
        }
        if (kc == juce::KeyPress::upKey)
        {
            if (mods.isShiftDown())
            {
                nudgeActiveNotesPitch(+1);
                return true;
            }
            cursorPitch = moveCursorByScaleStep(+1);
            repaint();
            scrollToCursor();
            return true;
        }
        if (kc == juce::KeyPress::downKey)
        {
            if (mods.isShiftDown())
            {
                nudgeActiveNotesPitch(-1);
                return true;
            }
            cursorPitch = moveCursorByScaleStep(-1);
            repaint();
            scrollToCursor();
            return true;
        }

        // Enter — place note at cursor
        if (kc == juce::KeyPress::returnKey)
        {
            if (pattern == nullptr) return true;
            auto& noteList = pattern->variations[variationIdx].notes[channel];
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
            auto& noteList = pattern->variations[variationIdx].notes[channel];

            if (hasSelectedNotes())
            {
                auto sorted = selectedNoteIndices;
                sorted.sort();
                for (int s = sorted.size() - 1; s >= 0; --s)
                {
                    const int idx = sorted[s];
                    if (juce::isPositiveAndBelow(idx, (int)noteList.size()))
                    {
                        const int deletedPitch = noteList[(size_t)idx].pitch;
                        noteList.erase(noteList.begin() + idx);
                        if (onNoteDeleted) onNoteDeleted(deletedPitch);
                    }
                }
                clearSelection(false);
                if (onNotesChanged) onNotesChanged();
                repaint();
                return true;
            }

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
            const int  pitch = keyboardBasePitch + km.semitone;
            const int  pidx  = juce::jlimit(0, 127, pitch);

            if (down && !heldKeyState[idx])
            {
                heldKeyState[idx]       = true;
                keyboardHeldPitch[pidx] = true;
                needRepaint = true;
                if (pitch >= minPitch && pitch <= maxPitch && onKeyPreview)
                    onKeyPreview(pitch);

                // Armed → Recording trigger: first keypress fires transport
                if (currentRecState == RecState::Armed)
                {
                    startRecording();
                    playheadBeat = 0.0;  // engine will start at beat 0; pin local state
                    if (onStartTransport) onStartTransport();
                }

                // Recording: note-on — capture quantize policy at this instant
                if (recording_ && playheadBeat >= 0.0 && pattern != nullptr)
                {
                    auto& prn         = pendingRecNotes_[(size_t)pidx];
                    prn.active        = true;
                    prn.wasQuantized  = quantizeEnabled;   // snapshot
                    prn.capturedGrid  = quantizeGrid;      // snapshot
                    const float rawBeat    = (float)playheadBeat;
                    const float totalBeats = (float)pattern->stepCount * 0.25f;
                    prn.startBeat = prn.wasQuantized
                        ? std::round(rawBeat / prn.capturedGrid) * prn.capturedGrid
                        : rawBeat;
                    // Quantize rounding can push startBeat to exactly totalBeats — wrap to 0
                    if (prn.startBeat >= totalBeats)
                        prn.startBeat = 0.0f;
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
                        const float totalBeats = (float)pattern->stepCount * 0.25f;
                        float len = (float)playheadBeat - prn.startBeat;
                        // Loop wraparound — only apply if len is genuinely negative
                        // (beyond quantize rounding error), not just a floating-point artifact
                        if (len < -(prn.capturedGrid * 0.5f))
                            len += totalBeats;

                        // Use the policy captured at note-on — not current state
                        const float minLen = prn.wasQuantized ? prn.capturedGrid : kMinNoteLen;
                        if (prn.wasQuantized)
                            len = std::round(len / prn.capturedGrid) * prn.capturedGrid;
                        len = juce::jmax(minLen, len);

                        NoteEvent n;
                        n.pitch       = pidx;
                        n.startBeat   = prn.startBeat;
                        n.lengthBeats = len;
                        n.velocity    = 0.8f;
                        pattern->variations[variationIdx].notes[channel].push_back(n);
                        sessionEndIdx = (int)pattern->variations[variationIdx].notes[channel].size() - 1;
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
    static constexpr int   minPitch     = 21;   // A0
    static constexpr int   maxPitch     = 108;  // C8
    int pixelsPerBeat = 80;   // mutable: changed by setZoom / Ctrl+scroll
    static constexpr int   resizeZone   = 8;    // px from right edge
    static constexpr int   velLaneH     = 72;   // velocity lane height
    static constexpr int   velLaneSep   = 4;    // gap between note area and vel lane
    static constexpr float kMinNoteLen  = 0.05f;   // minimum note length (matches finest snap)

    // Keyboard range — base pitch of lowest keyboard key ('a'), free semitone resolution
    int  keyboardBasePitch  = 60;   // default C4
    bool rangeBarDragging   = false;
    int  rangeBarDragStartY = 0;
    int  rangeBarDragStartBase = 0;

    bool  heldKeyState[256] = {};
    bool  keyboardHeldPitch[128] = {};

    // Keyboard cursor for note entry (arrow key navigation)
    float cursorBeat  = 0.0f;
    int   cursorPitch = 60;

    // ---- Recording state --------------------------------------------------
    bool     recording_        = false;
    RecState currentRecState   = RecState::Idle;
    bool     triggerEnabled    = false;
    bool     armedBlink        = false;
    int      sessionStartIdx   = 0;   // notes[channel] index at record start
    int      sessionEndIdx     = -1;  // index of last committed note in session
    bool  quantizeEnabled  = true;    // real-time quantize ON by default
    float quantizeGrid     = 0.25f;   // quantize resolution (beats); default 1/16

    // Quantize settings are captured at note-on time so that a single NoteEvent
    // is always processed with a consistent policy, even if the user toggles the
    // button between note-on and note-off.
    struct PendingRecNote
    {
        bool  active        = false;
        float startBeat     = 0.0f;
        bool  wasQuantized  = false;  // snapshot of quantizeEnabled at note-on
        float capturedGrid  = 0.25f;  // snapshot of quantizeGrid   at note-on
    };
    std::array<PendingRecNote, 128> pendingRecNotes_ {};

    int   hoverPitch      = -1;
    float hoverStepBeat   = -1.0f;   // beat position of hovered 1/16 column (-1 = none)
    KeySignature keySignature {};
    juce::Array<int> selectedNoteIndices;
    int   draggingIdx     = -1;
    int   draggingVelIdx  = -1;   // index of note being velocity-edited
    bool  resizingNote    = false;
    float dragStartBeat   = 0.0f;
    int   dragStartPitch  = 0;
    float dragStartLen    = 0.0f;
    int   dragStartMouseX = 0;
    int   dragStartMouseY = 0;
    bool  pendingEmptyGesture = false;
    bool  pendingEmptyGestureAdditive = false;
    bool  selectToolEnabled = false;
    int   pendingEmptyPitch = 60;
    float pendingEmptyBeat = 0.0f;
    juce::Point<int> pendingEmptyStartPos;
    bool marqueeSelecting = false;
    juce::Rectangle<int> marqueeRect;
    juce::Array<int> marqueeBaseSelection;
    static constexpr int dragGestureThreshold = 4;

    struct SelectedNoteDragState
    {
        int index = -1;
        float startBeat = 0.0f;
        int startPitch = 60;
    };
    std::vector<SelectedNoteDragState> selectedNoteDragStates;

    // Clipboard for note copy/paste
    std::vector<NoteEvent> noteClipboard;
    float                  clipboardBaseBeat = 0.0f;

    // Resize the component to match the current pixelsPerBeat, preserving Viewport fit
    void updateSizeForZoom()
    {
        if (auto* vp = findParentComponentOfClass<juce::Viewport>())
        {
            const int cw = juce::jmax(vp->getWidth(),  getNeededWidth());
            const int ch = juce::jmax(vp->getHeight(), getNeededHeight());
            setSize(cw, ch);
        }
        else
        {
            setSize(getNeededWidth(), getNeededHeight());
        }
    }

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
    bool  isScalePitch(int pitch) const { return MusicTheory::isPitchInScale(pitch, keySignature); }
    int   moveCursorByScaleStep(int direction) const
    {
        const int snapped = MusicTheory::snapPitchToScale(cursorPitch, keySignature);
        return juce::jlimit(minPitch, maxPitch,
                            MusicTheory::moveScaleStep(snapped, keySignature, direction));
    }
    juce::Colour rowColourForPitch(int pitch) const
    {
        if (isScalePitch(pitch))
            return isBlackKey(pitch) ? juce::Colour(0xff1a2140) : juce::Colour(0xff252f58);
        return isBlackKey(pitch) ? juce::Colour(0xff161628) : juce::Colour(0xff1e1e3a);
    }
    juce::String pitchLabel(int pitch) const
    {
        return MusicTheory::noteNameForPitch(pitch, keySignature) + juce::String(pitch / 12 - 1);
    }

    void clearSelection(bool shouldRepaint = true)
    {
        if (selectedNoteIndices.isEmpty())
            return;

        selectedNoteIndices.clearQuick();
        notifySelectionChanged();
        if (shouldRepaint)
            repaint();
    }

    bool isNoteSelected(int idx) const
    {
        return selectedNoteIndices.contains(idx);
    }

    void selectSingleNote(int idx)
    {
        selectedNoteIndices.clearQuick();
        if (idx >= 0)
            selectedNoteIndices.add(idx);
        notifySelectionChanged();
        repaint();
    }

    void toggleNoteSelection(int idx)
    {
        if (idx < 0)
            return;

        if (selectedNoteIndices.contains(idx))
            selectedNoteIndices.removeFirstMatchingValue(idx);
        else
            selectedNoteIndices.addIfNotAlreadyThere(idx);

        notifySelectionChanged();
        repaint();
    }

    void removeSelectionIndex(int erasedIdx)
    {
        bool changed = false;
        for (int i = selectedNoteIndices.size() - 1; i >= 0; --i)
        {
            int idx = selectedNoteIndices.getReference(i);
            if (idx == erasedIdx)
            {
                selectedNoteIndices.remove(i);
                changed = true;
            }
            else if (idx > erasedIdx)
            {
                selectedNoteIndices.set(i, idx - 1);
                changed = true;
            }
        }
        if (changed)
            notifySelectionChanged();
    }

    int appendNote(int pitch, float beat, float lengthBeats, float velocity)
    {
        auto& noteList = pattern->variations[variationIdx].notes[channel];
        NoteEvent n;
        n.pitch = pitch;
        n.startBeat = beat;
        n.lengthBeats = lengthBeats;
        n.velocity = velocity;
        noteList.push_back(n);
        return (int)noteList.size() - 1;
    }

    void resetInteractionState()
    {
        draggingIdx = -1;
        draggingVelIdx = -1;
        resizingNote = false;
        pendingEmptyGesture = false;
        pendingEmptyGestureAdditive = false;
        marqueeSelecting = false;
        marqueeRect = {};
        marqueeBaseSelection.clearQuick();
        selectedNoteDragStates.clear();
    }

    int progressionTonicMidi() const
    {
        int tonicMidi = 60 + keySignature.tonic;
        while (tonicMidi > 64)
            tonicMidi -= 12;
        while (tonicMidi < 48)
            tonicMidi += 12;
        return tonicMidi;
    }

    std::vector<int> buildChordPitches(const MusicTheory::ProgressionStep& step,
                                       ChordVoicing voicing) const
    {
        const auto& scaleIntervals = MusicTheory::getScaleIntervals(keySignature.scale);
        const int baseTonic = progressionTonicMidi();
        const int safeDegree = juce::jlimit(0, 6, step.degree);
        const int chordTones = juce::jlimit(3, 4, step.chordTones);

        std::vector<int> pitches;
        pitches.reserve((size_t)chordTones);

        for (int toneIndex = 0; toneIndex < chordTones; ++toneIndex)
        {
            const int stackedDegree = safeDegree + toneIndex * 2;
            const int octave = stackedDegree / 7;
            const int scaleIndex = stackedDegree % 7;
            int pitch = baseTonic + scaleIntervals[(size_t)scaleIndex] + octave * 12;

            if (! pitches.empty())
                while (pitch <= pitches.back())
                    pitch += 12;

            pitches.push_back(juce::jlimit(minPitch, maxPitch, pitch));
        }

        auto raiseLowest = [&pitches]
        {
            if (pitches.empty())
                return;

            pitches.front() += 12;
            std::sort(pitches.begin(), pitches.end());
        };

        switch (voicing)
        {
            case ChordVoicing::Close:
                break;

            case ChordVoicing::Open:
                if (pitches.size() >= 3)
                {
                    pitches[1] += 12;
                    std::sort(pitches.begin(), pitches.end());
                }
                break;

            case ChordVoicing::FirstInversion:
                raiseLowest();
                break;

            case ChordVoicing::SecondInversion:
                raiseLowest();
                raiseLowest();
                break;

            case ChordVoicing::Drop2:
                if (pitches.size() >= 4)
                {
                    pitches[pitches.size() - 2] -= 12;
                    std::sort(pitches.begin(), pitches.end());
                }
                else if (pitches.size() >= 3)
                {
                    pitches[1] -= 12;
                    std::sort(pitches.begin(), pitches.end());
                }
                break;
        }

        for (auto& pitch : pitches)
            pitch = juce::jlimit(minPitch, maxPitch, pitch);

        std::sort(pitches.begin(), pitches.end());

        return pitches;
    }

    struct ChordRegion
    {
        float startBeat = 0.0f;
        float endBeat = 0.0f;
        int rootPitch = 36;
    };

    int bassRegisterPitch(int sourcePitch) const
    {
        int bassPitch = 36 + (((sourcePitch % 12) + 12) % 12);
        while (bassPitch > 43)
            bassPitch -= 12;
        while (bassPitch < 31)
            bassPitch += 12;
        return juce::jlimit(minPitch, maxPitch, bassPitch);
    }

    int inferChordRootPitch(const std::vector<int>& chordPitches) const
    {
        if (chordPitches.empty())
            return 36;

        int bestPitchClass = ((chordPitches.front() % 12) + 12) % 12;
        int bestScore = std::numeric_limits<int>::min();

        for (const int candidatePitch : chordPitches)
        {
            const int candidatePc = ((candidatePitch % 12) + 12) % 12;
            int score = 0;

            for (const int pitch : chordPitches)
            {
                const int interval = (((pitch % 12) + 12) % 12 - candidatePc + 12) % 12;
                if (interval == 0) score += 6;
                else if (interval == 3 || interval == 4) score += 4;
                else if (interval == 7) score += 5;
                else if (interval == 10 || interval == 11) score += 2;
                else if (interval == 2 || interval == 5 || interval == 9) score += 1;
                else score -= 2;
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestPitchClass = candidatePc;
            }
        }

        const int tonicBase = 36 + keySignature.tonic;
        int rootPitch = tonicBase + ((bestPitchClass - keySignature.tonic + 12) % 12);
        while (rootPitch > 43)
            rootPitch -= 12;
        while (rootPitch < 31)
            rootPitch += 12;
        return juce::jlimit(minPitch, maxPitch, rootPitch);
    }

    std::vector<ChordRegion> extractChordRegions() const
    {
        std::vector<ChordRegion> regions;
        if (pattern == nullptr)
            return regions;

        const auto& noteList = pattern->variations[variationIdx].notes[channel];
        if (noteList.empty())
            return regions;

        struct SourceNote
        {
            float startBeat = 0.0f;
            int pitch = 60;
        };

        std::vector<SourceNote> sorted;
        sorted.reserve(noteList.size());
        for (const auto& note : noteList)
            sorted.push_back({ note.startBeat, note.pitch });

        std::sort(sorted.begin(), sorted.end(), [](const SourceNote& a, const SourceNote& b)
        {
            if (std::abs(a.startBeat - b.startBeat) > 0.0001f)
                return a.startBeat < b.startBeat;
            return a.pitch < b.pitch;
        });

        const float totalBeats = pattern->stepCount * 0.25f;
        size_t i = 0;
        while (i < sorted.size())
        {
            const float groupStart = sorted[i].startBeat;
            std::vector<int> chordPitches;
            chordPitches.push_back(sorted[i].pitch);
            size_t j = i + 1;
            while (j < sorted.size() && std::abs(sorted[j].startBeat - groupStart) < 0.0001f)
            {
                chordPitches.push_back(sorted[j].pitch);
                ++j;
            }

            const float groupEnd = j < sorted.size() ? sorted[j].startBeat : totalBeats;
            if (groupEnd > groupStart + 0.0001f)
                regions.push_back({ groupStart, groupEnd, inferChordRootPitch(chordPitches) });

            i = j;
        }

        return regions;
    }

    void appendBassNote(std::vector<NoteEvent>& noteList, int pitch, float startBeat, float lengthBeats, float velocity) const
    {
        NoteEvent note;
        note.pitch = juce::jlimit(minPitch, maxPitch, pitch);
        note.startBeat = juce::jmax(0.0f, startBeat);
        note.lengthBeats = juce::jmax(kMinNoteLen, lengthBeats);
        note.velocity = juce::jlimit(0.2f, 1.0f, velocity);
        noteList.push_back(note);
    }

    void appendBassRegion(std::vector<NoteEvent>& noteList,
                          float startBeat,
                          float endBeat,
                          int rootPitch,
                          int nextRootPitch,
                          BasslinePattern basslinePattern) const
    {
        const float regionLength = juce::jmax(kMinNoteLen, endBeat - startBeat);
        const float pulse = 0.5f;

        switch (basslinePattern)
        {
            case BasslinePattern::RootNotes:
                appendBassNote(noteList, rootPitch, startBeat, regionLength, 0.9f);
                break;

            case BasslinePattern::OctaveBounce:
            {
                int stepIndex = 0;
                for (float beat = startBeat; beat < endBeat - 0.0001f; beat += pulse, ++stepIndex)
                {
                    const float noteLen = juce::jmin(pulse, endBeat - beat);
                    const int pitch = (stepIndex % 2 == 0) ? rootPitch : juce::jmin(maxPitch, rootPitch + 12);
                    appendBassNote(noteList, pitch, beat, noteLen, stepIndex % 2 == 0 ? 0.92f : 0.74f);
                }
                break;
            }

            case BasslinePattern::Pulse8ths:
                for (float beat = startBeat; beat < endBeat - 0.0001f; beat += pulse)
                    appendBassNote(noteList, rootPitch, beat, juce::jmin(pulse, endBeat - beat), 0.86f);
                break;

            case BasslinePattern::PassingTone:
            {
                const float leadLength = juce::jmin(0.5f, regionLength * 0.33f);
                const float holdLength = juce::jmax(kMinNoteLen, regionLength - leadLength);
                appendBassNote(noteList, rootPitch, startBeat, holdLength, 0.9f);

                if (nextRootPitch != rootPitch && regionLength > (kMinNoteLen + 0.1f))
                {
                    const int direction = nextRootPitch > rootPitch ? 1 : -1;
                    const int passingPitch = MusicTheory::moveScaleStep(rootPitch, keySignature, direction);
                    appendBassNote(noteList, passingPitch, endBeat - leadLength, leadLength, 0.72f);
                }
                break;
            }
        }
    }

    void captureSelectedNoteDragState()
    {
        selectedNoteDragStates.clear();
        if (pattern == nullptr)
            return;

        const auto& noteList = pattern->variations[variationIdx].notes[channel];
        for (int i = 0; i < selectedNoteIndices.size(); ++i)
        {
            const int idx = selectedNoteIndices[i];
            if (! juce::isPositiveAndBelow(idx, (int)noteList.size()))
                continue;

            selectedNoteDragStates.push_back({ idx, noteList[(size_t)idx].startBeat, noteList[(size_t)idx].pitch });
        }
    }

    // Scroll the parent viewport when the mouse is near the top/bottom edge during drag
    void doAutoScroll(const juce::MouseEvent& e)
    {
        auto* vp = findParentComponentOfClass<juce::Viewport>();
        if (vp == nullptr) return;

        const int mouseY   = e.getPosition().y;
        const int viewTop  = vp->getViewPositionY();
        const int viewBot  = viewTop + vp->getViewHeight();
        const int margin   = 40;
        const int maxSpeed = 16;

        int scrollDY = 0;
        if (mouseY < viewTop + margin)
            scrollDY = -(int)juce::jmap((float)(mouseY - viewTop), 0.0f, (float)margin, (float)maxSpeed, 1.0f);
        else if (mouseY > viewBot - margin)
            scrollDY =  (int)juce::jmap((float)(viewBot - mouseY), 0.0f, (float)margin, (float)maxSpeed, 1.0f);

        if (scrollDY != 0)
            vp->setViewPosition(vp->getViewPositionX(),
                                juce::jlimit(0, vp->getViewedComponent()->getHeight() - vp->getViewHeight(),
                                             viewTop + scrollDY));
    }

    void moveDraggedSelection(float requestedBeatDelta, int requestedPitchDelta)
    {
        if (pattern == nullptr || selectedNoteDragStates.empty())
            return;

        auto& noteList = pattern->variations[variationIdx].notes[channel];
        float allowedBeatDelta = requestedBeatDelta;
        int allowedPitchDelta = requestedPitchDelta;

        for (const auto& state : selectedNoteDragStates)
        {
            allowedBeatDelta = juce::jmax(allowedBeatDelta, -state.startBeat);
            allowedPitchDelta = juce::jmax(allowedPitchDelta, minPitch - state.startPitch);
            allowedPitchDelta = juce::jmin(allowedPitchDelta, maxPitch - state.startPitch);
        }

        for (const auto& state : selectedNoteDragStates)
        {
            if (! juce::isPositiveAndBelow(state.index, (int)noteList.size()))
                continue;

            auto& note = noteList[(size_t)state.index];
            note.startBeat = juce::jmax(0.0f, state.startBeat + allowedBeatDelta);
            note.pitch = juce::jlimit(minPitch, maxPitch, state.startPitch + allowedPitchDelta);
        }
    }

    void nudgeSelectedNotes(float deltaBeats)
    {
        if (pattern == nullptr || selectedNoteIndices.isEmpty())
            return;

        auto& noteList = pattern->variations[variationIdx].notes[channel];
        float allowedDelta = deltaBeats;
        for (int i = 0; i < selectedNoteIndices.size(); ++i)
        {
            const int idx = selectedNoteIndices[i];
            if (juce::isPositiveAndBelow(idx, (int)noteList.size()))
                allowedDelta = juce::jmax(allowedDelta, -noteList[(size_t)idx].startBeat);
        }

        for (int i = 0; i < selectedNoteIndices.size(); ++i)
        {
            const int idx = selectedNoteIndices[i];
            if (juce::isPositiveAndBelow(idx, (int)noteList.size()))
                noteList[(size_t)idx].startBeat = juce::jmax(0.0f, noteList[(size_t)idx].startBeat + allowedDelta);
        }

        if (onNotesChanged) onNotesChanged();
        repaint();
    }

    void nudgeSelectedNotesPitch(int deltaSemitones)
    {
        if (pattern == nullptr || selectedNoteIndices.isEmpty())
            return;

        auto& noteList = pattern->variations[variationIdx].notes[channel];
        int allowedDelta = deltaSemitones;
        for (int i = 0; i < selectedNoteIndices.size(); ++i)
        {
            const int idx = selectedNoteIndices[i];
            if (! juce::isPositiveAndBelow(idx, (int)noteList.size()))
                continue;

            const int pitch = noteList[(size_t)idx].pitch;
            allowedDelta = juce::jmax(allowedDelta, minPitch - pitch);
            allowedDelta = juce::jmin(allowedDelta, maxPitch - pitch);
        }

        for (int i = 0; i < selectedNoteIndices.size(); ++i)
        {
            const int idx = selectedNoteIndices[i];
            if (juce::isPositiveAndBelow(idx, (int)noteList.size()))
                noteList[(size_t)idx].pitch = juce::jlimit(minPitch, maxPitch,
                                                           noteList[(size_t)idx].pitch + allowedDelta);
        }

        if (onNotesChanged) onNotesChanged();
        repaint();
    }

    void updateMarqueeSelection()
    {
        if (pattern == nullptr)
            return;

        juce::Array<int> nextSelection = pendingEmptyGestureAdditive ? marqueeBaseSelection : juce::Array<int>();
        const auto& noteList = pattern->variations[variationIdx].notes[channel];
        const auto area = marqueeRect;

        for (int i = 0; i < (int)noteList.size(); ++i)
        {
            if (area.intersects(noteHitRect(noteList[(size_t)i])))
                nextSelection.addIfNotAlreadyThere(i);
        }

        selectedNoteIndices = nextSelection;
        notifySelectionChanged();
    }

    juce::Rectangle<int> makeMarqueeRect(juce::Point<int> a, juce::Point<int> b) const
    {
        return juce::Rectangle<int>::leftTopRightBottom(juce::jmin(a.x, b.x),
                                                        juce::jmin(a.y, b.y),
                                                        juce::jmax(a.x, b.x),
                                                        juce::jmax(a.y, b.y));
    }

    void notifySelectionChanged()
    {
        if (onSelectionChanged)
            onSelectionChanged();
    }

    // Find the topmost note whose time range contains `beat` (for vel lane hit)
    int findNoteAtBeat(float beat) const
    {
        if (pattern == nullptr) return -1;
        const auto& nl = pattern->variations[variationIdx].notes[channel];
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
            idx >= (int)pattern->variations[variationIdx].notes[channel].size()) return;

        const float vel = 1.0f - juce::jlimit(0.0f, 1.0f,
                                   (float)(mouseY - velLaneY()) / (float)(velLaneH - 2));
        pattern->variations[variationIdx].notes[channel][(size_t)idx].velocity = vel;
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
            const bool inScale = isScalePitch(pitch);

            const bool hovered  = (pitch == hoverPitch);
            const bool kbActive = (pitch >= 0 && pitch < 128 && keyboardHeldPitch[pitch]);
            g.setColour(kbActive        ? juce::Colour(0xffff9500)
                        : hovered       ? juce::Colour(0xff3498db)
                        : inScale       ? (black ? juce::Colour(0xff2d3658)
                                                  : juce::Colour(0xff465987))
                        : black         ? juce::Colour(0xff2a2a2a)
                                        : juce::Colour(0xff3a3a4a));
            g.fillRect(0, y, keyWidth, noteH);

            g.setColour(juce::Colour(0xff111122));
            g.drawLine(0.0f, (float)(y + noteH), (float)keyWidth, (float)(y + noteH), 0.5f);

            if (pitch % 12 == keySignature.tonic || pitch % 12 == 0)
            {
                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
                g.drawText(pitchLabel(pitch),
                           2, y, keyWidth - 4, noteH, juce::Justification::centredLeft);
            }
        }
        // Right border
        g.setColour(juce::Colour(0xff0f3460));
        g.drawLine((float)keyWidth, 0.0f, (float)keyWidth, (float)getHeight(), 1.0f);

        // Keyboard range indicator strip
        drawRangeBar(g);
    }

    void drawRangeBar(juce::Graphics& g)
    {
        const int clampedTop = juce::jmin(keyboardBasePitch + kKeyboardSpan - 1, maxPitch);
        const juce::Colour barCol = rangeBarDragging ? juce::Colour(0xffff9500)
                                                     : juce::Colour(0xffff9500).withAlpha(0.65f);

        // Fill strip for each pitch in range
        for (int pitch = keyboardBasePitch; pitch <= clampedTop; ++pitch)
        {
            const int y = yFromPitch(pitch);
            g.setColour(barCol);
            g.fillRect(0, y, kRangeBarW, noteH);
        }

        // Border lines to make it read as a single block
        const int stripTop    = yFromPitch(clampedTop);
        const int stripBottom = yFromPitch(keyboardBasePitch) + noteH;
        g.setColour(juce::Colour(0xffff9500));
        g.drawRect(0, stripTop, kRangeBarW, stripBottom - stripTop, 1);

        // Top arrow ▲ (drag handle hint)
        {
            juce::Path tri;
            tri.addTriangle(0.0f,          (float)stripTop,
                            (float)kRangeBarW, (float)stripTop,
                            (float)(kRangeBarW / 2), (float)(stripTop - 5));
            g.setColour(juce::Colour(0xffff9500));
            g.fillPath(tri);
        }
        // Bottom arrow ▼
        {
            juce::Path tri;
            tri.addTriangle(0.0f,          (float)stripBottom,
                            (float)kRangeBarW, (float)stripBottom,
                            (float)(kRangeBarW / 2), (float)(stripBottom + 5));
            g.setColour(juce::Colour(0xffff9500));
            g.fillPath(tri);
        }

        // Base note label beside the strip (e.g. "C4")
        const juce::String lbl = pitchLabel(keyboardBasePitch);
        g.setColour(juce::Colour(0xffff9500).withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(8.5f)));
        g.drawText(lbl, kRangeBarW + 2, stripBottom - 11, 28, 10,
                   juce::Justification::centredLeft);
    }

    void drawVelocityKeyStub(juce::Graphics& g)
    {
        const int laneY = velLaneY();
        g.setColour(juce::Colour(0xff888892));
        g.fillRect(0, laneY, keyWidth, velLaneH);
        g.setColour(juce::Colour(0xffb0b0b8));
        g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
        g.drawText("VELOCITY", 2, laneY + 2, keyWidth - 4, 11,
                   juce::Justification::centredLeft);
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
            g.setColour(rowColourForPitch(pitch));
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
        // Background — colour by state: orange=Armed, red=Recording, blue=normal
        g.setColour(currentRecState == RecState::Armed
                        ? juce::Colour(0xff2e1800)
                        : recording_
                            ? juce::Colour(0xff3a0a0a)
                            : juce::Colour(0xff0f3460));
        g.fillRect(0, 0, getWidth(), headerH);

        // Hover step tick mark in the ruler
        if (hoverStepBeat >= 0.0f)
        {
            const int hx = xFromBeat(hoverStepBeat);
            const int hw = (int)(0.25f * pixelsPerBeat);
            g.setColour(juce::Colour(0xffffffff).withAlpha(0.08f));
            g.fillRect(hx, 0, hw, headerH);
        }

        // State indicator (ARM or REC)
        if (currentRecState == RecState::Armed)
        {
            if (armedBlink)
            {
                g.setColour(juce::Colour(0xffff8800));
                g.fillEllipse(4.0f, 5.0f, 8.0f, 8.0f);
            }
            g.setColour(juce::Colour(0xffff8800).withAlpha(0.90f));
            g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
            g.drawText("ARM", 15, 0, 36, headerH, juce::Justification::centredLeft);
        }
        else if (recording_ && playheadBeat >= 0.0)
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
        const auto& noteList = pattern->variations[variationIdx].notes[channel];

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
            const bool inScale = isScalePitch(n.pitch);
            const juce::Colour noteFill = inScale ? fill : fill.withAlpha(0.45f);
            const bool isSelected = isNoteSelected(i);

            const bool isVelDragging = (i == draggingVelIdx);
            g.setColour((i == draggingIdx || isVelDragging || isSelected) ? noteFill.brighter(0.35f) : noteFill);
            g.fillRoundedRectangle(r.toFloat(), 2.0f);
            g.setColour(isSelected ? juce::Colours::white.withAlpha(0.9f) : noteFill.darker(0.3f));
            g.drawRoundedRectangle(r.toFloat(), 2.0f, isSelected ? 1.6f : 1.0f);

            // Resize handle stripe
            g.setColour(noteFill.brighter(0.5f).withAlpha(0.6f));
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

            // Step-count hint when resizing (especially useful with Alt = free resize)
            if (i == draggingIdx && resizingNote)
            {
                const float steps     = n.lengthBeats / 0.25f;
                const juce::String txt = juce::String(steps, 2) + " steps";
                g.setColour(juce::Colours::white.withAlpha(0.92f));
                g.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
                g.drawText(txt, r.getX(), r.getY() - 13, 70, 11,
                           juce::Justification::centredLeft);
            }
        }
    }

    void drawMarqueeSelection(juce::Graphics& g)
    {
        if (! marqueeSelecting)
            return;

        const auto area = marqueeRect;
        g.setColour(juce::Colour(0x663498db));
        g.fillRect(area);
        g.setColour(juce::Colour(0xff7fc9ff));
        g.drawRect(area, 1);
    }

    void drawVelocityLane(juce::Graphics& g)
    {
        if (pattern == nullptr) return;

        const int laneY = velLaneY();

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

        for (float pct : { 0.25f, 0.5f, 0.75f })
        {
            const int guideY = laneY + (int)((1.0f - pct) * velLaneH);
            g.setColour(juce::Colour(0xff3a3a5a));
            g.drawLine((float)keyWidth, (float)guideY,
                       (float)getWidth(), (float)guideY, 0.5f);
        }

        // Velocity bars
        const auto& noteList = pattern->variations[variationIdx].notes[channel];
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

            if (cx < nx + margin)                        nx = juce::jmax(keyWidth, cx - margin);
            else if (cx > nx + va.getWidth() - margin)   nx = cx - va.getWidth() + margin;

            if (cy < ny + margin)                        ny = juce::jmax(0, cy - margin);
            else if (cy + noteH > ny + va.getHeight() - margin)
                ny = cy + noteH - va.getHeight() + margin;

            vp->setViewPosition(nx, ny);
        }
    }

    void drawPlayhead(juce::Graphics& g)
    {
        // Armed state: frozen blinking playhead at beat 0
        if (currentRecState == RecState::Armed)
        {
            if (armedBlink)
            {
                const int x = xFromBeat(0.0f);
                g.setColour(juce::Colour(0xffff8800).withAlpha(0.70f));
                g.drawLine((float)x, 0.0f, (float)x, (float)getHeight(), 2.5f);
                juce::Path cap;
                cap.addTriangle((float)(x - 5), 0.0f,
                                (float)(x + 5), 0.0f,
                                (float)x,       6.0f);
                g.setColour(juce::Colour(0xffff8800));
                g.fillPath(cap);
            }
            return;
        }

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

    // ---- Armed overlay (blink) --------------------------------------------
    void drawArmedOverlay(juce::Graphics& g)
    {
        if (currentRecState != RecState::Armed) return;

        // Pulsing tint over the note area
        g.setColour(juce::Colour(0xffcc1111).withAlpha(armedBlink ? 0.07f : 0.02f));
        g.fillRect(keyWidth, headerH, getWidth() - keyWidth, velLaneY() - headerH);

        // Text centred in the visible note grid
        g.setColour(juce::Colour(0xffff5555).withAlpha(armedBlink ? 0.92f : 0.35f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f).withStyle("Bold")));
        g.drawText("Waiting for key input...",
                   keyWidth, headerH,
                   getWidth() - keyWidth, velLaneY() - headerH,
                   juce::Justification::centred);
    }

    // ---- State machine transitions ----------------------------------------
    void enterArmed()
    {
        currentRecState = RecState::Armed;
        recording_      = false;
        pendingRecNotes_.fill({});
        if (pattern != nullptr)
            sessionStartIdx = (int)pattern->variations[variationIdx].notes[channel].size();
        sessionEndIdx = sessionStartIdx - 1;
        if (onRecordingStateChanged) onRecordingStateChanged();
        repaint();
    }

    void startRecording()
    {
        currentRecState = RecState::Recording;
        recording_      = true;
        if (pattern != nullptr)
            sessionStartIdx = (int)pattern->variations[variationIdx].notes[channel].size();
        sessionEndIdx = sessionStartIdx - 1;
        if (onRecordingStateChanged) onRecordingStateChanged();
        repaint();
    }

    void stopRecording()
    {
        // Commit every held note at current playhead position (prevents note loss)
        if (recording_ && pattern != nullptr && playheadBeat >= 0.0)
        {
            bool anyCommitted = false;
            for (int pidx = 0; pidx < 128; ++pidx)
            {
                auto& prn = pendingRecNotes_[(size_t)pidx];
                if (!prn.active) continue;

                prn.active = false;
                float len  = (float)playheadBeat - prn.startBeat;
                if (len <= 0.0f)
                    len += (float)pattern->stepCount * 0.25f;

                if (prn.wasQuantized)
                    len = juce::jmax(prn.capturedGrid,
                                     std::round(len / prn.capturedGrid) * prn.capturedGrid);
                else
                    len = juce::jmax(kMinNoteLen, len);

                NoteEvent n;
                n.pitch       = pidx;
                n.startBeat   = prn.startBeat;
                n.lengthBeats = len;
                n.velocity    = 0.8f;
                pattern->variations[variationIdx].notes[channel].push_back(n);
                sessionEndIdx = (int)pattern->variations[variationIdx].notes[channel].size() - 1;
                anyCommitted  = true;
            }
            if (anyCommitted && onNotesChanged) onNotesChanged();
        }

        currentRecState = RecState::Idle;
        recording_      = false;
        armedBlink      = false;
        pendingRecNotes_.fill({});
        if (onRecordingStateChanged) onRecordingStateChanged();
        repaint();
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
    struct PianoRollViewport : public juce::Viewport
    {
        std::function<void(const juce::Rectangle<int>&)> onVisibleAreaChanged;

        void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override
        {
            juce::Viewport::visibleAreaChanged(newVisibleArea);
            if (onVisibleAreaChanged)
                onVisibleAreaChanged(newVisibleArea);
        }
    };

    struct KeyboardOverlay : public juce::Component,
                             private juce::Timer
    {
        explicit KeyboardOverlay(PianoRollComponent& owner) : pianoRoll(owner)
        {
            startTimerHz(30);
        }

        void setViewY(int newViewY)
        {
            if (viewY != newViewY)
            {
                viewY = newViewY;
                repaint();
            }
        }

        void paint(juce::Graphics& g) override
        {
            pianoRoll.paintKeyboardOverlay(g, viewY, getHeight());
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.getPosition().x < PianoRollComponent::kRangeBarW)
            {
                pianoRoll.startRangeDrag(e.getPosition().y + viewY);
                return;
            }
            pianoRoll.previewPitchAtVisibleY(e.getPosition().y, viewY);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            pianoRoll.updateRangeDrag(e.getPosition().y + viewY);
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            pianoRoll.stopRangeDrag();
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            pianoRoll.setKeyboardHoverVisibleY(e.getPosition().y, viewY);
        }

        void mouseExit(const juce::MouseEvent&) override
        {
            pianoRoll.clearKeyboardHover();
        }

    private:
        void timerCallback() override
        {
            repaint();
        }

        PianoRollComponent& pianoRoll;
        int viewY = 0;
    };

    struct ContentPane : public juce::Component,
                         private juce::Timer
    {
        PianoRollComponent pianoRoll;
        PianoRollViewport  viewport;
        KeyboardOverlay    keyboardOverlay { pianoRoll };
        juce::ComboBox     snapBox;
        juce::TextButton   recBtn        { "REC" };
        juce::TextButton   selectBtn     { "Sel" };
        juce::Label        kbRangeLabel;
        juce::TextButton   quantizeBtn   { "Q" };
        juce::ComboBox     quantizeBox;
        juce::ComboBox     keyRootBox;
        juce::ComboBox     keyScaleBox;
        juce::ComboBox     chordVoicingBox;
        juce::ComboBox     chordStrumBox;
        juce::ComboBox     progressionBox;
        juce::TextButton   progressionBtn { "Apply Chords" };
        juce::ComboBox     bassPatternBox;
        juce::ComboBox     bassTargetBox;
        juce::TextButton   basslineBtn    { "Generate Bass" };
        juce::TextButton   loadMidiBtn   { "Load MIDI" };
        juce::TextButton   saveMidiBtn   { "Save MIDI" };
        juce::TextButton   clearBtn      { "Clear" };
        juce::TextButton   triggerBtn    { "Trigger" };
        juce::TextButton   nudgeLeftBtn  { "<" };
        juce::TextButton   nudgeRightBtn { ">" };
        juce::TextButton   nudgeUpBtn    { "^" };
        juce::TextButton   nudgeDownBtn  { "v" };
        juce::TextButton   alignBtn      { "Align" };

        std::function<void(const KeySignature&)> onKeySignatureChanged;
        std::function<void()> onImportMidi;
        std::function<void()> onExportMidi;
        std::function<int()> onEnsureBassChannel;
        std::function<void(int)> onBasslineApplied;

        bool blinkState = false;
        PianoRollComponent::ChordVoicing chordVoicing = PianoRollComponent::ChordVoicing::Close;
        float chordStrumBeats = 0.0f;
        PianoRollComponent::BasslinePattern basslinePattern = PianoRollComponent::BasslinePattern::RootNotes;

        ContentPane()
        {
            viewport.setViewedComponent(&pianoRoll, false);
            viewport.setScrollBarsShown(true, true);
            viewport.setScrollBarThickness(8);
            addAndMakeVisible(viewport);
            addAndMakeVisible(keyboardOverlay);
            viewport.onVisibleAreaChanged = [this](const juce::Rectangle<int>& area)
            {
                keyboardOverlay.setViewY(area.getY());
            };

            // ---- Edit snap box (left of REC) --------------------------------
            snapBox.addItem("0.05",  1);
            snapBox.addItem("1/16",  2);
            snapBox.addItem("1/8",   3);
            snapBox.addItem("1/4",   4);
            snapBox.addItem("1/2",   5);
            snapBox.addItem("1 Bar", 6);
            snapBox.setSelectedId(2, juce::dontSendNotification);
            snapBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0f3460));
            snapBox.setColour(juce::ComboBox::textColourId,       juce::Colours::white);
            snapBox.setColour(juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
            snapBox.onChange = [this]
            {
                static const float beats[] = { 0.05f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                const int idx = snapBox.getSelectedId() - 1;
                if (idx >= 0 && idx < 6)
                    pianoRoll.setSnapBeats(beats[idx]);
            };
            addAndMakeVisible(snapBox);

            // ---- REC button -------------------------------------------------
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

            // ---- Select tool toggle ----------------------------------------
            selectBtn.setClickingTogglesState(true);
            selectBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff1a2434));
            selectBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2c6fb2));
            selectBtn.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xff9fb6cf));
            selectBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
            selectBtn.onClick = [this]
            {
                pianoRoll.setSelectToolEnabled(selectBtn.getToggleState());
                pianoRoll.grabKeyboardFocus();
            };
            // Sync button when B/V keys change the tool mode
            pianoRoll.onToolModeChanged = [this](bool selectMode)
            {
                selectBtn.setToggleState(selectMode, juce::dontSendNotification);
            };
            addAndMakeVisible(selectBtn);

            // KB range label
            kbRangeLabel.setColour(juce::Label::textColourId,       juce::Colour(0xffff9500));
            kbRangeLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1a1200));
            kbRangeLabel.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
            kbRangeLabel.setJustificationType(juce::Justification::centred);
            kbRangeLabel.setText("KB: C4", juce::dontSendNotification);
            addAndMakeVisible(kbRangeLabel);
            pianoRoll.onKeyboardRangeChanged = [this](int basePitch)
            {
                const auto& ks = pianoRoll.getKeySignature();
                const juce::String noteName = MusicTheory::noteNameForPitch(basePitch, ks)
                                            + juce::String(basePitch / 12 - 1);
                kbRangeLabel.setText("KB: " + noteName, juce::dontSendNotification);
                keyboardOverlay.repaint();

                // Auto-scroll viewport to keep the range bar visible
                auto [topY, botY] = pianoRoll.getRangeBarContentY();
                const int viewY   = viewport.getViewPositionY();
                const int viewH   = viewport.getViewHeight();
                const int margin  = 24;
                if (topY < viewY + margin)
                    viewport.setViewPosition(viewport.getViewPositionX(),
                                            juce::jmax(0, topY - margin));
                else if (botY > viewY + viewH - margin)
                    viewport.setViewPosition(viewport.getViewPositionX(),
                                            botY - viewH + margin);
            };

            // ---- Trigger toggle (arm-and-wait mode) -------------------------
            triggerBtn.setClickingTogglesState(true);
            triggerBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff1a1a2a));
            triggerBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2255aa));
            triggerBtn.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xff7788aa));
            triggerBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
            triggerBtn.onClick = [this]
            {
                pianoRoll.setTriggerEnabled(triggerBtn.getToggleState());
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(triggerBtn);

            // ---- Nudge left / right -----------------------------------------
            nudgeLeftBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1c2030));
            nudgeLeftBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaabbdd));
            nudgeLeftBtn.onClick = [this]
            {
                // Nudge by current snap grid
                static const float beats[] = { 0.05f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                const int idx = snapBox.getSelectedId() - 1;
                const float step = (idx >= 0 && idx < 6) ? beats[idx] : 0.25f;
                pianoRoll.nudgeActiveNotes(-step);
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(nudgeLeftBtn);

            nudgeRightBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1c2030));
            nudgeRightBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaabbdd));
            nudgeRightBtn.onClick = [this]
            {
                static const float beats[] = { 0.05f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                const int idx = snapBox.getSelectedId() - 1;
                const float step = (idx >= 0 && idx < 6) ? beats[idx] : 0.25f;
                pianoRoll.nudgeActiveNotes(step);
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(nudgeRightBtn);

            nudgeUpBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1c2030));
            nudgeUpBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaabbdd));
            nudgeUpBtn.onClick = [this]
            {
                pianoRoll.nudgeActiveNotesPitch(+1);
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(nudgeUpBtn);

            nudgeDownBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1c2030));
            nudgeDownBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaabbdd));
            nudgeDownBtn.onClick = [this]
            {
                pianoRoll.nudgeActiveNotesPitch(-1);
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(nudgeDownBtn);

            // ---- Align (snap session start to nearest grid unit) ------------
            alignBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff1c2030));
            alignBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaabbdd));
            alignBtn.onClick = [this]
            {
                static const float beats[] = { 0.05f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                const int idx = snapBox.getSelectedId() - 1;
                const float grid = (idx >= 0 && idx < 6) ? beats[idx] : 0.25f;
                pianoRoll.alignSession(grid);
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(alignBtn);

            // ---- Wire PianoRoll state changes → ContentPane UI -------------
            pianoRoll.onRecordingStateChanged = [this]
            {
                const auto state = pianoRoll.getRecState();
                const bool active = (state != PianoRollComponent::RecState::Idle);
                recBtn.setToggleState(active, juce::dontSendNotification);

                if (state == PianoRollComponent::RecState::Armed)
                {
                    startTimer(500);   // blink at 2 Hz
                }
                else
                {
                    stopTimer();
                    blinkState = false;
                    pianoRoll.setArmedBlink(false);
                }

                updateSessionButtons();
            };
            pianoRoll.onSelectionChanged = [this] { updateSessionButtons(); };

            updateSessionButtons();  // initial state

            // ---- Quantize toggle (Q) ----------------------------------------
            quantizeBtn.setClickingTogglesState(true);
            quantizeBtn.setToggleState(true, juce::dontSendNotification);  // ON by default
            quantizeBtn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff1a2a1a));
            quantizeBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff27ae60));
            quantizeBtn.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xff668866));
            quantizeBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
            quantizeBtn.onClick = [this]
            {
                pianoRoll.setQuantize(quantizeBtn.getToggleState());
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(quantizeBtn);

            // ---- Clear all notes button -------------------------------------
            clearBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a1818));
            clearBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcc8888));
            clearBtn.onClick = [this]
            {
                pianoRoll.clearNotes();
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(clearBtn);

            // ---- Quantize grid (1/4 / 1/8 / 1/16 / 1/32) -------------------
            quantizeBox.addItem("1/4",  1);
            quantizeBox.addItem("1/8",  2);
            quantizeBox.addItem("1/16", 3);   // default
            quantizeBox.addItem("1/32", 4);
            quantizeBox.setSelectedId(3, juce::dontSendNotification);
            quantizeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff162016));
            quantizeBox.setColour(juce::ComboBox::textColourId,       juce::Colours::white);
            quantizeBox.setColour(juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
            quantizeBox.onChange = [this]
            {
                static const float grids[] = { 1.0f, 0.5f, 0.25f, 0.125f };
                const int idx = quantizeBox.getSelectedId() - 1;
                if (idx >= 0 && idx < 4)
                    pianoRoll.setQuantizeGrid(grids[idx]);
            };
            addAndMakeVisible(quantizeBox);

            static constexpr const char* noteNames[] =
                { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
            for (int i = 0; i < 12; ++i)
                keyRootBox.addItem(noteNames[i], i + 1);
            keyRootBox.setSelectedId(1, juce::dontSendNotification);
            keyRootBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff26385a));
            keyRootBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
            keyRootBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            keyRootBox.onChange = [this] { applySelectedKeySignature(); };
            addAndMakeVisible(keyRootBox);

            keyScaleBox.addItem("Major", 1);
            keyScaleBox.addItem("Minor", 2);
            keyScaleBox.setSelectedId(1, juce::dontSendNotification);
            keyScaleBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff26385a));
            keyScaleBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
            keyScaleBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            keyScaleBox.onChange = [this] { applySelectedKeySignature(); };
            addAndMakeVisible(keyScaleBox);

            chordVoicingBox.addItem("Close", 1);
            chordVoicingBox.addItem("Open", 2);
            chordVoicingBox.addItem("1st Inv", 3);
            chordVoicingBox.addItem("2nd Inv", 4);
            chordVoicingBox.addItem("Drop2", 5);
            chordVoicingBox.setSelectedId(1, juce::dontSendNotification);
            chordVoicingBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2f405e));
            chordVoicingBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
            chordVoicingBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            chordVoicingBox.onChange = [this]
            {
                chordVoicing = voicingForComboId(chordVoicingBox.getSelectedId());
                updateChordButtonText();
            };
            addAndMakeVisible(chordVoicingBox);

            chordStrumBox.addItem("Straight", 1);
            chordStrumBox.addItem("Light", 2);
            chordStrumBox.addItem("Medium", 3);
            chordStrumBox.addItem("Heavy", 4);
            chordStrumBox.setSelectedId(1, juce::dontSendNotification);
            chordStrumBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2f405e));
            chordStrumBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
            chordStrumBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            chordStrumBox.onChange = [this]
            {
                chordStrumBeats = strumForComboId(chordStrumBox.getSelectedId());
                updateChordButtonText();
            };
            addAndMakeVisible(chordStrumBox);

            progressionBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff27405f));
            progressionBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
            progressionBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible(progressionBox);

            progressionBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff27405f));
            progressionBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff3f7ff));
            progressionBtn.onClick = [this]
            {
                applySelectedChordProgression();
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(progressionBtn);
            updateChordButtonText();

            bassPatternBox.addItem("Root Notes", 1);
            bassPatternBox.addItem("Octave Bounce", 2);
            bassPatternBox.addItem("Pulse 8ths", 3);
            bassPatternBox.addItem("Passing Tone", 4);
            bassPatternBox.setSelectedId(1, juce::dontSendNotification);
            bassPatternBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2d4d2f));
            bassPatternBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
            bassPatternBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            bassPatternBox.onChange = [this]
            {
                basslinePattern = basslinePatternForComboId(bassPatternBox.getSelectedId());
                updateBasslineButtonText();
            };
            addAndMakeVisible(bassPatternBox);

            bassTargetBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff2d4d2f));
            bassTargetBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
            bassTargetBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible(bassTargetBox);

            basslineBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d4d2f));
            basslineBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffeef9ee));
            basslineBtn.onClick = [this]
            {
                applyBasslineGeneration();
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(basslineBtn);
            updateBasslineButtonText();
            refreshProgressionOptions();
            refreshBassTargetOptions();

            loadMidiBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff20324a));
            loadMidiBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd6e4ff));
            loadMidiBtn.onClick = [this]
            {
                if (onImportMidi) onImportMidi();
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(loadMidiBtn);

            saveMidiBtn.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff20324a));
            saveMidiBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd6e4ff));
            saveMidiBtn.onClick = [this]
            {
                if (onExportMidi) onExportMidi();
                pianoRoll.grabKeyboardFocus();
            };
            addAndMakeVisible(saveMidiBtn);
        }

        void setKeySignature(const KeySignature& key)
        {
            keyRootBox.setSelectedId(key.tonic + 1, juce::dontSendNotification);
            keyScaleBox.setSelectedId(key.scale == ScaleType::Minor ? 2 : 1, juce::dontSendNotification);
            pianoRoll.setKeySignature(key);
            refreshProgressionOptions();
            keyboardOverlay.repaint();
        }

        void setPattern(Pattern* pat, int ch, double bpmValue)
        {
            pianoRoll.setPattern(pat, ch, bpmValue);
            refreshProgressionOptions();
            refreshBassTargetOptions();
        }

        void resized() override
        {
            const int rowH = 26;

            // Row 1: tonal helpers
            quantizeBtn.setBounds(4, 1, 28, rowH - 2);
            quantizeBox.setBounds(34, 0, 68, rowH);
            keyRootBox.setBounds(106, 0, 70, rowH);
            keyScaleBox.setBounds(180, 0, 74, rowH);
            chordVoicingBox.setBounds(258, 0, 90, rowH);
            chordStrumBox.setBounds(352, 0, 88, rowH);
            progressionBtn.setBounds(getWidth() - 110, 0, 106, rowH);
            progressionBox.setBounds(444, 0, juce::jmax(120, getWidth() - 444 - 114), rowH);

            // Row 2: right-side (compute rightX so bass section knows its boundary)
            int rightX = getWidth();
            snapBox    .setBounds(rightX - 84, rowH, 84, rowH); rightX -= 88;
            recBtn     .setBounds(rightX - 44, rowH, 44, rowH); rightX -= 48;
            selectBtn  .setBounds(rightX - 42, rowH, 42, rowH); rightX -= 46;
            clearBtn   .setBounds(rightX - 44, rowH, 44, rowH); rightX -= 48;
            saveMidiBtn.setBounds(rightX - 68, rowH, 68, rowH); rightX -= 72;
            loadMidiBtn.setBounds(rightX - 68, rowH, 68, rowH); rightX -= 72;

            // Row 2: left-side fixed
            triggerBtn   .setBounds(4,   rowH, 54, rowH);
            nudgeLeftBtn .setBounds(62,  rowH, 26, rowH);
            nudgeRightBtn.setBounds(92,  rowH, 26, rowH);
            nudgeUpBtn   .setBounds(122, rowH, 26, rowH);
            nudgeDownBtn .setBounds(152, rowH, 26, rowH);
            alignBtn     .setBounds(182, rowH, 42, rowH);
            kbRangeLabel .setBounds(232, rowH, 62, rowH);

            // Row 2: bass section fills the gap between left and right-side buttons
            {
                const int bassLeft  = 298;
                const int bassRight = rightX - 4;
                int bx = bassLeft;
                bassPatternBox.setBounds(bx, rowH, 120, rowH); bx += 124;
                bassTargetBox .setBounds(bx, rowH, 160, rowH); bx += 164;
                basslineBtn   .setBounds(bx, rowH, juce::jmax(80, bassRight - bx), rowH);
            }

            const int keyboardW = pianoRoll.getKeyboardWidth();
            keyboardOverlay.setBounds(0, rowH * 2, keyboardW, getHeight() - rowH * 2);
            viewport.setBounds(keyboardW, rowH * 2, getWidth() - keyboardW, getHeight() - rowH * 2);

            const int cw = juce::jmax(viewport.getWidth(),  pianoRoll.getNeededWidth());
            const int ch = juce::jmax(viewport.getHeight(), pianoRoll.getNeededHeight());
            pianoRoll.setSize(cw, ch);
            if (viewport.getViewPositionX() < keyboardW)
                viewport.setViewPosition(keyboardW, viewport.getViewPositionY());
            keyboardOverlay.setViewY(viewport.getViewPositionY());
        }

        // --- Timer: drives armed blink ---
        void timerCallback() override
        {
            blinkState = !blinkState;
            pianoRoll.setArmedBlink(blinkState);
            updateSessionButtons();
        }

        // Enable/disable controls based on whether there is an editable note target
        void updateSessionButtons()
        {
            const bool hasMoveTarget = pianoRoll.hasMovableNoteTarget();
            const bool hasSession = pianoRoll.hasSession();
            nudgeLeftBtn .setEnabled(hasMoveTarget);
            nudgeRightBtn.setEnabled(hasMoveTarget);
            nudgeUpBtn   .setEnabled(hasMoveTarget);
            nudgeDownBtn .setEnabled(hasMoveTarget);
            alignBtn     .setEnabled(hasSession);
            nudgeLeftBtn .setAlpha(hasMoveTarget ? 1.0f : 0.4f);
            nudgeRightBtn.setAlpha(hasMoveTarget ? 1.0f : 0.4f);
            nudgeUpBtn   .setAlpha(hasMoveTarget ? 1.0f : 0.4f);
            nudgeDownBtn .setAlpha(hasMoveTarget ? 1.0f : 0.4f);
            alignBtn     .setAlpha(hasSession ? 1.0f : 0.4f);
        }

    private:
        void refreshProgressionOptions()
        {
            const auto key = pianoRoll.getKeySignature();
            const auto presets = MusicTheory::getChordProgressionPresets(key.scale);
            const int previousId = progressionBox.getSelectedId();

            progressionBox.clear(juce::dontSendNotification);
            int itemId = 1;
            for (const auto& preset : presets)
                progressionBox.addItem("[" + preset.category + "] "
                                           + preset.name
                                           + " - "
                                           + MusicTheory::chordSymbolsForProgression(key, preset),
                                       itemId++);

            const int clampedId = juce::jlimit(1, juce::jmax(1, (int)presets.size()), previousId > 0 ? previousId : 1);
            progressionBox.setSelectedId(clampedId, juce::dontSendNotification);
        }

        void applySelectedChordProgression()
        {
            const auto presets = MusicTheory::getChordProgressionPresets(pianoRoll.getKeySignature().scale);
            const int selectedId = progressionBox.getSelectedId();
            if (selectedId <= 0 || selectedId > (int)presets.size())
                return;

            if (pianoRoll.fillWithChordProgression(presets[(size_t)(selectedId - 1)],
                                                   chordVoicing,
                                                   chordStrumBeats))
                pianoRoll.grabKeyboardFocus();
        }

        void refreshBassTargetOptions(int preferredChannel = -1)
        {
            auto* pat = pianoRoll.getPatternModel();
            const int currentSelection = bassTargetBox.getSelectedId();
            bassTargetBox.clear(juce::dontSendNotification);
            bassTargetBox.addItem("Auto / Create Bass", 1);

            if (pat == nullptr)
            {
                bassTargetBox.setSelectedId(1, juce::dontSendNotification);
                return;
            }

            for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
            {
                if (ch == pianoRoll.getChannelIndex())
                    continue;
                if (pat->channelTypes[ch] != ChannelType::Melodic)
                    continue;

                const auto name = pat->channelNames[ch].isNotEmpty()
                    ? pat->channelNames[ch]
                    : ("Channel " + juce::String(ch + 1));
                bassTargetBox.addItem(name, 100 + ch);
            }

            if (preferredChannel >= 0)
                bassTargetBox.setSelectedId(100 + preferredChannel, juce::dontSendNotification);
            else if (currentSelection > 0 && bassTargetBox.indexOfItemId(currentSelection) >= 0)
                bassTargetBox.setSelectedId(currentSelection, juce::dontSendNotification);
            else
                bassTargetBox.setSelectedId(1, juce::dontSendNotification);
        }

        void applyBasslineGeneration()
        {
            int targetChannel = -1;
            const int selection = bassTargetBox.getSelectedId();
            if (selection == 1)
            {
                if (onEnsureBassChannel)
                    targetChannel = onEnsureBassChannel();
            }
            else if (selection >= 100)
            {
                targetChannel = selection - 100;
            }

            if (targetChannel < 0)
                return;

            refreshBassTargetOptions(targetChannel);

            if (pianoRoll.generateBasslineToChannel(targetChannel, basslinePattern))
            {
                if (onBasslineApplied)
                    onBasslineApplied(targetChannel);
                pianoRoll.grabKeyboardFocus();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Bassline Generation",
                    "Create or keep chord notes in the current piano roll first, then generate the bassline.");
            }
        }

        static PianoRollComponent::ChordVoicing voicingForComboId(int itemId)
        {
            switch (itemId)
            {
                case 2: return PianoRollComponent::ChordVoicing::Open;
                case 3: return PianoRollComponent::ChordVoicing::FirstInversion;
                case 4: return PianoRollComponent::ChordVoicing::SecondInversion;
                case 5: return PianoRollComponent::ChordVoicing::Drop2;
                default:   return PianoRollComponent::ChordVoicing::Close;
            }
        }

        static float strumForComboId(int itemId)
        {
            switch (itemId)
            {
                case 2: return 0.02f;
                case 3: return 0.05f;
                case 4: return 0.09f;
                default:   return 0.0f;
            }
        }

        juce::String chordVoicingLabel() const
        {
            switch (chordVoicing)
            {
                case PianoRollComponent::ChordVoicing::Open:            return "Open";
                case PianoRollComponent::ChordVoicing::FirstInversion:  return "1st Inv";
                case PianoRollComponent::ChordVoicing::SecondInversion: return "2nd Inv";
                case PianoRollComponent::ChordVoicing::Drop2:           return "Drop2";
                default:                                                return "Close";
            }
        }

        juce::String chordStrumLabel() const
        {
            if (chordStrumBeats >= 0.085f) return "Heavy";
            if (chordStrumBeats >= 0.045f) return "Medium";
            if (chordStrumBeats >= 0.015f) return "Light";
            return "Straight";
        }

        static PianoRollComponent::BasslinePattern basslinePatternForComboId(int itemId)
        {
            switch (itemId)
            {
                case 2: return PianoRollComponent::BasslinePattern::OctaveBounce;
                case 3: return PianoRollComponent::BasslinePattern::Pulse8ths;
                case 4: return PianoRollComponent::BasslinePattern::PassingTone;
                default:   return PianoRollComponent::BasslinePattern::RootNotes;
            }
        }

        juce::String basslinePatternLabel() const
        {
            switch (basslinePattern)
            {
                case PianoRollComponent::BasslinePattern::OctaveBounce: return "Bounce";
                case PianoRollComponent::BasslinePattern::Pulse8ths:    return "Pulse";
                case PianoRollComponent::BasslinePattern::PassingTone:  return "Passing";
                default:                                               return "Root";
            }
        }

        void updateChordButtonText()
        {
            progressionBtn.setButtonText("Apply Chords");
            progressionBtn.setTooltip("Apply the selected chord progression using voicing "
                                      + chordVoicingLabel() + " and strum " + chordStrumLabel() + ".");
        }

        void updateBasslineButtonText()
        {
            basslineBtn.setButtonText("Generate Bass");
            basslineBtn.setTooltip("Generate a bassline using the " + basslinePatternLabel() + " pattern.");
        }

        void applySelectedKeySignature()
        {
            KeySignature key;
            key.tonic = juce::jlimit(0, 11, keyRootBox.getSelectedId() - 1);
            key.scale = keyScaleBox.getSelectedId() == 2 ? ScaleType::Minor : ScaleType::Major;
            pianoRoll.setKeySignature(key);
            refreshProgressionOptions();
            keyboardOverlay.repaint();
            if (onKeySignatureChanged)
                onKeySignatureChanged(key);
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
        setResizeLimits(1100, 440, 4000, 4000);
        setSize(1400, 560);
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollWindow)
};
