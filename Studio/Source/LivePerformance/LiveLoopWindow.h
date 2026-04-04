#pragma once
#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"
#include "LiveLoopEngine.h"

// ─── Custom LookAndFeel for rounded pill buttons ──────────────────────────────
class LiveLoopLAF : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                              const juce::Colour& bg, bool over, bool down) override
    {
        const float r = (float)btn.getHeight() * 0.42f;
        const auto  b = btn.getLocalBounds().toFloat().reduced(0.5f);
        auto col = bg;
        if (down) col = col.darker(0.25f);
        else if (over) col = col.brighter(0.12f);
        g.setColour(col);
        g.fillRoundedRectangle(b, r);
        g.setColour(col.brighter(0.18f).withAlpha(0.5f));
        g.drawRoundedRectangle(b, r, 0.8f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& btn,
                        bool /*over*/, bool /*down*/) override
    {
        g.setColour(btn.findColour(juce::TextButton::textColourOffId));
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        g.drawFittedText(btn.getButtonText(), btn.getLocalBounds(),
                         juce::Justification::centred, 1);
    }
};

// ─── LiveLoopComponent ────────────────────────────────────────────────────────
class LiveLoopComponent : public juce::Component,
                          public juce::Timer
{
public:
    std::function<void(int)>               onChannelSelected;
    std::function<void(int)>               onArmChannel;
    std::function<void(int)>               onStopChannel;
    std::function<void(int, double)>       onSetLoopLength;
    std::function<void()>                  onStopAll;
    std::function<void(int)>              onEditInstrument;
    std::function<void(double)>            onBpmChanged;       // called with new BPM value
    std::function<void(double)>            onQuantizeChanged;  // called with step in beats (0=free)

    std::function<int()>                   getSelectedChannel;
    std::function<juce::String(int)>       getChannelName;
    std::function<juce::String(int)>       getInstrumentName;
    std::function<double()>                getCurrentBeat;
    std::function<double()>                getBpm;

    AudioEngine& engine_;

    explicit LiveLoopComponent(AudioEngine& engine) : engine_(engine)
    {
        setSize(620, kHeaderH + kNumCh * kRowH + kGridH + kFooterH);
        setLookAndFeel(&laf_);

        // Big REC button
        recBtn_.setButtonText("REC");
        styleBtn(recBtn_, kRed);
        recBtn_.onClick = [this]
        {
            const int ch = sel();
            const auto st = engine_.liveLoopGetState(ch);
            if (st == LiveLoopEngine::State::Idle) { if (onArmChannel)  onArmChannel(ch); }
            else                                   { if (onStopChannel) onStopChannel(ch); }
        };
        addAndMakeVisible(recBtn_);

        stopBtn_.setButtonText("STOP");
        styleBtn(stopBtn_, juce::Colour(0xff2a2a2a));
        stopBtn_.onClick = [this] { if (onStopChannel) onStopChannel(sel()); };
        addAndMakeVisible(stopBtn_);

        stopAllBtn_.setButtonText("STOP ALL");
        styleBtn(stopAllBtn_, juce::Colour(0xff1a1a1a));
        stopAllBtn_.onClick = [this] { if (onStopAll) onStopAll(); };
        addAndMakeVisible(stopAllBtn_);

        // BPM − / + buttons
        auto setupTiny = [this](juce::TextButton& b, const juce::String& txt, std::function<void()> fn)
        {
            b.setButtonText(txt);
            b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff28283a));
            b.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.75f));
            b.onClick = fn;
            addAndMakeVisible(b);
        };
        setupTiny(bpmMinusBtn_, "-", [this]
        {
            const double cur = getBpm ? getBpm() : 120.0;
            if (onBpmChanged) onBpmChanged(juce::jmax(20.0, cur - 5.0));
        });
        setupTiny(bpmPlusBtn_,  "+", [this]
        {
            const double cur = getBpm ? getBpm() : 120.0;
            if (onBpmChanged) onBpmChanged(juce::jmin(300.0, cur + 5.0));
        });

        // Quantize toggle – default 1/16
        styleBtn(quantBtn_, kAccent.darker(0.2f));
        quantBtn_.setButtonText("Q:1/16");
        quantBtn_.onClick = [this]
        {
            quantIdx_ = (quantIdx_ + 1) % 4;
            const bool active = kQuantSteps[quantIdx_] > 0.0;
            quantBtn_.setButtonText(juce::String("Q:") + kQuantLabels[quantIdx_]);
            quantBtn_.setColour(juce::TextButton::buttonColourId,
                                active ? kAccent.darker(0.2f) : juce::Colour(0xff1e1e2e));
            if (onQuantizeChanged) onQuantizeChanged(kQuantSteps[quantIdx_]);
        };
        addAndMakeVisible(quantBtn_);

        // Per-channel rows
        for (int c = 0; c < kNumCh; ++c)
        {
            auto& row = rows_[c];

            row.sel.setButtonText("");
            row.sel.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            row.sel.onClick = [this, c] { if (onChannelSelected) onChannelSelected(c); };
            addAndMakeVisible(row.sel);

            const double lens[] = { 4.0, 8.0, 16.0, 32.0 };
            const char*  labs[] = { "1", "2", "4", "8" };
            for (int i = 0; i < 4; ++i)
            {
                styleBtn(row.len[i], juce::Colour(0xff222232));
                row.len[i].setButtonText(labs[i]);
                const double lv = lens[i];
                row.len[i].onClick = [this, c, lv]
                {
                    loopLen_[c] = lv;
                    engine_.liveLoopSetLength(lv);
                    if (onSetLoopLength) onSetLoopLength(c, lv);
                    refreshLenButtons(c);
                };
                addAndMakeVisible(row.len[i]);
            }
            loopLen_[c] = 16.0;
            refreshLenButtons(c);
        }

        startTimerHz(24);
    }

    ~LiveLoopComponent() override { stopTimer(); setLookAndFeel(nullptr); }

    void timerCallback() override { repaint(); syncRecBtn(); }

    // ─── paint ───────────────────────────────────────────────────────────────
    void paint(juce::Graphics& g) override
    {
        // Base
        g.fillAll(kBg);

        // Header
        g.setColour(kHeaderBg);
        g.fillRoundedRectangle(0.0f, 0.0f, (float)getWidth(), (float)kHeaderH, 0.0f);

        paintTransport(g);

        // Track rows
        const int selCh = sel();
        for (int c = 0; c < kNumCh; ++c)
            paintRow(g, c, c == selCh);

        // Note grid panel
        paintNoteGrid(g);

        // Footer gradient
        g.setColour(kFooterBg);
        g.fillRect(0, footerY(), getWidth(), kFooterH);
        g.setColour(juce::Colour(0xff333333));
        g.drawHorizontalLine(footerY(), 0.0f, (float)getWidth());
    }

    void resized() override
    {
        for (int c = 0; c < kNumCh; ++c)
        {
            const int ry = rowY(c);
            rows_[c].sel.setBounds(0, ry, getWidth(), kRowH);

            // Len buttons – right-aligned, 4 buttons of 26px with 4px gap
            const int lenW = 26, gap = 4;
            const int lenTotalW = 4 * lenW + 3 * gap;
            const int lenX = getWidth() - lenTotalW - 12;
            for (int i = 0; i < 4; ++i)
                rows_[c].len[i].setBounds(lenX + i * (lenW + gap),
                                          ry + (kRowH - 20) / 2, lenW, 20);
        }

        // Footer buttons
        const int fy = footerY() + 10;
        const int fh = kFooterH - 20;
        const int fw = getWidth();
        recBtn_    .setBounds(12,            fy, fw / 3 - 16,   fh);
        stopBtn_   .setBounds(fw / 3,        fy, fw / 3 - 8,    fh);
        stopAllBtn_.setBounds(fw * 2 / 3 - 4,fy, fw / 3 - 8,   fh);

        // Header BPM ±  and quantize buttons
        const int hby = (kHeaderH - 18) / 2;
        bpmMinusBtn_.setBounds(70,  hby, 18, 18);
        bpmPlusBtn_ .setBounds(90,  hby, 18, 18);
        quantBtn_   .setBounds(220, (kHeaderH - 20) / 2, 68, 20);
    }

private:
    // ─── colours ─────────────────────────────────────────────────────────────
    const juce::Colour kBg        { 0xff141418 };
    const juce::Colour kHeaderBg  { 0xff1a1a24 };
    const juce::Colour kFooterBg  { 0xff111114 };
    const juce::Colour kRowEven   { 0xff181820 };
    const juce::Colour kRowOdd    { 0xff161618 };
    const juce::Colour kRowSel    { 0xff1e2040 };
    const juce::Colour kAccent    { 0xff4466ff };
    const juce::Colour kRed       { 0xffcc2233 };
    const juce::Colour kGreen     { 0xff22cc55 };
    const juce::Colour kYellow    { 0xffddcc00 };
    const juce::Colour kPurple    { 0xffaa33dd };

    // ─── layout ──────────────────────────────────────────────────────────────
    static constexpr int kNumCh   = 8;
    static constexpr int kHeaderH = 48;
    static constexpr int kRowH    = 52;
    static constexpr int kGridH   = 110;  // note grid panel height
    static constexpr int kFooterH = 68;

    int rowY(int c) const    { return kHeaderH + c * kRowH; }
    int gridY() const        { return kHeaderH + kNumCh * kRowH; }
    int footerY() const      { return gridY() + kGridH; }
    int sel() const          { return getSelectedChannel ? getSelectedChannel() : 0; }

    // ─── subcomponents ───────────────────────────────────────────────────────
    struct Row { juce::TextButton sel, len[4]; };
    Row             rows_[kNumCh];
    juce::TextButton recBtn_, stopBtn_, stopAllBtn_;
    juce::TextButton bpmMinusBtn_, bpmPlusBtn_, quantBtn_;
    double          loopLen_[kNumCh] {};
    LiveLoopLAF     laf_;

    // ─── quantize state ──────────────────────────────────────────────────────
    static constexpr const char*  kQuantLabels[4] = { "FREE", "1/4", "1/8", "1/16" };
    static constexpr double       kQuantSteps [4] = {  0.0,   1.0,   0.5,   0.25   };
    int    quantIdx_  = 3;  // default: 1/16

    // ─── helpers ─────────────────────────────────────────────────────────────
    void styleBtn(juce::TextButton& b, juce::Colour bg)
    {
        b.setColour(juce::TextButton::buttonColourId, bg);
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.9f));
    }

    void refreshLenButtons(int c)
    {
        const double lens[] = { 4.0, 8.0, 16.0, 32.0 };
        for (int i = 0; i < 4; ++i)
        {
            const bool on = (loopLen_[c] == lens[i]);
            rows_[c].len[i].setColour(juce::TextButton::buttonColourId,
                on ? kAccent.darker(0.1f) : juce::Colour(0xff222232));
            rows_[c].len[i].setColour(juce::TextButton::textColourOffId,
                on ? juce::Colours::white : juce::Colour(0xff888899));
        }
    }

    void syncRecBtn()
    {
        const auto st = engine_.liveLoopGetState(sel());
        if (st == LiveLoopEngine::State::Idle)
        {
            recBtn_.setButtonText("REC");
            recBtn_.setColour(juce::TextButton::buttonColourId, kRed);
        }
        else
        {
            recBtn_.setButtonText("STOP REC");
            recBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff882222));
        }
    }

    void paintTransport(juce::Graphics& g)
    {
        const double beat    = getCurrentBeat ? getCurrentBeat() : 0.0;
        const double bpmV    = getBpm         ? getBpm()         : 120.0;
        const int    bar     = (int)(beat / 4.0) + 1;
        const double bib     = std::fmod(beat, 4.0) + 1.0;

        // BPM label + value (buttons sit at x=70 and x=90, paint stays left)
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.setFont(juce::Font(10.0f));
        g.drawText("BPM", 12, 8, 30, 14, juce::Justification::centredLeft);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(22.0f, juce::Font::bold));
        g.drawText(juce::String((int)bpmV), 12, 20, 56, 22, juce::Justification::centredLeft);

        // Separator
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawVerticalLine(114, 10.0f, (float)kHeaderH - 10.0f);

        // Bar / Beat
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.setFont(juce::Font(10.0f));
        g.drawText("BAR", 122, 8, 30, 14, juce::Justification::centredLeft);
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(22.0f, juce::Font::bold));
        g.drawText(juce::String(bar), 122, 20, 44, 22, juce::Justification::centredLeft);

        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.setFont(juce::Font(10.0f));
        g.drawText("BEAT", 170, 8, 36, 14, juce::Justification::centredLeft);
        g.setColour(kAccent.withAlpha(0.9f));
        g.setFont(juce::Font(22.0f, juce::Font::bold));
        g.drawText(juce::String(bib, 1), 170, 20, 44, 22, juce::Justification::centredLeft);

        // Separator before quantize
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawVerticalLine(216, 10.0f, (float)kHeaderH - 10.0f);

        // Column headers (right side)
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.setFont(juce::Font(9.5f));
        g.drawText("BARS",  getWidth() - 120, 28, 108, 14, juce::Justification::centredRight);
    }

    // ─── single track row ────────────────────────────────────────────────────
    void paintRow(juce::Graphics& g, int c, bool selected)
    {
        const int y   = rowY(c);
        const auto st = engine_.liveLoopGetState(c);

        // Row background
        const juce::Colour rowBg = selected ? kRowSel
                                 : (c % 2 == 0 ? kRowEven : kRowOdd);
        g.setColour(rowBg);
        g.fillRect(0, y, getWidth(), kRowH);

        // Left accent stripe
        if (selected)
        {
            g.setColour(kAccent);
            g.fillRect(0, y, 3, kRowH);
        }

        // Bottom divider
        g.setColour(juce::Colour(0xff252528));
        g.drawHorizontalLine(y + kRowH - 1, 0.0f, (float)getWidth());

        // ── State dot ────────────────────────────────────────────────────────
        const auto [dotCol, stateLabel] = stateStyle(st);
        auto dot = dotCol;
        // Blink armed / recording
        if (st == LiveLoopEngine::State::Armed || st == LiveLoopEngine::State::Recording)
        {
            const double t = juce::Time::getMillisecondCounterHiRes() / 550.0;
            if (std::fmod(t, 1.0) > 0.5) dot = dot.withAlpha(0.25f);
        }

        const float cx = 18.0f, cy = y + kRowH * 0.5f;
        // Glow
        if (st != LiveLoopEngine::State::Idle)
        {
            g.setColour(dot.withAlpha(0.15f));
            g.fillEllipse(cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
        }
        g.setColour(dot);
        g.fillEllipse(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);

        // ── Channel number ────────────────────────────────────────────────────
        g.setColour(selected ? juce::Colours::white.withAlpha(0.5f)
                             : juce::Colours::white.withAlpha(0.28f));
        g.setFont(juce::Font(9.5f));
        g.drawText(juce::String::formatted("%02d", c + 1), 30, y, 22, kRowH,
                   juce::Justification::centredLeft);

        // ── Track name ────────────────────────────────────────────────────────
        const juce::String name  = getChannelName    ? getChannelName(c)    : "Track " + juce::String(c+1);
        const juce::String instr = getInstrumentName ? getInstrumentName(c) : "";
        g.setColour(selected ? juce::Colours::white : juce::Colours::white.withAlpha(0.72f));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(name, 56, y + 6, 120, 18, juce::Justification::centredLeft);
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.setFont(juce::Font(10.5f));
        g.drawText(instr, 56, y + 26, 120, 16, juce::Justification::centredLeft);

        // ── State badge ───────────────────────────────────────────────────────
        paintBadge(g, stateLabel, dotCol, 182, y + (kRowH - 18) / 2, 62, 18);

        // ── Loop progress bar ─────────────────────────────────────────────────
        if (st == LiveLoopEngine::State::Recording || st == LiveLoopEngine::State::Looping)
        {
            const double beat    = getCurrentBeat ? getCurrentBeat() : 0.0;
            const double loopLen = loopLen_[c];
            const double phase   = loopLen > 0.0 ? std::fmod(beat, loopLen) / loopLen : 0.0;

            const int bx = 182, bw = 62, bh = 3, bby = y + kRowH - 6;
            g.setColour(juce::Colour(0xff282830));
            g.fillRoundedRectangle((float)bx, (float)bby, (float)bw, (float)bh, 1.5f);
            g.setColour(dotCol.withAlpha(0.75f));
            g.fillRoundedRectangle((float)bx, (float)bby, (float)(bw * phase), (float)bh, 1.5f);
        }
    }

    // ─── state → colour + label ──────────────────────────────────────────────
    std::pair<juce::Colour, juce::String> stateStyle(LiveLoopEngine::State st) const
    {
        switch (st)
        {
            case LiveLoopEngine::State::Armed:        return { kPurple, "ARMED"   };
            case LiveLoopEngine::State::WaitingForBar:return { kYellow, "WAIT"    };
            case LiveLoopEngine::State::Recording:    return { kRed,    "REC"     };
            case LiveLoopEngine::State::Looping:      return { kGreen,  "LOOP"    };
            default:                                  return { juce::Colour(0xff404048), "IDLE" };
        }
    }

    // ─── note input grid ─────────────────────────────────────────────────────
    void paintNoteGrid(juce::Graphics& g)
    {
        const int selCh = sel();
        const auto st   = engine_.liveLoopGetState(selCh);

        // Panel background
        const int py = gridY();
        g.setColour(juce::Colour(0xff0e0e14));
        g.fillRect(0, py, getWidth(), kGridH);
        g.setColour(juce::Colour(0xff252530));
        g.drawHorizontalLine(py, 0.0f, (float)getWidth());

        const int gx = 10, gy = py + 8;
        const int gw = getWidth() - 20, gh = kGridH - 16;

        // Grid canvas background
        g.setColour(juce::Colour(0xff0a0a10));
        g.fillRoundedRectangle((float)gx, (float)gy, (float)gw, (float)gh, 4.0f);
        g.setColour(juce::Colour(0xff252530));
        g.drawRoundedRectangle((float)gx + 0.5f, (float)gy + 0.5f,
                               (float)gw - 1.0f, (float)gh - 1.0f, 4.0f, 0.6f);

        const double loopLen = engine_.liveLoopGetChannelLength(selCh);
        if (loopLen <= 0.0) return;

        // ── quantize grid lines (faint) ──
        if (quantIdx_ > 0)
        {
            const double qStep = kQuantSteps[quantIdx_];
            g.setColour(juce::Colour(0xff1e1e30));
            for (double b = qStep; b < loopLen; b += qStep)
            {
                const float x = gx + (float)(b / loopLen * gw);
                g.fillRect(x, (float)gy, 0.8f, (float)gh);
            }
        }

        // ── beat grid lines ──
        for (int b = 1; b <= (int)loopLen; ++b)
        {
            const float x = gx + (float)(b / loopLen * gw);
            const bool isBar = (b % 4 == 0);
            g.setColour(isBar ? juce::Colour(0xff353548) : juce::Colour(0xff252535));
            g.fillRect(x, (float)gy, isBar ? 1.2f : 0.7f, (float)gh);
        }

        // ── pitch lane lines (every octave) ──
        for (int oct = 1; oct < 7; ++oct)
        {
            const float y = gy + gh - (float)(oct / 7.0 * gh);
            g.setColour(juce::Colour(0xff1a1a28));
            g.drawHorizontalLine((int)y, (float)gx, (float)(gx + gw));
        }

        // ── recorded notes ──
        if (st != LiveLoopEngine::State::Idle)
        {
            LiveLoopEngine::NoteDisplayItem notes[512];
            const int nCount = engine_.liveLoopGetNotesForDisplay(selCh, notes, 512);

            for (int i = 0; i < nCount; ++i)
            {
                const auto& n = notes[i];
                const float x1 = gx + (float)(n.startBeat / loopLen * gw);
                float       x2 = gx + (float)(n.endBeat   / loopLen * gw);
                if (x2 <= x1) x2 = gx + gw;  // wrap-around notes extend to end
                const float fw = std::fmax(3.0f, x2 - x1);

                // pitch → vertical position: C2(36)–C8(96) range
                const float pitchNorm = juce::jlimit(0.0f, 1.0f, (n.pitch - 24) / 84.0f);
                const float ny = gy + gh - pitchNorm * gh - 3.0f;

                const auto col = kAccent.withBrightness(0.45f + n.velocity * 0.55f);
                g.setColour(col.withAlpha(0.8f));
                g.fillRoundedRectangle(x1, ny, fw, 5.0f, 2.0f);
            }
        }

        // ── playback cursor ──
        if (st == LiveLoopEngine::State::Looping || st == LiveLoopEngine::State::Recording)
        {
            const double phase = engine_.liveLoopGetPhase(selCh);
            const float  cx    = gx + (float)(phase / loopLen * gw);
            g.setColour(juce::Colours::white.withAlpha(0.75f));
            g.fillRect(cx, (float)gy, 1.5f, (float)gh);
        }

        // ── label ──
        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.setFont(juce::Font(9.0f));
        const juce::String label = "NOTE GRID  TRACK " + juce::String(selCh + 1)
                                 + "  |  " + juce::String(loopLen, 0) + " BEATS";
        g.drawText(label, gx + 6, gy + 3, gw - 12, 12, juce::Justification::centredLeft);

        // ── idle hint ──
        if (st == LiveLoopEngine::State::Idle || st == LiveLoopEngine::State::Armed)
        {
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.setFont(juce::Font(11.0f));
            g.drawText("Press REC then play notes to record", gx, gy, gw, gh,
                       juce::Justification::centred);
        }
    }

    // ─── pill-shaped badge ───────────────────────────────────────────────────
    void paintBadge(juce::Graphics& g, const juce::String& text,
                    juce::Colour col, int x, int y, int w, int h)
    {
        const float r = h * 0.4f;
        g.setColour(col.withAlpha(0.15f));
        g.fillRoundedRectangle((float)x, (float)y, (float)w, (float)h, r);
        g.setColour(col.withAlpha(0.55f));
        g.drawRoundedRectangle((float)x + 0.5f, (float)y + 0.5f,
                               (float)w - 1.0f, (float)h - 1.0f, r, 0.8f);
        g.setColour(col.brighter(0.3f));
        g.setFont(juce::Font(9.5f, juce::Font::bold));
        g.drawText(text, x, y, w, h, juce::Justification::centred);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveLoopComponent)
};

// ─── Window wrapper ───────────────────────────────────────────────────────────
class LiveLoopWindow : public juce::DocumentWindow
{
public:
    LiveLoopComponent content;

    explicit LiveLoopWindow(AudioEngine& engine)
        : juce::DocumentWindow("Live Loop",
                               juce::Colour(0xff141418),
                               juce::DocumentWindow::closeButton),
          content(engine)
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&content, true);
        setResizable(false, false);
        centreWithSize(content.getWidth(), content.getHeight());
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveLoopWindow)
};
