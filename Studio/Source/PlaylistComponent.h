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
    double getPlayheadBar() const { return playheadBar; }
    int getTrackHeaderWidth() const { return trackHeaderWidth; }

    void setSnapDivisor(int d) { snapDivisor = d; repaint(); }

    int getNeededWidth()  const { return trackHeaderWidth + (int)(juce::jmax(200.0, getTotalBars() + 8.0)) * barWidth; }
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
        if (nc.name.isEmpty())
            nc.name = "Clip " + juce::String(newId);
        if (onClipAdded) onClipAdded(nc);
        repaint();
    }

    // M11 — horizontal zoom (pixels per bar)
    void setBarWidth(int w) { barWidth = juce::jlimit(20, 256, w); clearWaveformCache(); repaint(); }
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
    std::function<void(int clipId, int patternId)>                              onClipPatternChanged;
    std::function<void(int clipId, juce::String oldName, juce::String newName)> onClipRenamed;

    // Double-click on a clip with an assigned pattern → navigate to that pattern
    std::function<void(int patternId)>                                          onNavigateToPattern;

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
    static constexpr int trackHeight   = 56;
    static constexpr int trackGap      = 4;
    static constexpr int trackHeaderWidth = 80;
    static constexpr int resizeHotspot = 10; // px from right edge = resize handle
    static constexpr int autoLaneHeight = 60; // M9 — height of each automation lane

    int barWidth = 64;  // M11 zoom: pixels per bar (variable)

    // ---------------------------------------------------------------------------
    // Clip waveform cache — built from step/note data, invalidated on zoom change
    // ---------------------------------------------------------------------------
    struct WavePoint { float min; float max; };
    using WaveCache = std::vector<WavePoint>;

    // Key: (patternId, variationIdx, pixelWidth, bpmRounded)
    mutable std::map<std::tuple<int,int,int,int>, WaveCache> waveformCache_;

    void clearWaveformCache() { waveformCache_.clear(); }

    // Builds a TRUE min/max waveform from pattern data.
    //
    // Each voice is synthesised as (ADSR envelope × sin oscillation) using the
    // channel's actual SynthParams ADSR values so that attack, decay, sustain, and
    // release are all visually reflected.  Notes extend into a release tail after
    // note-off.  All voices are additively mixed, peak-normalised, then downsampled
    // to pixel width via min/max — the same technique real DAWs use for audio clips.
    static WaveCache buildPatternWave(const Pattern& pat, int vi, int width,
                                      float clipBeats, double bpm = 120.0)
    {
        if (width <= 0) return {};
        WaveCache out(width, {0.0f, 0.0f});

        static constexpr int kOS = 8;
        const int N = width * kOS;
        std::vector<float> samples(N, 0.0f);

        const float spb          = (float)N / clipBeats;   // oversampled-buffer samples per beat
        const float patternBeats = juce::jmax(0.25f, (float)pat.stepCount * 0.25f);
        const float twoPi        = juce::MathConstants<float>::twoPi;

        // ms → oversampled-buffer samples
        // spb beats/s * (bpm/60 beats/s)^-1 => 1 beat = spb*(60/bpm) buf-samples
        // 1 ms = spb * (bpm/60) / 1000 buf-samples
        const float spm = spb * (float)(bpm / 60.0) / 1000.0f;

        for (int ch = 0; ch < Pattern::kMaxChannels; ++ch)
        {
            // Use the channel's SynthParams ADSR.  Fall back to musical defaults
            // when synth is not enabled (drums still have a characteristic shape).
            const SynthParams& sp = pat.synthParams[(size_t)ch];
            const float atkS = juce::jmax(1.0f, sp.attack  * spm);
            const float decS = juce::jmax(1.0f, sp.decay   * spm);
            const float sus  = juce::jlimit(0.0f, 1.0f, sp.sustain);
            const float relS = juce::jmax(1.0f, sp.release * spm);

            // ---- Step triggers (drum / rhythmic channels) -----------------------
            for (float lb = 0.0f; lb < clipBeats; lb += patternBeats)
            {
                for (int s = 0; s < pat.stepCount; ++s)
                {
                    if (!pat.variations[vi].steps[ch][s]) continue;
                    const float beat = lb + s * 0.25f;
                    if (beat >= clipBeats) break;

                    const int sStart  = juce::jlimit(0, N - 1, (int)(beat * spb));
                    // Hold for one 16th-note, then release
                    const int holdEnd = juce::jmin(N, sStart + (int)(0.25f * spb));
                    const int fullEnd = juce::jmin(N, holdEnd + (int)relS);

                    // Oscillation: 5 cycles over attack+decay (percussive thump look)
                    const float burstLen = juce::jmax(1.0f, atkS + decS);
                    const float freq = twoPi * 5.0f / burstLen;

                    for (int i = sStart; i < fullEnd; ++i)
                    {
                        const float ti = (float)(i - sStart);
                        float env;
                        if (ti < atkS)
                        {
                            env = ti / atkS;                            // attack
                        }
                        else if (ti < atkS + decS)
                        {
                            const float td = (ti - atkS) / decS;
                            env = 1.0f - td * (1.0f - sus);            // decay → sustain
                        }
                        else if (i < holdEnd)
                        {
                            env = sus;                                  // sustain (held)
                        }
                        else
                        {
                            // Release after note-off: smooth exponential tail
                            const float tr = (float)(i - holdEnd) / relS;
                            env = sus * std::exp(-tr * 4.5f);
                        }
                        samples[i] += env * std::sin(freq * ti);
                    }
                }
            }

            // ---- Note triggers (melodic channels) --------------------------------
            for (const auto& note : pat.variations[vi].notes[ch])
            {
                const float phase = std::fmod(note.startBeat, patternBeats);

                // Pitch → oscillation cycles (more cycles = higher pitch feel)
                const float pitchRatio = std::pow(2.0f, (note.pitch - 60) * (1.0f / 12.0f));
                const float cycles     = juce::jlimit(3.0f, 14.0f, 4.5f * pitchRatio);

                for (float lb = 0.0f; lb < clipBeats; lb += patternBeats)
                {
                    const float beatStart = lb + phase;
                    if (beatStart >= clipBeats) break;

                    const int s0      = juce::jlimit(0, N, (int)(beatStart * spb));
                    const float noteEndBeat = beatStart + note.lengthBeats;
                    const int noteEnd = juce::jlimit(0, N, (int)(noteEndBeat * spb));
                    // Release tail extends beyond note-off
                    const int fullEnd = juce::jmin(N, noteEnd + (int)relS);

                    const float noteDur = (float)juce::jmax(1, noteEnd - s0);
                    const float freq    = twoPi * cycles / noteDur;

                    for (int i = s0; i < fullEnd; ++i)
                    {
                        const float ti = (float)(i - s0);
                        float env;
                        if (ti < atkS)
                        {
                            env = ti / atkS;                            // attack
                        }
                        else if (ti < atkS + decS)
                        {
                            const float td = (ti - atkS) / decS;
                            env = 1.0f - td * (1.0f - sus);            // decay → sustain
                        }
                        else if (i < noteEnd)
                        {
                            env = sus;                                  // sustain
                        }
                        else
                        {
                            // Release after note-off
                            const float tr = (float)(i - noteEnd) / relS;
                            env = sus * std::exp(-tr * 4.5f);
                        }
                        samples[i] += env * note.velocity * std::sin(freq * ti);
                    }
                }
            }
        }

        // Peak-normalise so the loudest sample = ±1
        float peak = 0.0f;
        for (float v : samples) peak = std::max(peak, std::abs(v));
        if (peak > 1e-4f)
        {
            const float inv = 1.0f / peak;
            for (float& v : samples) v *= inv;
        }

        // Downsample: per-pixel min/max
        for (int px = 0; px < width; ++px)
        {
            float mn = 0.0f, mx = 0.0f;
            const int s0 = px * kOS;
            const int s1 = juce::jmin(N, s0 + kOS);
            for (int i = s0; i < s1; ++i)
            {
                if (samples[i] < mn) mn = samples[i];
                if (samples[i] > mx) mx = samples[i];
            }
            out[px] = { mn, mx };
        }
        return out;
    }

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
                const float x = trackHeaderWidth + (bar + (float)sub / snapDivisor) * barWidth;
                g.drawLine(x, (float)headerHeight, x, (float)getHeight(), 0.5f);
            }
    }

    for (int bar = 0; bar < totalBars; ++bar)
    {
        const int x = trackHeaderWidth + bar * barWidth;
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
        g.fillRect(0, trackY, trackHeaderWidth, trackHeight);
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

        const int x  = trackHeaderWidth + (int)(clip.startBar  * barWidth);
        const int w  = (int)(clip.lengthBars * barWidth) - 2;
        const int ty = headerHeight + clip.trackIndex * (trackHeight + trackGap) + 3;
        const int h  = trackHeight - 6;

        const bool hasPattern = clip.patternId > 0;
        const juce::Colour fill = hasPattern
            ? palette[(clip.patternId - 1) % paletteSize]
            : juce::Colour(0xff3a3a46);
        const juce::Colour border = hasPattern
            ? fill.brighter(0.4f)
            : juce::Colour(0xff9090a8);

        g.setColour(fill.withAlpha(clip.id == draggingClipId ? 0.6f : 0.85f));
        g.fillRoundedRectangle((float)x + 1, (float)ty, (float)w, (float)h, 4.0f);

        g.setColour(border);
        g.drawRoundedRectangle((float)x + 1, (float)ty, (float)w, (float)h, 4.0f, 1.3f);

        // Resize handle stripe
        g.setColour(border.withAlpha(0.5f));
        g.fillRect(x + w - 4, ty + 2, 4, h - 4);

        // M11.5 — mini note + step preview inside the clip
        if (project != nullptr && hasPattern && w > 20)
        {
            const Pattern* pat = nullptr;
            for (const auto& p : project->patterns)
                if (p.id == clip.patternId) { pat = &p; break; }

            if (pat != nullptr && pat->stepCount > 0)
            {
                const int   previewX  = x + 4;
                const int   previewW  = w - 8;
                const float clipBeats = juce::jmax(0.25f, clip.lengthBars * 4.0f);
                const int   vi        = juce::jlimit(0, Pattern::kMaxVariations - 1, clip.variationIdx);

                // ---- Unified waveform — full clip body below title --------------
                // All voices (steps + notes, all channels) are additively combined
                // into a single normalised min/max waveform.
                {
                    const double bpm = (project != nullptr) ? project->bpm : 120.0;
                    const int bpmRounded = (int)std::round(bpm);
                    const auto cacheKey = std::make_tuple(clip.patternId, vi, previewW, bpmRounded);
                    auto it = waveformCache_.find(cacheKey);
                    if (it == waveformCache_.end())
                    {
                        waveformCache_[cacheKey] = buildPatternWave(*pat, vi, previewW, clipBeats, bpm);
                        it = waveformCache_.find(cacheKey);
                    }
                    const auto& wave = it->second;

                    // Waveform area: full clip body, below the title row
                    static constexpr int kTitleH = 14;  // height reserved for clip name
                    const int   waveY    = ty + kTitleH + 2;
                    const int   waveBot  = ty + h - 3;
                    const int   waveH    = juce::jmax(4, waveBot - waveY);
                    const float centerY  = (float)waveY + (float)waveH * 0.5f;
                    const float halfH    = (float)waveH * 0.5f - 1.0f;

                    // === Centre line (midline) ===
                    // Visible axis: always drawn, indicates ±0 baseline
                    g.setColour(fill.brighter(0.5f).withAlpha(0.40f));
                    g.drawHorizontalLine((int)std::round(centerY),
                                        (float)previewX, (float)(previewX + previewW));

                    // === Waveform — true min/max per-pixel rendering ===
                    // wave[px].min is negative (below centre), .max is positive (above centre).
                    // y1 maps .max above centerY, y2 maps .min below centerY.
                    for (int px = 0; px < previewW && px < (int)wave.size(); ++px)
                    {
                        const float wMax = wave[px].max;  // 0..+1
                        const float wMin = wave[px].min;  // -1..0

                        // Peak-to-peak half-amplitude for brightness decisions
                        const float amp = (wMax - wMin) * 0.5f;  // 0..1
                        if (amp < 0.012f) continue;

                        // Map to pixel space: positive values go UP, negative DOWN
                        const float y1 = centerY - wMax * halfH;   // top of line
                        const float y2 = centerY - wMin * halfH;   // bottom of line
                        const float fx = (float)(previewX + px);

                        // Layer 1 — outer glow (strong transients only)
                        if (amp > 0.38f)
                        {
                            const float ga = (amp - 0.38f) / 0.62f * 0.20f;
                            g.setColour(fill.brighter(2.6f).withAlpha(ga));
                            g.fillRect(fx - 0.5f, y1 - 1.0f, 2.0f, (y2 - y1) + 2.0f);
                        }

                        // Layer 2 — mid halo body
                        if (amp > 0.16f)
                        {
                            const float ha = juce::jmap(amp, 0.16f, 1.0f, 0.09f, 0.26f);
                            g.setColour(fill.brighter(1.9f).withAlpha(ha));
                            g.fillRect(fx - 0.5f, y1, 2.0f, y2 - y1);
                        }

                        // Layer 3 — core line: brightness & opacity scale with amplitude
                        const float coreAlpha  = 0.55f + amp * 0.45f;
                        const float brightness = 0.55f + amp * 1.30f;
                        g.setColour(fill.brighter(brightness).withAlpha(coreAlpha));
                        g.drawVerticalLine(previewX + px, y1, y2);
                    }

                    // Loop repeat markers (subtle vertical guides when clip > pattern length)
                    const float patternBeats = juce::jmax(0.25f, (float)pat->stepCount * 0.25f);
                    if (clipBeats > patternBeats + 0.01f)
                    {
                        g.setColour(juce::Colours::white.withAlpha(0.08f));
                        const float pxPerBeat = (float)previewW / clipBeats;
                        for (float lb = patternBeats; lb < clipBeats; lb += patternBeats)
                        {
                            const int lx = previewX + (int)std::round(lb * pxPerBeat);
                            g.drawVerticalLine(lx, (float)waveY, (float)waveBot);
                        }
                    }
                }
            }
        }

        juce::String assignedPatternName;
        if (project != nullptr && hasPattern)
        {
            for (const auto& pattern : project->patterns)
            {
                if (pattern.id == clip.patternId)
                {
                    assignedPatternName = pattern.name;
                    break;
                }
            }
        }

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        juce::String clipLabel;
        if (hasPattern)
        {
            const auto clipName = clip.name.isNotEmpty() ? clip.name : "Clip " + juce::String(clip.id);
            clipLabel = assignedPatternName.isNotEmpty()
                ? clipName + "  [" + assignedPatternName + "]"
                : clipName;
        }
        else
        {
            clipLabel = clip.name.isNotEmpty() ? clip.name + " (Unassigned)" : "Unassigned Clip";
        }
        g.drawText(clipLabel, x + 6, ty + 1, w - 14, 12, juce::Justification::centredLeft);

        // Show variation letter (A/B/C/D) in top-right of clip
        if (hasPattern && w > 20)
        {
            const juce::String varLetter = juce::String::charToString('A' + clip.variationIdx);
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.drawText(varLetter, x + w - 14, ty + 2, 12, 10, juce::Justification::right);
        }
    }
}

inline void PlaylistComponent::drawPlayhead(juce::Graphics& g)
{
    if (playheadBar < 0.0) return;
    const int x = trackHeaderWidth + (int)(playheadBar * barWidth);
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
        const int cx = trackHeaderWidth + (int)(clip.startBar   * barWidth);
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
    const int rightEdge = trackHeaderWidth + (int)((clip.startBar + clip.lengthBars) * barWidth);
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
            const double bar = (double)(pos.x - trackHeaderWidth) / barWidth;
            if (onSeekToBar) onSeekToBar(bar);
            setPlayheadBar(bar);
        }
        return;
    }

    PlaylistClip* clip = findClipAt(pos.x, pos.y);

    // Right-click → context or track menu
    if (e.mods.isRightButtonDown())
    {
        if (pos.x < trackHeaderWidth)
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
            const float pasteBar   = (float)(pos.x - trackHeaderWidth) / barWidth;
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
                        if (onClipAdded) onClipAdded(nc);
                        repaint();
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

            const float beatPos  = (float)(pos.x - trackHeaderWidth) / barWidth * 4.0f;
            const int   laneRelY = relY % autoLaneHeight;
            const float value    = 1.0f - juce::jlimit(0.0f, 1.0f,
                                       (float)(laneRelY - 4) / (float)(autoLaneHeight - 8));

            // Check if clicking near an existing point
            dragLaneIdx  = li;
            dragPointIdx = -1;
            for (int pi = 0; pi < (int)lane.points.size(); ++pi)
            {
                const int px = trackHeaderWidth + (int)(lane.points[(size_t)pi].beat * 0.25 * barWidth);
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
                pt.beat  = juce::jmax(0.0, (double)(e.getPosition().x - trackHeaderWidth) / barWidth * 4.0);
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
        // Double-click on clip with pattern → navigate to pattern; otherwise rename
        if (existing->patternId > 0 && onNavigateToPattern)
            onNavigateToPattern(existing->patternId);
        else
            showRenameDialog(existing->id);
        return;
    }

    // Double-click on empty space → create new clip
    const int track = trackIndexAt(pos.y);
    if (track < 0 || track >= getTrackCount()) return;

    const float rawBar  = (float)(pos.x - trackHeaderWidth) / barWidth;
    const float snapUnit = (snapDivisor == 0) ? 0.0f : 1.0f / (float)snapDivisor;
    const float bar = (snapDivisor == 0)
                        ? juce::jmax(0.0f, rawBar)
                        : snapUnit * std::round(rawBar / snapUnit);

    auto& list = clipList();
    int newId = 1;
    for (const auto& c : list) newId = juce::jmax(newId, c.id + 1);

    PlaylistClip clip;
    clip.id         = newId;
    clip.patternId  = -1;
    clip.trackIndex = track;
    clip.startBar   = bar;
    clip.lengthBars = 4.0f;
    clip.name       = "Clip " + juce::String(newId);
    if (onClipAdded) onClipAdded(clip);
    repaint();
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
        patMenu.addItem(90, "None", true, clip->patternId <= 0);
        patMenu.addSeparator();
        for (const auto& pat : project->patterns)
            patMenu.addItem(100 + pat.id, pat.name, true, clip->patternId == pat.id);
        menu.addSubMenu("Assign Pattern", patMenu);
    }

    // Variation submenu (A/B/C/D)
    {
        juce::PopupMenu varMenu;
        varMenu.addItem(201, "A", true, clip->variationIdx == 0);
        varMenu.addItem(202, "B", true, clip->variationIdx == 1);
        varMenu.addItem(203, "C", true, clip->variationIdx == 2);
        varMenu.addItem(204, "D", true, clip->variationIdx == 3);
        menu.addSubMenu("Variation", varMenu);
    }

    menu.addSeparator();
    menu.addItem(3, "Copy Clip");
    menu.addItem(4, "Detach to New Pattern", clip->patternId > 0);
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
                PlaylistClip deleted;
                bool found = false;
                for (const auto& c : clipList()) { if (c.id == clipId) { deleted = c; found = true; break; } }
                if (found && onClipDeleted) onClipDeleted(deleted);
                repaint();
            }
            else if (result == 90)
            {
                if (onClipPatternChanged) onClipPatternChanged(clipId, -1);
                repaint();
            }
            else if (result >= 201 && result <= 204)
            {
                if (auto* c = findClipById(clipId))
                {
                    c->variationIdx = result - 201;
                    repaint();
                }
            }
            else if (result >= 100)
            {
                const int patId = result - 100;
                if (onClipPatternChanged) onClipPatternChanged(clipId, patId);
                repaint();
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
                const auto newName = dialog->getTextEditorContents("name").trim();
                if (auto* c = findClipById(clipId))
                {
                    if (newName.isNotEmpty() && newName != c->name)
                    {
                        const auto oldName = c->name;
                        if (onClipRenamed)
                            onClipRenamed(clipId, oldName, newName);
                        else
                            c->name = newName;
                    }
                }
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
            g.drawLine((float)(trackHeaderWidth + bar * barWidth), (float)(laneY + 16),
                       (float)(trackHeaderWidth + bar * barWidth), (float)(laneY + autoLaneHeight), 0.5f);

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
                const float px = trackHeaderWidth + (float)(pt.beat * 0.25 * barWidth);
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
            const float px = trackHeaderWidth + (float)(pt.beat * 0.25 * barWidth);
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
