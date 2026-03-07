# Studio DAW

A professional digital audio workstation built in C++ using the JUCE framework.
Goal: feature parity with FL Studio, with a focus on being even more intuitive and user-friendly.

---

## Layout

```
┌──────────────────────────────────────────────────────────────┐
│  Toolbar Row 1 — Transport: Play / Stop / Rec / BPM / Mode   │
│  Toolbar Row 2 — Patterns / File / Export                    │
├──────────────────────────────────────────────────────────────┤
│  Playlist  (scrollable — bars × tracks timeline)             │
├──────────────────────────────────────────────────────────────┤
│  Channel Rack  (scrollable step sequencer)                   │
└──────────────────────────────────────────────────────────────┘
```

---

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Space` | Play / Stop |
| `Cmd+Z` | Undo |
| `Cmd+Shift+Z` | Redo |
| `Cmd+S` | Save |
| `Cmd+N` | New project |
| `Cmd+O` | Open project |

---

## Toolbar

### Row 1 — Transport

| Control | How to use |
|---|---|
| **Play** | Starts playback. Pattern mode loops the active pattern; Song mode plays the full timeline from bar 0. |
| **Stop** | Stops playback and resets the playhead. |
| **Rec** | Planned — Audio Recording (M7). |
| **BPM slider** | Drag to change tempo (60–200 BPM). Takes effect immediately, even during playback. Default: 70. |
| **Pattern / Song** | Switches play mode. |

### Row 2 — Pattern Management

| Control | How to use |
|---|---|
| **Pattern dropdown** | Selects the active pattern. The Channel Rack immediately loads its steps. |
| **+ button** | Creates a new empty 16-step pattern. |
| **= button** | Duplicates the active pattern (copies all steps). |
| **− button** | Deletes the active pattern. Disabled when only one pattern exists. |
| **Rename button** | Renames the active pattern via a dialog. |

### Row 2 — File & Export

| Control | How to use |
|---|---|
| **New** | Creates a new empty project. Warns if there are unsaved changes. |
| **Open** | Opens a `.studioproj` file from disk. |
| **Save** | Saves the current project. Prompts for a filename on first save. |
| **Save As** | Saves to a new file. |
| **Export WAV** | Renders the project to a WAV file (24-bit stereo, 44.1 kHz). In Pattern mode, exports 4 loops of the active pattern. In Song mode, exports the full timeline. |

The title bar shows the project name with `*` when there are unsaved changes.

---

## Channel Rack

The step sequencer. Each row is one instrument channel (Kick, Snare, HiHat, etc.).

### Step Grid

| Action | Result |
|---|---|
| **Click a step** | Toggles that step on (lit) or off. Active steps fire the channel's sample. |
| **Cmd+Z** | Undoes the last step toggle. |
| **Step count slider** (bottom-left) | Sets the pattern length from 1 to 64 steps in increments of 1. Click +/− or type directly. |
| **+ Add Channel** (bottom-left) | Adds a new empty channel row. |

### Per-Channel Controls

| Control | Location | Range | What it does |
|---|---|---|---|
| **Volume** (blue slider, top row) | Right of channel name | 0.0 – 1.0 | Playback volume. Constant-power law. |
| **Pan** (orange slider, bottom row) | Right of channel name | −1.0 (L) – +1.0 (R) | Stereo position. |
| **Pitch** (green vertical slider) | Centre column | −24 to +24 semitones | Pitch-shifts the sample via linear interpolation resampling. |
| **M** (Mute) | Far right, top | toggle | Silences this channel. Turns red when active. |
| **S** (Solo) | Far right, bottom | toggle | Solos this channel. Turns orange when active. |

### Loading Samples

Drag & drop any **WAV, AIFF, or MP3** file from Finder onto a channel row.
The row highlights green on hover. On drop the sample loads immediately and the filename appears in the label.

### Renaming a Channel

Double-click the channel name text (left 90 px of the label area) to rename via a dialog.

### Scrolling

The Channel Rack scrolls vertically when there are more channels than fit on screen.

---

## Playlist

The song arrangement timeline. Horizontal = bars. Vertical = tracks (unlimited).
Both axes scroll independently.

### Snap Grid

The **Snap** dropdown (top-right of the playlist area) controls how clips snap when you create, move, or resize them:

| Setting | Snap unit |
|---|---|
| 1 Bar | Whole bars (default) |
| 1/2 Bar | Half bars (2 beats) |
| 1/4 Bar | Quarter bars (1 beat) |
| Free | No snap — pixel-level precision |

Sub-bar guide lines appear on the ruler when a finer snap is selected.

### Working with Clips

| Action | Result |
|---|---|
| **Double-click empty space** | Creates a new 4-bar clip assigned to the active pattern. Snaps to current grid. |
| **Drag clip left / right** | Moves clip horizontally. Snaps to grid. |
| **Drag clip up / down** | Moves clip to a different track. |
| **Drag right edge of clip** | Resizes clip length. Minimum size = 1 snap unit. The resize handle is the bright stripe on the right edge. |
| **Double-click existing clip** | Renames the clip. |
| **Right-click clip** | Opens context menu: Rename / Assign Pattern / Delete. |
| **Cmd+Z** | Undoes the last clip add, delete, move, or resize. |

Each clip shows a **mini step-grid preview** in its lower section — three rows of dots representing the first three channels of the assigned pattern.

### Managing Tracks

**Right-click the track label** (left 80 px of any track row) to open the track menu:

| Option | Action |
|---|---|
| **Rename Track** | Opens a rename dialog for that track. |
| **Add Track Below** | Inserts a new empty track below the current one. |
| **Delete Track** | Removes the track and all clips on it. Disabled when only one track exists. |

Tracks are saved and restored with the project file. The three dots (···) on each label hint at the right-click menu.

### Playhead

The red vertical line shows the current playback position in Song mode. Updates at 30 Hz.

---

## Built-in Synthesizer (M13)

Right-click any channel in the Channel Rack → **Open Synth Editor** to open the synth parameter window.

### Enabling the Synth

Toggle **Enable Synth** in the editor. When enabled, the channel renders audio from the built-in synthesizer instead of a loaded sample file.

- For **Drum channels**: the synth fires on each active step at a fixed pitch (C4).
- For **Melodic channels**: the synth fires on Piano Roll NoteEvents with the correct pitch and duration.

### Synth Parameters

| Parameter | Description |
|---|---|
| **Waveform** | Oscillator shape: Sine / Saw / Square / Triangle |
| **Attack** | Envelope attack time (ms) |
| **Decay** | Envelope decay time (ms) |
| **Sustain** | Envelope sustain level (0 – 1) |
| **Release** | Envelope release time (ms) |
| **Cutoff** | Low-pass filter cutoff frequency (Hz) |
| **Resonance** | Filter resonance / Q (0 – 1) |
| **LFO Rate** | LFO oscillation speed (Hz) |
| **LFO Depth** | LFO modulation amount (0 = off) |
| **LFO Target** | What the LFO modulates: Cutoff or Pitch |

The synth is 8-voice polyphonic with round-robin voice stealing.

---

## Built-in FX (M14)

Open the **Mixer** panel, then click the purple **FX** button on any mixer track strip. This opens the FX chain editor for that track.

### FX Chain Order

```
Compressor → Delay → Reverb
```

Each effect can be enabled/disabled independently.

### Compressor

| Parameter | Description |
|---|---|
| **Threshold (dB)** | Level above which gain reduction kicks in |
| **Ratio** | Compression ratio (1 = bypass, 20 = hard limit) |
| **Attack (ms)** | How fast the compressor responds to transients |
| **Release (ms)** | How fast the compressor recovers |

### Delay

| Parameter | Description |
|---|---|
| **Time (beats)** | Delay time as a beat fraction (0.25 = 16th, 0.5 = 8th, 1.0 = quarter) |
| **Feedback** | How much of the delayed signal feeds back (0 – 0.95) |
| **Mix** | Wet/dry blend (0 = dry, 1 = full wet) |

The delay is BPM-synced — it automatically adjusts to the current project BPM.

### Reverb

| Parameter | Description |
|---|---|
| **Room Size** | Size of the simulated space (0 – 1) |
| **Damping** | High-frequency absorption (0 = bright, 1 = dark) |
| **Wet Mix** | Reverb return level (0 = dry, 1 = fully wet) |
| **Width** | Stereo width of the reverb tail (0 = mono, 1 = full stereo) |

---

## Save / Load

Projects are saved as `.studioproj` files (XML format). All patterns, steps, playlist clips, and BPM are saved.

---

## Milestone Progress

| # | Feature | Status |
|---|---|---|
| M1 | Channel Rack — Vol, Pan, Pitch, Mute/Solo, Scroll, Rename | **Done** |
| M2 | Multiple Patterns, Clip Editing, Pattern-to-Playlist link | **Done** |
| M3 | Piano Roll (melodic note editor) | **Done** |
| M4 | Save / Load (XML project file) | **Done** |
| M5 | Mixer (per-track faders, sends, FX slots) | **Done** |
| M6 | Undo / Redo | **Done** |
| M7 | Audio Recording (mic / line-in → clip) | Planned |
| M8 | VST / AU Plugin Hosting | Planned |
| M9 | Automation (parameter curves on timeline) | Planned |
| M10 | Export / Render to WAV | **Done** |
| M11 | UI Polish, Zoom, Track Management, Pattern Preview | **Partial** (M11.3 + M11.5 done) |
| M12 | MIDI Input / Output | Planned |
| M13 | Built-in Instruments (synth, sampler) | **Done** |
| M14 | Built-in FX (EQ, Compressor, Reverb, Delay) | **Partial** (Compressor, Delay, Reverb done — no EQ yet) |
| M15 | Sample Browser / Library Panel | Planned |
| M16 | Auto-save, Recent Files, Project Templates | Planned |

---

## Tech Stack

- **Language:** C++17
- **Framework:** JUCE
- **Audio:** 44,100 Hz sample rate, 512-sample buffer
- **Project format:** XML (`.studioproj`)
- **Export format:** WAV (24-bit stereo)
