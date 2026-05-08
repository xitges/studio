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
#include "StudioLookAndFeel.h"

class PlaylistComponent : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget
{
public:
    PlaylistComponent();
    ~PlaylistComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown       (const juce::MouseEvent& e) override;
    void mouseDrag       (const juce::MouseEvent& e) override;
    void mouseUp         (const juce::MouseEvent& e) override;
    void mouseMove       (const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove  (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    // FileDragAndDropTarget — OS drag from Finder
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter          (const juce::StringArray& files, int x, int y) override;
    void fileDragExit           (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

    // DragAndDropTarget — internal drag from SampleBrowserComponent
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped              (const juce::DragAndDropTarget::SourceDetails& details) override;

    double getTotalBars() const;

    void setProject(Project* projectToUse);

    void setPlayheadBar(double bar)
    {
        if (playheadBar != bar) { playheadBar = bar; repaint(); }
    }
    double getPlayheadBar() const { return playheadBar; }
    int getTrackHeaderWidth() const { return trackHeaderWidth; }

    void setSnapDivisor(int d) { snapDivisor = d; repaint(); }
    void setViewport(juce::Viewport* vp) { viewport_ = vp; }

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

    // Audio clip dropped — host should call audioEngine.loadAudioClip(clipId, file)
    std::function<void(int clipId, juce::String filePath)> onAudioClipDropped;

    // Pitch/mode changed — host should push to AudioEngine
    std::function<void(int clipId, float oldPitch, float newPitch)>           onClipPitchChanged;
    std::function<void(int clipId, AudioClipMode oldMode, AudioClipMode mode)> onClipModeChanged;

    // Fade changed — called on mouseUp after drag
    std::function<void(int clipId, float fadeInBars, float fadeOutBars)> onClipFadeChanged;

    // Slip edit — Alt+drag shifts sourceOffsetSamples, called on mouseUp
    std::function<void(int clipId, float oldOffset, float newOffset)> onClipSlipEdited;

    // Pattern slip edit — Alt+drag shifts patternStartOffsetBars, called on mouseUp
    std::function<void(int clipId, float oldOffset, float newOffset)> onPatternSlipEdited;

    // Clip mute toggle
    std::function<void(int clipId, bool muted)> onClipMuteToggled;

    // Automation undo callbacks
    std::function<void(int laneIdx, int ptIdx, AutomationPoint pt)>                          onAutomationPointAdded;
    std::function<void(int laneIdx, int ptIdx, AutomationPoint before, AutomationPoint after)> onAutomationPointMoved;
    std::function<void(AutomationLane lane)>                                                  onAutomationLaneAdded;
    std::function<void(int laneIdx, AutomationLane lane)>                                     onAutomationLaneRemoved;

    // Returns decoded AudioBuffer for waveform preview (host → AudioEngine)
    std::function<std::shared_ptr<juce::AudioBuffer<float>>(const juce::String& path)> getAudioBuffer;

private:
    Project* project = nullptr;

    std::vector<PlaylistClip> localDemoClips;

    static constexpr int kPlaylistHdrH  = 38;  // PLAYLIST title + snap strip
    static constexpr int kRulerH        = 22;  // ruler band
    static constexpr int headerHeight   = kPlaylistHdrH + kRulerH;  // 60 total
    static constexpr int trackHeight    = 54;
    static constexpr int trackGap       = 0;
    static constexpr int trackHeaderWidth = 160;
    static constexpr int resizeHotspot  = 10;
    static constexpr int autoLaneHeight = 60;

    int barWidth = 44;  // matches the HTML reference timeline scale

    // ---------------------------------------------------------------------------
    // Clip waveform cache — built from step/note data, invalidated on zoom change
    // ---------------------------------------------------------------------------
    struct WavePoint { float min; float max; };
    using WaveCache = std::vector<WavePoint>;

    // Key: (patternId, variationIdx, pixelWidth, bpmRounded)
    mutable std::map<std::tuple<int,int,int,int>, WaveCache> waveformCache_;

    void clearWaveformCache()
    {
        waveformCache_.clear();
        audioWaveCache_.clear();
    }

    // Audio clip waveform cache — key: (filePathHash, pixelWidth)
    mutable std::map<std::pair<size_t, int>, WaveCache> audioWaveCache_;

    void handleAudioFileDrop(const juce::String& filePath, int dropX, int dropY);

    // Drag hover state for visual feedback
    bool  audioDragHover_    = false;
    float audioDragHoverX_   = 0.0f;
    int   audioDragHoverTrack_ = -1;

    // -----------------------------------------------------------------------
    // Lanczos-3 sinc interpolation helpers for smooth waveform rendering.
    // -----------------------------------------------------------------------
    static float lanczos3(float x) noexcept
    {
        const float ax = std::abs(x);
        if (ax < 1e-5f) return 1.0f;
        if (ax >= 3.0f) return 0.0f;
        const float piX  = juce::MathConstants<float>::pi * x;
        const float piX3 = juce::MathConstants<float>::pi * (x / 3.0f);
        return (std::sin(piX) / piX) * (std::sin(piX3) / piX3);
    }

    // Sample a float array at a fractional index using 6-tap Lanczos-3 kernel.
    static float lerpL3(const float* src, int len, float x) noexcept
    {
        float v = 0.0f;
        const int xi = (int)std::floor(x);
        for (int t = -2; t <= 3; ++t)
            v += src[juce::jlimit(0, len - 1, xi + t)] * lanczos3(x - (float)(xi + t));
        return v;
    }

    // Builds waveform from a real decoded AudioBuffer (audio clips).
    // visibleSamples: how many file samples the clip displays (0 = full file).
    // Samples beyond visibleSamples are rendered as silence (flat midline).
    static WaveCache buildAudioWave(const juce::AudioBuffer<float>& buf, int width,
                                    int visibleSamples = 0)
    {
        WaveCache cache((size_t)width);
        const int totalSamples = buf.getNumSamples();
        if (totalSamples <= 0 || width <= 0) return cache;

        // If no clip-length constraint, show the whole file
        const int displaySamples = (visibleSamples > 0)
                                   ? juce::jmin(visibleSamples, totalSamples)
                                   : totalSamples;

        const float spP   = (float)displaySamples / (float)width;
        const int   srcCh = juce::jmin(buf.getNumChannels(), 2);

        for (int px = 0; px < width; ++px)
        {
            const int s0 = (int)((float)px * spP);
            const int s1 = juce::jmin((int)((float)(px + 1) * spP), displaySamples);
            if (s0 >= displaySamples) break;  // beyond clip window → leave as silent
            float mn = 0.0f, mx = 0.0f;
            for (int ch = 0; ch < srcCh; ++ch)
                for (int s = s0; s < s1; ++s)
                {
                    const float v = buf.getSample(ch, s);
                    mn = juce::jmin(mn, v);
                    mx = juce::jmax(mx, v);
                }
            cache[(size_t)px] = { mn, mx };
        }
        return cache;
    }

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
    int snapDivisor = 4;    // 1=1bar, 2=½bar, 4=¼bar, 8=⅛bar, 16=1/16bar, 0=free

    juce::Viewport* viewport_ = nullptr;

    // Pixel ↔ bar helpers — single source of truth for all timeline math.
    // x is the component-local pixel x coordinate.
    float  pixelToBar(float  x)   const noexcept { return (x - (float)trackHeaderWidth) / (float)barWidth; }
    float  barToPixel(float  bar) const noexcept { return (float)trackHeaderWidth + bar * (float)barWidth; }

    // Apply snap grid to a raw bar value.
    float  snapBar(float bars) const noexcept
    {
        if (snapDivisor == 0) return bars;
        const float unit = 1.0f / (float)snapDivisor;
        return unit * std::round(bars / unit);
    }

    // Drag state
    int   draggingClipId  = -1;
    bool  resizingClip    = false;
    float dragStartBar    = 0.0f;
    int   dragStartTrack  = 0;
    float dragStartLength = 1.0f;
    int   dragStartMouseX = 0;
    int   dragStartMouseY = 0;

    // Fade drag state
    int   fadeDraggingClipId = -1;   // -1 = not dragging a fade handle
    bool  fadeDraggingIn     = true; // true = fade-in handle, false = fade-out
    float dragStartFade      = 0.0f; // value at drag start (bars)

    // Slip drag state (Alt + drag inside audio clip)
    int   slipDraggingClipId  = -1;
    float dragStartOffset     = 0.0f; // sourceOffsetSamples at drag start
    float dragSamplesPerPixel = 1.0f; // conversion factor for this drag

    // Pattern slip drag state (Alt + drag inside pattern clip)
    int   patSlipClipId       = -1;
    float dragStartPatOffset  = 0.0f; // patternStartOffsetBars at drag start

    double playheadBar = -1.0;

    // Clip clipboard (copy/paste)
    PlaylistClip clipboardClip;
    bool         hasClipboard   = false;
    int          selectedClipId = -1;

    // Bar position stored at right-click time (used by "Split Here")
    float contextMenuBar_ = 0.0f;

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
    int             dragLaneIdx      = -1;
    int             dragPointIdx     = -1;
    AutomationPoint dragStartAutoPt; // captured at mouseDown for undo

    // Automation snap resolution — applied when creating/moving breakpoints
    enum class AutoSnapResolution { Bar = 1, Half = 2, Quarter = 4, Eighth = 8, Sixteenth = 16 };
    AutoSnapResolution autoSnapRes = AutoSnapResolution::Quarter;

    float getAutoSnapBeats() const noexcept { return 4.0f / static_cast<float>(static_cast<int>(autoSnapRes)); }
    float snapAutoBeat(float beat, bool enable) const noexcept
    {
        if (!enable) return juce::jmax(0.0f, beat);
        const float unit = getAutoSnapBeats();
        return juce::jmax(0.0f, unit * std::round(beat / unit));
    }

    // Axis lock: determined from initial drag direction, reset each drag session
    enum class AutoAxisLock { None, TimeOnly, ValueOnly };
    AutoAxisLock     autoAxisLock    = AutoAxisLock::None;
    juce::Point<int> autoDragStartPos;

    // Hover state for automation breakpoints
    int hoverLaneIdx  = -1;
    int hoverPointIdx = -1;

    // Hit testing
    PlaylistClip* findClipAt(int x, int y);
    bool          isOnRightEdge(const PlaylistClip& clip, int mouseX) const;
    int           trackIndexAt(int mouseY) const;

    // Returns 0=none, 1=fadeIn handle, 2=fadeOut handle
    int           fadeHandleAt(const PlaylistClip& clip, int mouseX, int mouseY) const;

    // Clip operations
    void showContextMenu           (int clipId);
    void showRenameDialog          (int clipId);
    void showClipPropertiesDialog  (int clipId);
    void showTrackContextMenu      (int trackIdx);   // M11
    void showTrackRenameDialog     (int trackIdx);   // M11

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

    // Fixed header: re-draw on top at current scroll offset so it never scrolls away
    const int scrollY = viewport_ ? viewport_->getViewPositionY() : 0;
    if (scrollY > 0)
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(0, scrollY, getWidth(), headerHeight);
        g.setOrigin(0, scrollY);
        drawBackground(g);
        drawTimeRuler(g);
    }
}

inline void PlaylistComponent::drawBackground(juce::Graphics& g)
{
    using LF = StudioLookAndFeel;
    const float W = (float)getWidth();

    // Full bg (track area shows through)
    g.fillAll(juce::Colour(0xfff5f5f5));

    // ---- PLAYLIST header strip (0..kPlaylistHdrH) ----
    {
        juce::ColourGradient hdrBg(juce::Colours::white, 0.0f, 0.0f,
                                   juce::Colour(0xffe8e8e8), 0.0f, (float)headerHeight, false);
        g.setGradientFill(hdrBg);
        g.fillRect(0, 0, (int)W, kPlaylistHdrH);
        g.setColour(juce::Colour(0xffdcdcdc));
        g.drawLine(0.0f, (float)kPlaylistHdrH, W, (float)kPlaylistHdrH, 1.0f);

        const int cy = kPlaylistHdrH / 2;

        // Accent square
        g.setColour(juce::Colour(LF::kAccent));
        g.fillRoundedRectangle(12.0f, (float)(cy - 4), 8.0f, 8.0f, 2.0f);

        // "PLAYLIST"
        g.setFont(LF::monoFont(10.0f, juce::Font::bold));
        g.setColour(juce::Colour(LF::kText));
        g.drawText("PLAYLIST", 28, cy - 7, 68, 14, juce::Justification::centredLeft);

        // Tag helper
        auto drawTag = [&](int tx, int ty2, int tw, const juce::String& txt)
        {
            const juce::Rectangle<float> r((float)tx, (float)ty2, (float)tw, 14.0f);

            // 1. 태그 배경: 매우 밝은 회색 혹은 약간의 투명도가 있는 하얀색
            // 0x40ffffff는 배경 위에서 은은하게 겹쳐 보이는 효과를 줍니다.
            g.setColour(juce::Colour(0x40ffffff));
            g.fillRoundedRectangle(r, 3.0f);

            // 2. 태그 테두리: 다른 선들과 통일된 연한 회색
            g.setColour(juce::Colour(0xffdcdcdc));
            g.drawRoundedRectangle(r, 3.0f, 0.5f);

            // 3. 태그 텍스트: 밝은 배경에서도 잘 보이도록 진한 회색으로 변경
            g.setFont(LF::monoFont(7.0f, juce::Font::bold));
            g.setColour(juce::Colour(0xff666666));
            
            g.drawText(txt, tx + 3, ty2 + 1, tw - 6, 12, juce::Justification::centredLeft);
        };
        drawTag(100, cy - 7, 76, juce::String::fromUTF8("200 BARS \xc2\xb7 4/4"));
        drawTag(180, cy - 7, 56, juce::String(getTrackCount()) + " TRACKS");

        // Snap section
//        g.setFont(LF::monoFont(7.5f, juce::Font::bold));
//        g.setColour(juce::Colour(LF::kTextFaint));
//        g.drawText("SNAP", 240, cy - 7, 30, 14, juce::Justification::centredRight);

        const juce::String snapNames[] = {
            "1bar",
            juce::String::fromUTF8("\xc2\xbd"),    // ½
            juce::String::fromUTF8("\xc2\xbc"),    // ¼
            "FREE"
        };
        const int snapDivs[]  = { 1, 2, 4, 0 };
        const int snapBtnW[]  = { 30, 20, 20, 36 };

        // Snap container bg
        g.setColour(juce::Colour(0x0f000000));
        g.fillRoundedRectangle(242.0f, (float)(cy - 8), 110.0f, 16.0f, 3.0f);

        int bx = 244;
        for (int i = 0; i < 4; ++i)
        {
            const bool active = (snapDivisor == snapDivs[i]);
            const juce::Rectangle<float> btnR((float)bx, (float)(cy - 7), (float)snapBtnW[i], 14.0f);
            if (active)
            {
                g.setColour(juce::Colour(LF::kAccent));
                g.fillRoundedRectangle(btnR, 2.0f);
            }
            g.setFont(LF::monoFont(7.5f, juce::Font::bold));
            g.setColour(active ? juce::Colours::white : juce::Colour(LF::kTextDim));
            g.drawText(snapNames[i], (int)btnR.getX(), (int)btnR.getY(),
                       (int)btnR.getWidth(), (int)btnR.getHeight(), juce::Justification::centred);
            bx += snapBtnW[i];
        }

        // UNDO ⌘Z tag
        drawTag(bx + 4, cy - 7, 54,
                juce::String::fromUTF8("UNDO \xe2\x8c\x98") + "Z");
    }
}

inline void PlaylistComponent::drawTimeRuler(juce::Graphics& g)
{
    using LF = StudioLookAndFeel;
    const float ry = (float)kPlaylistHdrH;
    const float rh = (float)kRulerH;
    const int W = getWidth();
    const int totalBars = juce::jmax(W / barWidth + 1, 32);

    // ---- Ruler bg (display-bg) ----
    g.setColour(juce::Colour(LF::kDisplayBg));
    g.fillRect((int)0, (int)ry, W, (int)rh);
    // Bottom shadow
    g.setColour(juce::Colour(0x80000000));
    g.fillRect(0, (int)(ry + rh - 1), W, 1);

    // ---- TRK column header ----
    {
        g.setColour(juce::Colour(LF::kDisplayBg).darker(0.18f));
        g.fillRect(0, (int)ry, trackHeaderWidth, (int)rh);

        g.setFont(juce::Font(juce::FontOptions("VT323", 12.0f, juce::Font::plain)));
        g.setColour(juce::Colour(LF::kDisplayFg).withAlpha(0.7f));
        g.drawText("TRK / " + juce::String(getTrackCount()), 8, (int)ry, 80, (int)rh, juce::Justification::centredLeft);

        // ＋ add track button
        g.setFont(juce::Font(juce::FontOptions("VT323", 14.0f, juce::Font::plain)));
        g.setColour(juce::Colour(LF::kDisplayFg));
        g.drawText(juce::String::fromUTF8("\xef\xbc\x8b"), // ＋
                   trackHeaderWidth - 22, (int)ry, 18, (int)rh, juce::Justification::centred);

        // Right border
        g.setColour(juce::Colour(0x60000000));
        g.fillRect(trackHeaderWidth - 1, (int)ry, 1, (int)rh);
    }

    // ---- Bar marks (ruler + track grid) ----
    // Sub-bar grid
    if (snapDivisor > 1)
    {
        g.setColour(juce::Colour(LF::kDisplayFg).withAlpha(0.05f));
        for (int bar = 0; bar < totalBars; ++bar)
            for (int sub = 1; sub < snapDivisor; ++sub)
            {
                const float x = (float)trackHeaderWidth + (bar + (float)sub / snapDivisor) * barWidth;
                g.drawLine(x, (float)headerHeight, x, (float)getHeight(), 0.5f);
            }
    }

    for (int bar = 0; bar < totalBars; ++bar)
    {
        const float x = (float)trackHeaderWidth + (float)bar * barWidth;
        const bool major = (bar % 4 == 0);

        // Ruler tick line
        g.setColour(major ? juce::Colour(LF::kDisplayFg).withAlpha(0.5f)
                          : juce::Colour(LF::kDisplayFg).withAlpha(0.15f));
        g.fillRect(x, ry, 1.0f, rh);

        // Track grid line (extends through track rows)
        g.setColour(major ? juce::Colour(0x1e000000) : juce::Colour(0x0d000000));
        g.fillRect(x, (float)headerHeight, 1.0f, (float)(getHeight() - headerHeight));

        // Bar number (every 4 bars, VT323 phosphor glow)
        if (major)
        {
            const juce::String barNum = juce::String(bar + 1).paddedLeft('0', 3);
            g.setFont(juce::Font(juce::FontOptions("VT323", 13.0f, juce::Font::plain)));
            // Glow pass
            g.setColour(juce::Colour(LF::kDisplayFg).withAlpha(0.28f));
            g.drawText(barNum, (int)x + 3, (int)ry + 2, barWidth - 4, (int)rh - 4,
                       juce::Justification::centredLeft);
            // Main text
            g.setColour(juce::Colour(LF::kDisplayFg));
            g.drawText(barNum, (int)x + 4, (int)ry + 3, barWidth - 4, (int)rh - 4,
                       juce::Justification::centredLeft);
        }
    }
}

inline void PlaylistComponent::drawTracks(juce::Graphics& g)
{
    using LF = StudioLookAndFeel;
    const int count = getTrackCount();
    const bool hasTracks = (project != nullptr && !project->playlistTracks.empty());

    for (int t = 0; t < count; ++t)
    {
        const int trackY = headerHeight + t * (trackHeight + trackGap);

        // Per-track colour: from model or fallback palette
//        const juce::Colour tCol = (hasTracks && t < (int)project->playlistTracks.size())
//            ? project->playlistTracks[(size_t)t].colour
//            : juce::Colour(t % 2 == 0 ? 0xffd8412a : 0xffe89c2b);
        const juce::Colour tCol = juce::Colour(0xffffffff);

        // ---- Track row bg (cream tones matching reference) ----
        {
            const juce::Colour c1 = (t % 2 == 0) ? juce::Colour(0xfffafafa) : juce::Colour(0xfff5f5f5);
            const juce::Colour c2 = (t % 2 == 0) ? juce::Colour(0xfff5f5f5) : juce::Colour(0xfff0f0f0);
            juce::ColourGradient rowBg(c1, 0.0f, (float)trackY, c2, 0.0f, (float)(trackY + trackHeight), false);
            g.setGradientFill(rowBg);
            g.fillRect(trackHeaderWidth, trackY, getWidth() - trackHeaderWidth, trackHeight);
        }

        // Row bottom border
        g.setColour(juce::Colour(LF::kAccent).withAlpha(0.3f));
        g.drawLine(0.0f, (float)(trackY + trackHeight - 1), (float)getWidth(), (float)(trackY + trackHeight - 1), 0.5f);

        // ---- Track header bg (panel → chassis-2 gradient) ----
        {
            juce::ColourGradient hdrBg(juce::Colours::white, 0.0f, (float)trackY,
                                                   juce::Colour(0xfff0f0f0), 0.0f, (float)(trackY + trackHeight), false);
            g.setGradientFill(hdrBg);
            g.fillRect(0, trackY, trackHeaderWidth, trackHeight);
            g.setColour(juce::Colour(LF::kAccent).withAlpha(0.8f));
            g.drawLine((float)trackHeaderWidth - 0.5f, (float)trackY,
                       (float)trackHeaderWidth - 0.5f, (float)(trackY + trackHeight), 1.0f);
        }

        // ---- Idx colour box (18×18, rounded 3) ----
        {
            const int boxX = 8, boxY = trackY + (trackHeight - 18) / 2;
            // Soft glow ring
            g.setColour(tCol.withAlpha(0.22f));
            g.fillRoundedRectangle((float)(boxX - 2), (float)(boxY - 2), 22.0f, 22.0f, 4.0f);
            // Box fill
            g.setColour(tCol);
            g.fillRoundedRectangle((float)boxX, (float)boxY, 18.0f, 18.0f, 3.0f);
            // Border
            g.setColour(juce::Colour(0x66000000));
            g.drawRoundedRectangle((float)boxX + 0.5f, (float)boxY + 0.5f, 17.0f, 17.0f, 3.0f, 1.0f);
            // Number
            g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 9.0f, juce::Font::bold)));
            g.setColour(juce::Colours::black.withAlpha(0.9f));
            g.drawText(juce::String(t + 1), boxX, boxY, 18, 18, juce::Justification::centred);
        }

        // ---- Track name (10px bold uppercase) ----
        {
            const juce::String name = (hasTracks && t < (int)project->playlistTracks.size())
                                      ? project->playlistTracks[(size_t)t].name
                                      : ("Track " + juce::String(t + 1));
            g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 9.5f, juce::Font::bold)));
            g.setColour(juce::Colour(LF::kText));
            g.drawText(name.toUpperCase(), 34, trackY + 5, trackHeaderWidth - 34 - 34, 13,
                       juce::Justification::centredLeft, true);
        }

        // ---- M / S / R buttons (16×13) ----
        {
            struct BtnDef { const char* label; juce::uint32 onColor; bool on; };
            const BtnDef btns[] = {
                { "M", LF::kLedAmber, false },
                { "S", LF::kLedGreen, false },
                { "R", LF::kLedRed,   false },
            };
            int bx = 34;
            const int by = trackY + 22;
            for (const auto& b : btns)
            {
                const juce::Rectangle<float> btnR((float)bx, (float)by, 16.0f, 13.0f);
                g.setColour(b.on ? juce::Colour(b.onColor) : juce::Colour(0x0d000000));
                g.fillRoundedRectangle(btnR, 2.0f);
                g.setColour(juce::Colour(0x33000000));
                g.drawRoundedRectangle(btnR.reduced(0.5f), 2.0f, 0.5f);
                if (b.on)
                {
                    g.setColour(juce::Colour(b.onColor).withAlpha(0.35f));
                    g.fillRoundedRectangle(btnR.expanded(2.0f, 2.0f), 3.0f);
                }
                g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 7.0f, juce::Font::bold)));
                g.setColour(b.on ? juce::Colour(0xcc000000) : juce::Colour(LF::kTextFaint));
                g.drawText(b.label, (int)btnR.getX(), (int)btnR.getY(), 16, 13, juce::Justification::centred);
                bx += 19;
            }
        }

        // ---- VOL knob (22×22, painted) ----
        {
            const float kx = (float)(trackHeaderWidth - 8 - 22);
            const float ky = (float)(trackY + (trackHeight - 22) / 2);
            const float kr = 11.0f;
            const float cx = kx + kr, cy2 = ky + kr;
            juce::ColourGradient kg(juce::Colour(LF::kChassis), cx - kr * 0.3f, cy2 - kr * 0.3f,
                                    juce::Colour(0xff0d0d0d), cx + kr, cy2 + kr, true);
            g.setGradientFill(kg);
            g.fillEllipse(kx, ky, 22.0f, 22.0f);
            g.setColour(juce::Colour(LF::kPanelRim));
            g.drawEllipse(kx + 0.5f, ky + 0.5f, 21.0f, 21.0f, 0.8f);
            // Indicator needle at ~80% volume position
            const float angle = -juce::MathConstants<float>::pi * 0.75f
                                + 0.8f * juce::MathConstants<float>::pi * 1.5f;
            const float ix = cx + std::cos(angle) * (kr - 3.5f);
            const float iy = cy2 + std::sin(angle) * (kr - 3.5f);
            g.setColour(juce::Colour(LF::kAccent));
            g.fillEllipse(ix - 1.5f, iy - 1.5f, 3.0f, 3.0f);
        }
    }
}

inline void PlaylistComponent::drawClips(juce::Graphics& g)
{
    using LF = StudioLookAndFeel;
    // ---- Track colour: 8-color palette ----
        auto trackCol = [&](int tidx) -> juce::Colour
        {
            static const juce::uint32 pal[] = {
                0xffd8412a, 0xffe89c2b, 0xff7ab87a, 0xff5fa8d8,
                0xffb87ad6, 0xff8d7a5a, 0xff5dd8c8, 0xffb8b87a
            };
            return juce::Colour(pal[(size_t)tidx % 8]);
        };

    const int tc = getTrackCount();
    for (const auto& clip : clipList())
    {
        if (clip.trackIndex < 0 || clip.trackIndex >= tc) continue;

        const int x  = trackHeaderWidth + (int)(clip.startBar  * barWidth);
        const int w  = juce::jmax(4, juce::jmin((int)(clip.lengthBars * barWidth) - 4, getWidth() * 4));
        const int ty = headerHeight + clip.trackIndex * (trackHeight + trackGap) + 3;

        const int h  = juce::jmax(4, trackHeight - 6);

        if (x + w < 0 || x > getWidth()) continue;

        const bool isAudio  = (clip.clipType == ClipType::Audio);
        const juce::Colour col = trackCol(clip.trackIndex);
        const float muteA  = clip.muted ? 0.4f : 1.0f;
        const float baseA  = (clip.id == draggingClipId ? 0.6f : 1.0f) * muteA;

        // ---- Clip background ----
        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(x, ty, w, h);
            g.setColour(col.withAlpha(0.22f * baseA));
            g.fillRoundedRectangle((float)x, (float)ty, (float)w, (float)h, 3.0f);
            if (isAudio)
            {
                // Diagonal stripes (135°): alternating color33/color55
                for (float sx = (float)(x - h); sx < (float)(x + w); sx += 8.0f)
                {
                    g.setColour(col.withAlpha(0.20f * baseA));
                    g.drawLine(sx, (float)ty, sx + (float)h, (float)(ty + h), 4.0f);
                    g.setColour(col.withAlpha(0.33f * baseA));
                    g.drawLine(sx + 4.0f, (float)ty, sx + 4.0f + (float)h, (float)(ty + h), 4.0f);
                }
            }
        }

        // ---- Border ----
        g.setColour(col.withAlpha(baseA));
        g.drawRoundedRectangle((float)x + 0.5f, (float)ty + 0.5f,
                               (float)w - 1.0f, (float)h - 1.0f, 3.0f, 1.0f);

        // ---- Clip header row (top 18px): gradient + bottom rule ----
        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(x + 1, ty + 1, w - 2, 17);
            juce::ColourGradient hg(col.withAlpha(0.40f * baseA), 0.0f, (float)ty,
                                    col.withAlpha(0.20f * baseA), 0.0f, (float)(ty + 18), false);
            g.setGradientFill(hg);
            g.fillRect(x + 1, ty + 1, w - 2, 17);
        }
        g.setColour(col.withAlpha(0.65f * baseA));
        g.drawLine((float)(x + 1), (float)(ty + 18), (float)(x + w - 1), (float)(ty + 18), 0.5f);

        // ---- Clip label ----
        if (w > 20)
        {
            juce::String prefix, label;
            if (isAudio)
            {
                prefix = juce::String::fromUTF8("\xe2\x99\xaa"); // ♪
                label  = clip.name.isNotEmpty() ? clip.name
                       : juce::File(clip.audioFilePath).getFileNameWithoutExtension();
            }
            else
            {
                prefix = juce::String::fromUTF8("\xe2\x96\xa6"); // ▦
                const juce::String patId = (clip.patternId > 0)
                    ? ("PAT " + juce::String::formatted("%02d", clip.patternId))
                    : "PAT --";
                const juce::String cname = clip.name.isNotEmpty() ? clip.name
                                         : ("Clip " + juce::String(clip.id));
                label = patId + juce::String::fromUTF8(" \xc2\xb7 ") + cname;
            }
            g.setFont(juce::Font(juce::FontOptions("JetBrains Mono", 8.5f, juce::Font::bold)));
            g.setColour(juce::Colour(StudioLookAndFeel::kText).withAlpha(baseA));
            g.drawText(prefix + " " + label,
                       x + 5, ty + 4, w - 10 - (clip.muted ? 12 : 0), 10,
                       juce::Justification::centredLeft, true);
            if (clip.muted && w > 30)
            {
                g.setColour(juce::Colour(StudioLookAndFeel::kTextFaint));
                g.drawText("M", x + w - 13, ty + 4, 10, 10, juce::Justification::centredRight);
            }
        }

        // ---- Waveform preview ----
        if (w > 24)
        {
            const int wfX = x + 3;
            const int wfY = ty + 21;
            const int wfW = w - 6;
            const int wfH = h - 24;
            if (wfH > 4 && wfW > 0)
            {
                const WaveCache* wcPtr = nullptr;
                WaveCache        tempCache;

                if (!isAudio)
                {
                    // Pattern clip — synthesised waveform
                    const Pattern* pat = nullptr;
                    const int vi = juce::jlimit(0, Pattern::kMaxVariations - 1, clip.variationIdx);
                    if (project != nullptr && clip.patternId > 0)
                        for (const auto& p : project->patterns)
                            if (p.id == clip.patternId) { pat = &p; break; }

                    if (pat != nullptr)
                    {
                        const double bpm  = (project != nullptr) ? project->bpm : 120.0;
                        const int    bpmR = juce::roundToInt(bpm);
                        const auto   key  = std::make_tuple(clip.patternId, vi, wfW, bpmR);
                        auto it = waveformCache_.find(key);
                        if (it == waveformCache_.end())
                        {
                            const float clipBeats = clip.lengthBars * 4.0f;
                            waveformCache_[key] = buildPatternWave(*pat, vi, wfW, clipBeats, bpm);
                            it = waveformCache_.find(key);
                        }
                        wcPtr = &it->second;
                    }
                }
                else
                {
                    // Audio clip — real waveform from decoded buffer
                    if (getAudioBuffer && clip.audioFilePath.isNotEmpty())
                    {
                        const size_t pathHash = std::hash<std::string>{}(clip.audioFilePath.toStdString());
                        const auto   key      = std::make_pair(pathHash, wfW);
                        auto it = audioWaveCache_.find(key);
                        if (it == audioWaveCache_.end())
                        {
                            auto buf = getAudioBuffer(clip.audioFilePath);
                            if (buf != nullptr && buf->getNumSamples() > 0)
                            {
                                const double bpmL = (project != nullptr) ? project->bpm : 120.0;
                                const int vis = (int)(clip.lengthBars * 4.0 * 60.0 / bpmL * 44100.0);
                                audioWaveCache_[key] = buildAudioWave(*buf, wfW, vis);
                            }
                            else
                            {
                                audioWaveCache_[key] = {};
                            }
                            it = audioWaveCache_.find(key);
                        }
                        if (!it->second.empty())
                            wcPtr = &it->second;
                    }
                }

                if (wcPtr != nullptr && !wcPtr->empty())
                {
                    juce::Graphics::ScopedSaveState ss(g);
                    g.reduceClipRegion(wfX, wfY, wfW, wfH);
                    const float midY = (float)(wfY + wfH / 2);
                    const float half = (float)(wfH / 2) * 0.88f;
                    g.setColour(col.withAlpha(0.80f * baseA));
                    for (int px = 0; px < wfW && px < (int)wcPtr->size(); ++px)
                    {
                        const float y0 = midY - (*wcPtr)[px].max * half;
                        const float y1 = midY - (*wcPtr)[px].min * half;
                        g.drawVerticalLine(wfX + px, y0, juce::jmax(y0 + 0.5f, y1));
                    }
                }
            }
        }

        // ---- Resize handle stripe (right edge) ----
        g.setColour(col.withAlpha(0.45f * baseA));
        g.fillRect(x + w - 4, ty + 2, 3, h - 4);

        // ---- Fade overlays ----
        if (w > 4)
        {
            const float fiPx = clip.fadeInBars  * (float)barWidth;
            const float foPx = clip.fadeOutBars * (float)barWidth;
            if (fiPx > 0.5f)
            {
                const float ex = juce::jmin(fiPx, (float)w);
                juce::Path tri;
                tri.addTriangle((float)x + 1, (float)ty, (float)x + 1 + ex, (float)ty, (float)x + 1, (float)(ty + h));
                g.setColour(juce::Colours::black.withAlpha(0.35f));
                g.fillPath(tri);
                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.fillEllipse((float)x + 1 + ex - 4.0f, (float)ty, 8.0f, 8.0f);
            }
            if (foPx > 0.5f)
            {
                const float ex = juce::jmin(foPx, (float)w);
                const float rx = (float)(x + 1 + w);
                juce::Path tri;
                tri.addTriangle(rx, (float)ty, rx - ex, (float)ty, rx, (float)(ty + h));
                g.setColour(juce::Colours::black.withAlpha(0.35f));
                g.fillPath(tri);
                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.fillEllipse(rx - ex - 4.0f, (float)ty, 8.0f, 8.0f);
            }
        }

        // ---- Slip edit labels (kept for drag feedback) ----
        if (!isAudio && clip.patternStartOffsetBars != clip.originalPatternStartOffsetBars
            && clip.id == patSlipClipId && w > 40)
        {
            const juce::String txt = juce::String(clip.patternStartOffsetBars, 2) + " bar";
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            const int lblX = x + (w - 48) / 2;
            g.setColour(juce::Colour(0xff604020).withAlpha(0.85f));
            g.fillRoundedRectangle((float)lblX, (float)(ty + h/2 - 6), 48.0f, 12.0f, 3.0f);
            g.setColour(juce::Colour(0xffffd080));
            g.drawText(txt, lblX, ty + h/2 - 6, 48, 12, juce::Justification::centred);
        }
        if (isAudio && clip.sourceOffsetSamples != clip.originalSourceOffsetSamples
            && clip.id == slipDraggingClipId && w > 40)
        {
            const double bpmL = (project != nullptr) ? project->bpm : 120.0;
            const float offsetBars = (float)((double)clip.sourceOffsetSamples / (44100.0 * 60.0 / bpmL * 4.0));
            g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
            const juce::String txt = juce::String(offsetBars, 2) + " bar";
            const int lblX = x + (w - 48) / 2;
            g.setColour(juce::Colour(0xff102040).withAlpha(0.85f));
            g.fillRoundedRectangle((float)lblX, (float)(ty + h/2 - 6), 48.0f, 12.0f, 3.0f);
            g.setColour(juce::Colour(0xff80d0ff));
            g.drawText(txt, lblX, ty + h/2 - 6, 48, 12, juce::Justification::centred);
        }

        // (isAudioClip, hasPattern, isSlipTarget kept as unused — suppress warnings)
        (void)isAudio;
    }

    // Audio drag hover indicator
    if (audioDragHover_ && audioDragHoverTrack_ >= 0)
    {
        const int hY = headerHeight + audioDragHoverTrack_ * (trackHeight + trackGap);
        g.setColour(juce::Colour(StudioLookAndFeel::kAccent).withAlpha(0.25f));
        g.fillRect((int)audioDragHoverX_, hY, 4, trackHeight);
    }
}


inline void PlaylistComponent::drawPlayhead(juce::Graphics& g)
{
    if (playheadBar < 0.0) return;
    const float px = (float)(trackHeaderWidth + (int)(playheadBar * barWidth));

    // Glow
    g.setColour(juce::Colour(0xffff3333).withAlpha(0.25f));
    g.fillRect(px - 2.0f, (float)kPlaylistHdrH, 5.0f, (float)(getHeight() - kPlaylistHdrH));

    // Main 2px line (starts at ruler, goes to bottom)
    g.setColour(juce::Colour(0xffff3333));
    g.drawLine(px, (float)kPlaylistHdrH, px, (float)getHeight(), 2.0f);

    // Triangle cap in ruler
    juce::Path tri;
    tri.addTriangle(px - 5.0f, (float)kPlaylistHdrH,
                    px + 5.0f, (float)kPlaylistHdrH,
                    px,        (float)(kPlaylistHdrH + 10));
    g.setColour(juce::Colour(0xffff3333));
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
        const int cw = juce::jmin((int)(clip.lengthBars * barWidth) - 2, getWidth() * 4);
        const int cy = headerHeight + clip.trackIndex * (trackHeight + trackGap) + 3;
        const int ch = trackHeight - 6;
        if (cw > 0 && juce::Rectangle<int>(cx, cy, cw, ch).contains(x, y))
            return &clip;
    }
    return nullptr;
}

inline bool PlaylistComponent::isOnRightEdge(const PlaylistClip& clip, int mouseX) const
{
    const float rawRight = (clip.startBar + clip.lengthBars) * barWidth;
    if (rawRight > 1e8f) return false;
    const int rightEdge = trackHeaderWidth + (int)rawRight;
    return mouseX >= (rightEdge - resizeHotspot) && mouseX <= (rightEdge + 2);
}

inline int PlaylistComponent::trackIndexAt(int mouseY) const
{
    const int relY = mouseY - headerHeight;
    if (relY < 0) return -1;
    const int t = relY / (trackHeight + trackGap);
    return juce::jlimit(0, getTrackCount() - 1, t);
}

inline int PlaylistComponent::fadeHandleAt(const PlaylistClip& clip, int mouseX, int mouseY) const
{
    const int cx  = trackHeaderWidth + (int)(clip.startBar    * barWidth);
    const int cw  = juce::jmin((int)(clip.lengthBars * barWidth) - 2, getWidth() * 4);
    const int cy  = headerHeight + clip.trackIndex * (trackHeight + trackGap) + 3;
    if (cw <= 0) return 0;
    // Fade handles occupy the top 8px of the clip, 14px wide at each edge
    if (mouseY < cy || mouseY > cy + 8) return 0;
    if (mouseX >= cx     && mouseX <= cx + 14) return 1; // fade-in
    if (mouseX >= cx + cw - 14 && mouseX <= cx + cw) return 2; // fade-out
    return 0;
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

inline void PlaylistComponent::mouseDown(const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    // Click in PLAYLIST header strip → check snap buttons
    if (pos.y < kPlaylistHdrH)
    {
        if (!e.mods.isRightButtonDown())
        {
            const int snapDivs[] = { 1, 2, 4, 0 };
            const int snapBtnW[] = { 30, 20, 20, 36 };
            int bx = 244;
            for (int i = 0; i < 4; ++i)
            {
                if (pos.x >= bx && pos.x < bx + snapBtnW[i])
                {
                    snapDivisor = snapDivs[i];
                    repaint();
                    return;
                }
                bx += snapBtnW[i];
            }
        }
        return;
    }

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
            contextMenuBar_ = pixelToBar((float)pos.x);
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

            // "Add Automation Lane" submenu — all targets
            juce::PopupMenu autoMenu;
            autoMenu.addItem(10, juce::String("Master Volume"));
            autoMenu.addItem(11, juce::String("BPM (60-200)"));
            autoMenu.addSeparator();

            juce::PopupMenu chMenu;
            for (int i = 0; i < 16; ++i)
                chMenu.addItem(20 + i, juce::String("Ch ") + juce::String(i + 1));
            autoMenu.addSubMenu(juce::String("Channel Volumes"), chMenu);

            juce::PopupMenu mixMenu;
            for (int t = 0; t < 8; ++t)
                mixMenu.addItem(40 + t, juce::String("Mixer Track ") + juce::String(t + 1));
            autoMenu.addSubMenu(juce::String("Mixer Track Volumes"), mixMenu);

            m.addSubMenu(juce::String("Add Automation Lane"), autoMenu);

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
                    else if (r >= 20 && r <= 35)
                    {
                        const int ch = r - 20;
                        addAutomationLane("ch" + juce::String(ch) + "vol", 0.0f, 1.0f,
                                          "Ch " + juce::String(ch + 1) + " Volume");
                    }
                    else if (r >= 40 && r <= 47)
                    {
                        const int t = r - 40;
                        addAutomationLane("mixVol" + juce::String(t), 0.0f, 1.0f,
                                          "Mixer Track " + juce::String(t + 1));
                    }
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

            const bool  shiftHeld = e.mods.isShiftDown();
            const int   laneY     = autoLanesY() + li * autoLaneHeight;
            const float rawBeat   = (float)(pos.x - trackHeaderWidth) / barWidth * 4.0f;
            const float beatPos   = snapAutoBeat(rawBeat, !shiftHeld);
            const int   laneRelY  = relY % autoLaneHeight;
            const float value     = 1.0f - juce::jlimit(0.0f, 1.0f,
                                        (float)(laneRelY - 4) / (float)(autoLaneHeight - 8));

            // Check if clicking near an existing point (radius = 10px, 2D distance)
            dragLaneIdx  = li;
            dragPointIdx = -1;
            for (int pi = 0; pi < (int)lane.points.size(); ++pi)
            {
                const float ptPx = (float)trackHeaderWidth + (float)(lane.points[(size_t)pi].beat * 0.25 * barWidth);
                const float ptPy = (float)(laneY + autoLaneHeight - 4)
                                   - lane.points[(size_t)pi].value * (float)(autoLaneHeight - 8);
                const float dx   = (float)pos.x - ptPx;
                const float dy   = (float)pos.y - ptPy;
                if (std::sqrt(dx * dx + dy * dy) < 10.0f)
                {
                    dragPointIdx = pi;
                    break;
                }
            }

            if (dragPointIdx < 0)
            {
                // Add new point (value quantised to 0.01 steps)
                AutomationPoint pt;
                pt.beat  = (double)beatPos;
                pt.value = juce::jlimit(0.0f, 1.0f, std::round(value / 0.01f) * 0.01f);
                auto it  = lane.points.begin();
                while (it != lane.points.end() && it->beat < pt.beat) ++it;
                const int insertIdx = (int)(it - lane.points.begin());
                lane.points.insert(it, pt);
                dragPointIdx = insertIdx;
                if (onAutomationPointAdded)
                    onAutomationPointAdded(li, insertIdx, lane.points[(size_t)insertIdx]);
            }

            // Capture state for undo on mouseUp + initialise axis-lock
            if (dragPointIdx < (int)lane.points.size())
                dragStartAutoPt = lane.points[(size_t)dragPointIdx];
            autoDragStartPos = pos;
            autoAxisLock     = AutoAxisLock::None;

            repaint();
            return;
        }
    }
    dragLaneIdx = -1;

    // Check fade handles first (top edge of audio clips)
    if (clip != nullptr && !e.mods.isRightButtonDown())
    {
        const int fh = fadeHandleAt(*clip, pos.x, pos.y);
        if (fh != 0)
        {
            fadeDraggingClipId = clip->id;
            fadeDraggingIn     = (fh == 1);
            dragStartFade      = fadeDraggingIn ? clip->fadeInBars : clip->fadeOutBars;
            dragStartMouseX    = pos.x;
            repaint();
            return;
        }
    }
    fadeDraggingClipId = -1;

    // Alt + left-button drag on pattern clip → pattern slip edit
    if (clip != nullptr && !e.mods.isRightButtonDown()
        && e.mods.isAltDown() && clip->clipType == ClipType::Pattern)
    {
        patSlipClipId        = clip->id;
        dragStartPatOffset   = clip->patternStartOffsetBars;
        dragStartMouseX      = pos.x;
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        repaint();
        return;
    }
    patSlipClipId = -1;

    // Alt + left-button drag on audio clip → slip edit
    if (clip != nullptr && !e.mods.isRightButtonDown()
        && e.mods.isAltDown() && clip->clipType == ClipType::Audio)
    {
        slipDraggingClipId = clip->id;
        dragStartOffset    = clip->sourceOffsetSamples;
        dragStartMouseX    = pos.x;
        // samples per pixel: samplesPerBar / barWidth
        const double bpmLocal = (project != nullptr) ? project->bpm : 120.0;
        const double spBar    = 44100.0 * 60.0 / bpmLocal * 4.0;
        dragSamplesPerPixel   = (float)(spBar / (double)barWidth);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        repaint();
        return;
    }
    slipDraggingClipId = -1;

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
                auto& pt = lane.points[(size_t)dragPointIdx];

                const bool shiftHeld = e.mods.isShiftDown();
                const bool altHeld   = e.mods.isAltDown();
                const float fineMul  = altHeld ? 0.2f : 1.0f;

                // Axis lock: latch once initial movement exceeds 4px
                const int totalDx = std::abs(e.getPosition().x - autoDragStartPos.x);
                const int totalDy = std::abs(e.getPosition().y - autoDragStartPos.y);
                if (autoAxisLock == AutoAxisLock::None && (totalDx > 4 || totalDy > 4))
                    autoAxisLock = (totalDx >= totalDy) ? AutoAxisLock::TimeOnly
                                                        : AutoAxisLock::ValueOnly;

                // Time axis: relative displacement from drag-start position
                if (autoAxisLock != AutoAxisLock::ValueOnly)
                {
                    const float dxPx    = (float)(e.getPosition().x - autoDragStartPos.x) * fineMul;
                    const float rawBeat = (float)dragStartAutoPt.beat + dxPx / (float)barWidth * 4.0f;
                    pt.beat = (double)snapAutoBeat(rawBeat, !shiftHeld);
                }

                // Value axis: relative displacement scaled to lane height
                if (autoAxisLock != AutoAxisLock::TimeOnly)
                {
                    const float dyPx   = (float)(e.getPosition().y - autoDragStartPos.y) * fineMul;
                    const float rawVal = (float)dragStartAutoPt.value
                                        - dyPx / (float)(autoLaneHeight - 8);
                    pt.value = juce::jlimit(0.0f, 1.0f,
                                            std::round(rawVal / 0.01f) * 0.01f);
                }

                repaint();
            }
        }
        return;
    }

    // Fade handle drag
    if (fadeDraggingClipId >= 0)
    {
        if (auto* clip = findClipById(fadeDraggingClipId))
        {
            const float deltaX = (float)(e.getPosition().x - dragStartMouseX);
            const float deltaBars = deltaX / (float)barWidth;
            const float newFade = juce::jmax(0.0f,
                fadeDraggingIn ? dragStartFade + deltaBars
                               : dragStartFade - deltaBars);
            // Clamp so fade-in + fade-out don't exceed clip length
            const float maxFade = clip->lengthBars * 0.5f;
            if (fadeDraggingIn)
                clip->fadeInBars  = juce::jmin(newFade, maxFade);
            else
                clip->fadeOutBars = juce::jmin(newFade, maxFade);
            repaint();
        }
        return;
    }

    // Pattern slip drag (Alt + drag on pattern clip)
    if (patSlipClipId >= 0)
    {
        if (auto* clip = findClipById(patSlipClipId))
        {
            const float deltaBars = (float)(e.getPosition().x - dragStartMouseX) / (float)barWidth;
            const float rawOffset = dragStartPatOffset + deltaBars;

            float patternBars = 0.0f;
            if (project != nullptr)
                for (const auto& p : project->patterns)
                    if (p.id == clip->patternId) { patternBars = (float)p.stepCount / 16.0f; break; }

            if (patternBars > 0.0f)
                clip->patternStartOffsetBars = std::fmod(std::fmod(rawOffset, patternBars) + patternBars, patternBars);
            else
                clip->patternStartOffsetBars = juce::jmax(0.0f, rawOffset);
            repaint();
        }
        return;
    }

    // Slip drag (Alt + drag)
    if (slipDraggingClipId >= 0)
    {
        if (auto* clip = findClipById(slipDraggingClipId))
        {
            const float deltaX       = (float)(e.getPosition().x - dragStartMouseX);
            const float deltaOffset  = deltaX * dragSamplesPerPixel;
            const float rawOffset    = dragStartOffset + deltaOffset;

            // Wrap offset into [0, fileLen) if we have the buffer, else clamp ≥ 0
            float fileLen = 0.0f;
            if (getAudioBuffer)
            {
                auto buf = getAudioBuffer(clip->audioFilePath);
                if (buf != nullptr && buf->getNumSamples() > 0)
                    fileLen = (float)buf->getNumSamples();
            }
            if (fileLen > 0.0f)
                clip->sourceOffsetSamples = std::fmod(std::fmod(rawOffset, fileLen) + fileLen, fileLen);
            else
                clip->sourceOffsetSamples = juce::jmax(0.0f, rawOffset);
            repaint();
        }
        return;
    }

    if (draggingClipId < 0) return;

    const auto  pos    = e.getPosition();
    const float deltaX = (float)(pos.x - dragStartMouseX);

    PlaylistClip* clip = findClipById(draggingClipId);
    if (clip == nullptr) return;

    static constexpr float minLength = 0.0625f;  // 1/16 bar — independent of snap grid

    if (resizingClip)
    {
        const float rawLen = dragStartLength + deltaX / (float)barWidth;
        clip->lengthBars   = juce::jmax(minLength, snapBar(rawLen));
    }
    else
    {
        const float rawBar = dragStartBar + deltaX / (float)barWidth;
        clip->startBar     = juce::jmax(0.0f, snapBar(rawBar));

        const int deltaY     = pos.y - dragStartMouseY;
        const int deltaTrack = (int)std::round((double)deltaY / (double)(trackHeight + trackGap));
        clip->trackIndex = juce::jlimit(0, getTrackCount() - 1, dragStartTrack + deltaTrack);
    }

    repaint();
}

inline void PlaylistComponent::mouseUp(const juce::MouseEvent&)
{
    // Fire automation move undo callback BEFORE resetting state
    if (dragLaneIdx >= 0 && dragPointIdx >= 0 && project != nullptr
        && dragLaneIdx < (int)project->automationLanes.size())
    {
        auto& lane = project->automationLanes[(size_t)dragLaneIdx];
        if (dragPointIdx < (int)lane.points.size())
        {
            const AutomationPoint& after = lane.points[(size_t)dragPointIdx];
            if ((after.beat != dragStartAutoPt.beat || after.value != dragStartAutoPt.value)
                && onAutomationPointMoved)
                onAutomationPointMoved(dragLaneIdx, dragPointIdx, dragStartAutoPt, after);
        }
    }
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

    // Fade handle drag end
    if (fadeDraggingClipId >= 0)
    {
        if (auto* clip = findClipById(fadeDraggingClipId))
        {
            if (onClipFadeChanged)
                onClipFadeChanged(fadeDraggingClipId, clip->fadeInBars, clip->fadeOutBars);
        }
        fadeDraggingClipId = -1;
    }

    // Pattern slip drag end
    if (patSlipClipId >= 0)
    {
        if (auto* clip = findClipById(patSlipClipId))
        {
            if (onPatternSlipEdited && clip->patternStartOffsetBars != dragStartPatOffset)
                onPatternSlipEdited(patSlipClipId, dragStartPatOffset, clip->patternStartOffsetBars);
        }
        patSlipClipId = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    // Audio slip drag end
    if (slipDraggingClipId >= 0)
    {
        if (auto* clip = findClipById(slipDraggingClipId))
        {
            if (onClipSlipEdited && clip->sourceOffsetSamples != dragStartOffset)
                onClipSlipEdited(slipDraggingClipId, dragStartOffset, clip->sourceOffsetSamples);
        }
        slipDraggingClipId = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

inline void PlaylistComponent::mouseMove(const juce::MouseEvent& e)
{
    if (project == nullptr) return;
    const auto pos = e.getPosition();

    int newHoverLane  = -1;
    int newHoverPoint = -1;

    if (pos.y >= autoLanesY())
    {
        const int relY = pos.y - autoLanesY();
        const int li   = relY / autoLaneHeight;
        if (li >= 0 && li < (int)project->automationLanes.size())
        {
            const auto& lane  = project->automationLanes[(size_t)li];
            const int   laneY = autoLanesY() + li * autoLaneHeight;
            for (int pi = 0; pi < (int)lane.points.size(); ++pi)
            {
                const float ptPx = (float)trackHeaderWidth + (float)(lane.points[(size_t)pi].beat * 0.25 * barWidth);
                const float ptPy = (float)(laneY + autoLaneHeight - 4)
                                   - lane.points[(size_t)pi].value * (float)(autoLaneHeight - 8);
                const float dx   = (float)pos.x - ptPx;
                const float dy   = (float)pos.y - ptPy;
                if (std::sqrt(dx * dx + dy * dy) < 10.0f)
                {
                    newHoverLane  = li;
                    newHoverPoint = pi;
                    break;
                }
            }
            setMouseCursor(newHoverPoint >= 0 ? juce::MouseCursor::PointingHandCursor
                                              : juce::MouseCursor::CrosshairCursor);
        }
        else
        {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    if (newHoverLane != hoverLaneIdx || newHoverPoint != hoverPointIdx)
    {
        hoverLaneIdx  = newHoverLane;
        hoverPointIdx = newHoverPoint;
        repaint();
    }
}

inline void PlaylistComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (pos.y < headerHeight) return;

    // Double-click in automation lane → delete the hit breakpoint
    if (project != nullptr && pos.y >= autoLanesY())
    {
        const int relY = pos.y - autoLanesY();
        const int li   = relY / autoLaneHeight;
        if (li >= 0 && li < (int)project->automationLanes.size())
        {
            auto& lane  = project->automationLanes[(size_t)li];
            const int laneY = autoLanesY() + li * autoLaneHeight;
            for (int pi = 0; pi < (int)lane.points.size(); ++pi)
            {
                const float ptPx = (float)trackHeaderWidth + (float)(lane.points[(size_t)pi].beat * 0.25 * barWidth);
                const float ptPy = (float)(laneY + autoLaneHeight - 4)
                                   - lane.points[(size_t)pi].value * (float)(autoLaneHeight - 8);
                const float dx   = (float)pos.x - ptPx;
                const float dy   = (float)pos.y - ptPy;
                if (std::sqrt(dx * dx + dy * dy) < 10.0f)
                {
                    lane.points.erase(lane.points.begin() + pi);
                    repaint();
                    return;
                }
            }
        }
        return;  // double-click in lane but no point hit → do nothing
    }

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

    const float bar = juce::jmax(0.0f, snapBar(pixelToBar((float)pos.x)));

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

    // Audio clip properties (mode + pitch)
    if (clip->clipType == ClipType::Audio)
    {
        const juce::String modeStr =
            clip->audioClipMode == AudioClipMode::Stretch   ? "Stretch" :
            clip->audioClipMode == AudioClipMode::Elastique ? "Elastique" : "Resample";
        menu.addItem(6, "Properties  [" + modeStr + "  "
                        + juce::String(clip->pitchSemitone, 1) + " st]");
        menu.addSeparator();
    }

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

    // Reset Slip Offset (type-specific, enabled when offset differs from original)
    if (clip->clipType == ClipType::Audio)
    {
        const bool canReset = (clip->sourceOffsetSamples != clip->originalSourceOffsetSamples);
        menu.addItem(7, juce::String("Reset Slip Offset"), canReset);
    }
    else
    {
        const bool canReset = (clip->patternStartOffsetBars != clip->originalPatternStartOffsetBars);
        menu.addItem(8, juce::String("Reset Pattern Offset"), canReset);
    }

    menu.addSeparator();
    // "Split Here" — only meaningful when the cursor is strictly inside the clip
    const bool canSplit = (contextMenuBar_ > clip->startBar + 0.0625f) &&
                          (contextMenuBar_ < clip->startBar + clip->lengthBars - 0.0625f);
    menu.addItem(5, "Split Here", canSplit);
    menu.addSeparator();
    menu.addItem(9, clip->muted ? "Unmute Clip" : "Mute Clip");
    menu.addSeparator();
    menu.addItem(3, "Copy Clip");
    menu.addItem(4, "Detach to New Pattern", clip->patternId > 0);
    menu.addItem(2, "Delete");

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [this, clipId](int result)
        {
            if (result == 6)
            {
                showClipPropertiesDialog(clipId);
            }
            else if (result == 7)
            {
                // Reset audio slip offset to original
                if (auto* c = findClipById(clipId))
                {
                    const float prev = c->sourceOffsetSamples;
                    c->sourceOffsetSamples = c->originalSourceOffsetSamples;
                    if (onClipSlipEdited)
                        onClipSlipEdited(clipId, prev, c->sourceOffsetSamples);
                    repaint();
                }
            }
            else if (result == 8)
            {
                // Reset pattern slip offset to original
                if (auto* c = findClipById(clipId))
                {
                    const float prev = c->patternStartOffsetBars;
                    c->patternStartOffsetBars = c->originalPatternStartOffsetBars;
                    if (onPatternSlipEdited)
                        onPatternSlipEdited(clipId, prev, c->patternStartOffsetBars);
                    repaint();
                }
            }
            else if (result == 1)
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
            else if (result == 5)
            {
                // Split clip at contextMenuBar_
                if (auto* c = findClipById(clipId))
                {
                    const float splitBar    = contextMenuBar_;
                    const float origEnd     = c->startBar + c->lengthBars;
                    const float newALen     = splitBar - c->startBar;
                    const float newBLen     = origEnd  - splitBar;

                    // Compute a fresh ID
                    int newId = 1;
                    for (const auto& cl : clipList()) newId = juce::jmax(newId, cl.id + 1);

                    // Clip B — right half
                    PlaylistClip clipB  = *c;
                    clipB.id            = newId;
                    clipB.startBar      = splitBar;
                    clipB.lengthBars    = newBLen;

                    // Shorten clip A (direct mutation; undo is handled by the host
                    // via the existing onClipResized path if needed)
                    const float oldLen = c->lengthBars;
                    if (onClipResized) onClipResized(clipId, oldLen, newALen);
                    else               c->lengthBars = newALen;

                    // Register clip B
                    if (onClipAdded) onClipAdded(clipB);

                    // If audio clip, also load it into the engine
                    if (clipB.clipType == ClipType::Audio &&
                        clipB.audioFilePath.isNotEmpty() &&
                        onAudioClipDropped)
                        onAudioClipDropped(clipB.id, clipB.audioFilePath);

                    repaint();
                }
            }
            else if (result == 9)
            {
                if (auto* c = findClipById(clipId))
                {
                    c->muted = !c->muted;
                    if (onClipMuteToggled) onClipMuteToggled(clipId, c->muted);
                    repaint();
                }
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

inline void PlaylistComponent::showClipPropertiesDialog(int clipId)
{
    PlaylistClip* clip = findClipById(clipId);
    if (clip == nullptr || clip->clipType != ClipType::Audio) return;

    struct PropertiesPanel : public juce::Component
    {
        juce::Label        modeLabel, pitchLabel;
        juce::ComboBox     modeBox;
        juce::Slider       pitchSlider;

        explicit PropertiesPanel(AudioClipMode currentMode, float currentPitch)
        {
            // Mode row
            modeLabel.setText("Mode", juce::dontSendNotification);
            modeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0b0b8));
            addAndMakeVisible(modeLabel);

            modeBox.addItem("Resample",  1);
            modeBox.addItem("Stretch",   2);
            modeBox.addItem("Elastique", 3);
            modeBox.setSelectedId((int)currentMode + 1, juce::dontSendNotification);
            addAndMakeVisible(modeBox);

            // Pitch row
            pitchLabel.setText("Pitch (st)", juce::dontSendNotification);
            pitchLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0b0b8));
            addAndMakeVisible(pitchLabel);

            pitchSlider.setSliderStyle(juce::Slider::LinearHorizontal);
            pitchSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 20);
            pitchSlider.setRange(-24.0, 24.0, 0.5);
            pitchSlider.setValue((double)currentPitch, juce::dontSendNotification);
            addAndMakeVisible(pitchSlider);

            setSize(300, 80);
        }

        void resized() override
        {
            modeLabel  .setBounds(4,   4, 68, 20);
            modeBox    .setBounds(76,  4, getWidth() - 84, 20);
            pitchLabel .setBounds(4,  32, 68, 20);
            pitchSlider.setBounds(76, 30, getWidth() - 84, 26);
        }
    };

    const float         oldPitch = clip->pitchSemitone;
    const AudioClipMode oldMode  = clip->audioClipMode;

    auto* panel = new PropertiesPanel(oldMode, oldPitch);

    panel->modeBox.onChange = [this, clipId, oldMode, panel]
    {
        if (auto* c = findClipById(clipId))
        {
            const AudioClipMode newMode = (AudioClipMode)(panel->modeBox.getSelectedId() - 1);
            c->audioClipMode = newMode;
            if (onClipModeChanged) onClipModeChanged(clipId, oldMode, newMode);
            repaint();
        }
    };

    panel->pitchSlider.onValueChange = [this, clipId, oldPitch, panel]
    {
        if (auto* c = findClipById(clipId))
        {
            const float newPitch = (float)panel->pitchSlider.getValue();
            c->pitchSemitone = newPitch;
            if (onClipPitchChanged) onClipPitchChanged(clipId, oldPitch, newPitch);
            repaint();
        }
    };

    juce::CallOutBox::launchAsynchronously(
        std::unique_ptr<juce::Component>(panel),
        getScreenBounds(),
        nullptr);
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

// ---------------------------------------------------------------------------
// Audio drag & drop helpers
// ---------------------------------------------------------------------------
static bool isAudioFile(const juce::String& path)
{
    const juce::String ext = juce::File(path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".mp3" || ext == ".flac";
}

inline void PlaylistComponent::handleAudioFileDrop(const juce::String& filePath, int dropX, int dropY)
{
    if (project == nullptr) return;
    if (!isAudioFile(filePath)) return;
    const juce::File file(filePath);
    if (!file.existsAsFile()) return;

    // Compute bar position and track index from drop coordinates
    const float snappedBar = juce::jmax(0.0f, snapBar(pixelToBar((float)dropX)));
    const int   trackIdx  = juce::jlimit(0, getTrackCount() - 1, trackIndexAt(dropY));

    // Estimate length in bars from file duration
    float lengthBars = 4.0f;
    {
        juce::AudioFormatManager fmt;
        fmt.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fmt.createReaderFor(file));
        if (reader != nullptr && reader->sampleRate > 0.0)
        {
            const double bpm   = (project != nullptr) ? project->bpm : 120.0;
            const double secs  = (double)reader->lengthInSamples / reader->sampleRate;
            const double beats = secs * (bpm / 60.0);
            lengthBars = (float)(beats / 4.0);
        }
    }
    lengthBars = juce::jmax(0.0625f, lengthBars);  // 1/16 bar minimum, never force shorter samples longer

    // Allocate new clip ID
    int newId = 1;
    for (const auto& c : clipList()) newId = juce::jmax(newId, c.id + 1);

    PlaylistClip clip;
    clip.id            = newId;
    clip.clipType      = ClipType::Audio;
    clip.audioFilePath = filePath;
    clip.startBar      = snappedBar;
    clip.lengthBars    = lengthBars;
    clip.trackIndex    = trackIdx;
    clip.name          = file.getFileNameWithoutExtension();
    clip.patternId     = -1;

    if (onClipAdded) onClipAdded(clip);
    if (onAudioClipDropped) onAudioClipDropped(clip.id, filePath);
    repaint();
}

inline bool PlaylistComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFile(f)) return true;
    return false;
}

inline void PlaylistComponent::fileDragEnter(const juce::StringArray&, int x, int y)
{
    audioDragHover_     = true;
    audioDragHoverX_    = (float)x;
    audioDragHoverTrack_ = trackIndexAt(y);
    repaint();
}

inline void PlaylistComponent::fileDragExit(const juce::StringArray&)
{
    audioDragHover_ = false;
    repaint();
}

inline void PlaylistComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    audioDragHover_ = false;
    for (const auto& f : files)
    {
        if (isAudioFile(f))
        {
            handleAudioFileDrop(f, x, y);
            break;   // one file per drop for now
        }
    }
}

inline bool PlaylistComponent::isInterestedInDragSource(
    const juce::DragAndDropTarget::SourceDetails& details)
{
    return isAudioFile(details.description.toString());
}

inline void PlaylistComponent::itemDropped(const juce::DragAndDropTarget::SourceDetails& details)
{
    audioDragHover_ = false;
    handleAudioFileDrop(details.description.toString(),
                        details.localPosition.x,
                        details.localPosition.y);
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
    if (onAutomationLaneAdded) onAutomationLaneAdded(lane);
    repaint();
}

inline void PlaylistComponent::removeAutomationLane(int laneIdx)
{
    if (project == nullptr) return;
    if (laneIdx < 0 || laneIdx >= (int)project->automationLanes.size()) return;
    const AutomationLane removed = project->automationLanes[(size_t)laneIdx];
    project->automationLanes.erase(project->automationLanes.begin() + laneIdx);
    if (onAutomationLaneRemoved) onAutomationLaneRemoved(laneIdx, removed);
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

        // Snap grid lines — bar lines strong, subdivisions progressively lighter
        {
            const int snapDiv = static_cast<int>(autoSnapRes); // 1,2,4,8,16
            for (int bar = 0; bar < totalBars; ++bar)
            {
                // Bar line (always)
                const float bx = (float)(trackHeaderWidth + bar * barWidth);
                g.setColour(juce::Colour(0xff2c2c48));
                g.drawLine(bx, (float)(laneY + 14), bx, (float)(laneY + autoLaneHeight), 0.8f);

                // Subdivision lines
                for (int sub = 1; sub < snapDiv; ++sub)
                {
                    const float sx = bx + (float)sub / (float)snapDiv * (float)barWidth;
                    // Brightness by subdivision level: count trailing-2 factors of sub
                    int s = sub, level = 0;
                    while (s % 2 == 0) { s /= 2; ++level; }
                    static constexpr float alphas[] = { 0.10f, 0.16f, 0.24f, 0.32f };
                    const float alpha = alphas[juce::jlimit(0, 3, level)];
                    g.setColour(juce::Colours::white.withAlpha(alpha * 0.6f));
                    g.drawLine(sx, (float)(laneY + 14), sx, (float)(laneY + autoLaneHeight), 0.4f);
                }
            }
            // Final bar line at right edge
            const float lastBx = (float)(trackHeaderWidth + totalBars * barWidth);
            g.setColour(juce::Colour(0xff2c2c48));
            g.drawLine(lastBx, (float)(laneY + 14), lastBx, (float)(laneY + autoLaneHeight), 0.8f);
        }

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

        // Breakpoints (circles) — larger hit area reflected by 10px radius visually
        for (int pi = 0; pi < (int)lane.points.size(); ++pi)
        {
            const auto& pt = lane.points[(size_t)pi];
            const float px = (float)trackHeaderWidth + (float)(pt.beat * 0.25 * barWidth);
            const float py = (float)(laneY + autoLaneHeight - 4)
                             - pt.value * (float)(autoLaneHeight - 8);
            const bool dragging = (dragLaneIdx == li && dragPointIdx == pi);
            const bool hovering = (hoverLaneIdx == li && hoverPointIdx == pi);

            // Outer glow for hover
            if (hovering && !dragging)
            {
                g.setColour(col.brighter(0.6f).withAlpha(0.25f));
                g.fillEllipse(px - 8.0f, py - 8.0f, 16.0f, 16.0f);
            }

            const juce::Colour fillCol = dragging ? juce::Colours::white
                                       : hovering ? col.brighter(0.9f)
                                       : col.brighter(0.4f);
            g.setColour(fillCol);
            g.fillEllipse(px - 5.0f, py - 5.0f, 10.0f, 10.0f);
            g.setColour(hovering ? juce::Colours::white.withAlpha(0.7f) : col.darker(0.3f));
            g.drawEllipse(px - 5.0f, py - 5.0f, 10.0f, 10.0f, 1.2f);
        }
    }
}
