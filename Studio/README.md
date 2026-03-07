# Studio — DAW (FL Studio-style)

Built with JUCE (C++). Professional DAW development in progress, milestone-driven.

---

## Layout Overview

```
┌─────────────────────────────────────────────────────┐
│                    Toolbar                          │  ← Play / Stop / BPM / Mode
├─────────────────────────────────────────────────────┤
│                   Playlist                          │  ← Song timeline (40% height)
├─────────────────────────────────────────────────────┤
│                 Channel Rack                        │  ← Step sequencer (scrollable)
└─────────────────────────────────────────────────────┘
```

---

## Toolbar (two rows)

### Row 1 — Transport

### Row 2 — Pattern Management

| Control | How to use |
|---|---|
| **PATTERN dropdown** | Selects the active pattern. The Channel Rack immediately loads the selected pattern's step grid. |
| **+ button** | Creates a new empty 16-step pattern and selects it. |
| **= button** | Duplicates the active pattern (copies all steps) and selects the copy. |
| **- button** | Deletes the active pattern. Cannot delete if only one pattern exists. |

---

## Toolbar (transport)

| Control | How to use |
|---|---|
| **Play** | Starts playback. In Pattern mode, runs the step sequencer. In Song mode, plays the playlist timeline from bar 0. |
| **Stop** | Stops playback and resets the playhead to the beginning. |
| **Rec** | Button exists — not yet functional (planned for M7: Audio Recording). |
| **BPM knob** | Drag up/down to change tempo (range: 60–200 BPM). Change takes effect immediately even during playback. |
| **Pattern / Song dropdown** | Switches between the two play modes (see below). |

### Play Modes

**Pattern mode**
The step sequencer loops continuously through the Channel Rack pattern.
Use this when building and auditioning a beat.

**Song mode**
The Playlist timeline drives playback. Clips on the timeline trigger their associated pattern at the correct bar position. The red playhead moves across the Playlist.
Use this when arranging a full song.

---

## Channel Rack

The step sequencer. Each row is one channel (e.g. Kick, Snare, HiHat).

### Step Grid

| Action | Result |
|---|---|
| **Click a step button** | Toggles that step on (lit blue) or off (dark). Active steps will trigger the channel's sample when the sequencer reaches them. |
| **16 / 32 / 64 Steps dropdown** (bottom-left) | Changes the number of steps in the pattern. All channels share this length. |
| **+ Add Channel button** (bottom-left) | Adds a new empty channel row. |

### Per-Channel Controls (inside the label area)

Each channel row has the following controls in the left 200px label area:

| Control | Location | Range | What it does |
|---|---|---|---|
| **Volume slider** (blue, horizontal, top row) | Right of channel name | 0.0 – 1.0 | Sets playback volume for that channel. Uses constant-power law. Default: 0.8 |
| **Pan slider** (orange, horizontal, bottom row) | Right of channel name | -1.0 (L) – +1.0 (R) | Pans the channel left or right in the stereo field. Default: center (0.0) |
| **Pitch slider** (green, vertical) | Centre column | -24 to +24 semitones | Shifts the sample pitch up or down. Uses linear interpolation resampling. Default: 0 (no shift) |
| **M button** (Mute) | Far right, top | on/off | Silences this channel. Button turns red when active. |
| **S button** (Solo) | Far right, bottom | on/off | Solos this channel — all others are muted. Button turns orange when active. |

### Loading Samples

**Drag & drop** an audio file (WAV, AIFF, MP3) from Finder onto a channel row.
The channel highlights green as you hover over it.
On drop, the sample name appears in the label and the sample is loaded into the audio engine immediately.

### Renaming a Channel

**Double-click** the channel name text (left 90px of the label area).
A dialog appears — type the new name and press OK.

### Scrolling

If you add more channels than fit in the visible area, the Channel Rack scrolls vertically using the scrollbar on the right edge.

---

## Playlist

The song timeline. Horizontal axis = bars. Each row = one track.

### Clips

| Action | Result |
|---|---|
| **Double-click on empty track space** | Creates a new 4-bar clip. Automatically assigned to the currently active pattern. |
| **Drag a clip left/right** | Moves the clip horizontally. Snaps to whole bars. |
| **Drag a clip up/down** | Moves the clip to a different track. Snaps to track rows. |
| **Drag the right edge of a clip** | Resizes the clip length. The last 10px of the right edge is the resize handle (shown as a bright stripe). Minimum 1 bar. |
| **Double-click on an existing clip** | Renames the clip via a dialog. |
| **Right-click on a clip** | Opens context menu: Rename / Assign Pattern / Delete. |

### Playhead

The red vertical line shows the current playback position in Song mode.
It updates 30 times per second.

### What clip editing is NOT yet implemented

The following are planned but not yet built:

All clip editing features are now implemented — see the full table above.

---

## Audio Engine Internals (for reference)

| Feature | Status |
|---|---|
| Sample playback (WAV / AIFF / MP3) | Done |
| Per-channel Volume, Pan | Done (M1.1) |
| Per-channel Pitch shift (resampling) | Done (M1.2) |
| Attack / Release envelope per channel | Engine-ready, no UI yet (M1.3) |
| Mute / Solo per channel | Done |
| 16/32/64-step pattern sequencer | Done |
| Song mode timeline playback | Done |
| Save / Load project | Not yet (M4) |
| Mixer (per-track faders, FX) | Not yet (M5) |
| Piano Roll (melodic notes) | Not yet (M3) |
| VST / AU plugin hosting | Not yet (M8) |
| Audio recording | Not yet (M7) |
| Export to WAV / MP3 | Not yet (M10) |
| Undo / Redo | Not yet (M6) |

---

## Known Limitations

- **No save/load.** All work is lost when the app closes. (M4 is the next priority after M2.)
- **Only 4 tracks** in the Playlist. Track management is planned for M11.
- **Only 1 pattern.** Multiple pattern support is M2.
- **Record button** does nothing yet.
- The **Pitch slider** uses linear interpolation resampling — good quality for ±12 semitones, slightly less accurate at extreme values. A better resampler (sinc / Lanczos) will replace it later.

---

## Milestone Progress

| # | Name | Status |
|---|---|---|
| M1 | Channel Rack Depth (Vol, Pan, Pitch, Envelope, Scroll, Rename) | **Done** |
| M2 | Multiple Patterns & Clip Editing | **Done** |
| M3 | Piano Roll | Planned |
| M4 | Save / Load | **Next** |
| M5 | Mixer | Planned |
| M6 | Undo / Redo | Planned |
| M7 | Audio Recording | Planned |
| M8 | VST / AU Plugin Hosting | Planned |
| M9 | Automation | Planned |
| M10 | Export / Render | Planned |
| M11 | UI Polish & Track Management | Planned |
