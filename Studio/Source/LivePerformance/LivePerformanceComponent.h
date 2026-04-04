#pragma once
#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"
#include "ClipLauncher.h"

/**  Live Performance Debug Panel
 *
 *  Developer-only readout: transport, per-channel loop-recorder state,
 *  clip launcher state, and real-time CC volume values.
 *
 *  All data is read from safe getters on AudioEngine / ClipLauncher — no
 *  audio-thread access, no locks.  Repaints at 30 Hz via juce::Timer.
 *
 *  Layout (top to bottom):
 *    [Transport]      beat pos, bar, BPM, play state
 *    [Channels 0-7]   recorder state + CC volume
 *    [Clips 0-7]      ClipLauncher pad states
 *    [Recording]      which channels are actively recording/overdubbing
 */
class LivePerformanceComponent : public juce::Component,
                                  private juce::Timer
{
public:
    LivePerformanceComponent(AudioEngine& engine, ClipLauncher& launcher)
        : engine_(engine), launcher_(launcher)
    {
        setSize(320, 480);
        startTimerHz(30);
    }

    ~LivePerformanceComponent() override { stopTimer(); }

    // -------------------------------------------------------------------------
    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        // Background
        g.fillAll(juce::Colour(0xff1a1a1a));

        const float rowH   = 18.0f;
        const float indent = 8.0f;
        float y            = 4.0f;
        const float w      = bounds.getWidth();

        auto drawHeader = [&](const char* title)
        {
            g.setColour(juce::Colour(0xff888888));
            g.drawHorizontalLine((int)(y + rowH * 0.5f), indent, w - indent);
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.setColour(juce::Colour(0xffaaaaaa));
            g.drawText(title, (int)indent + 4, (int)y, 200, (int)rowH, juce::Justification::centredLeft, false);
            y += rowH;
        };

        auto drawRow = [&](const juce::String& label, const juce::String& value,
                           juce::Colour valueColour = juce::Colour(0xffdddddd))
        {
            g.setFont(juce::FontOptions(12.0f));
            g.setColour(juce::Colour(0xff888888));
            g.drawText(label, (int)indent, (int)y, 130, (int)rowH, juce::Justification::centredLeft, false);
            g.setColour(valueColour);
            g.drawText(value, (int)indent + 132, (int)y, (int)(w - indent - 136), (int)rowH,
                       juce::Justification::centredLeft, false);
            y += rowH;
        };

        // draws a small filled square indicator then text
        auto drawIndicator = [&](juce::Colour dot, const juce::String& label, const juce::String& value)
        {
            g.setColour(dot);
            g.fillRect(juce::Rectangle<float>(indent, y + (rowH - 10.0f) * 0.5f, 10.0f, 10.0f));
            g.setFont(juce::FontOptions(12.0f));
            g.setColour(juce::Colour(0xffcccccc));
            g.drawText(label, (int)indent + 14, (int)y, 110, (int)rowH, juce::Justification::centredLeft, false);
            g.setColour(juce::Colour(0xffdddddd));
            g.drawText(value, (int)indent + 126, (int)y, (int)(w - indent - 130), (int)rowH,
                       juce::Justification::centredLeft, false);
            y += rowH;
        };

        // ── Transport ────────────────────────────────────────────────────────
        drawHeader("-- TRANSPORT --");

        const bool  playing  = engine_.isPlaying();
        const double beatPos = engine_.getPatternBeatPos();
        const double bpm     = engine_.getBPM();
        const int    bar     = (int)(beatPos / 4.0) + 1;
        const double beat    = std::fmod(beatPos, 4.0) + 1.0;

        drawRow("State",
                playing ? "PLAYING" : "STOPPED",
                playing ? juce::Colour(0xff44ff44) : juce::Colour(0xff888888));
        drawRow("BPM",    juce::String(bpm, 1));
        drawRow("Bar",    juce::String(bar));
        drawRow("Beat",   juce::String(beat, 2));
        drawRow("BeatPos",juce::String(beatPos, 3));

        y += 4.0f;

        // ── Channel Status (0-7) ─────────────────────────────────────────────
        drawHeader("-- CHANNELS (0-7) --");

        for (int ch = 0; ch < 8; ++ch)
        {
            const auto state  = engine_.liveLoopGetState(ch);
            const float ccVol = engine_.getCcChannelVolume(ch);

            juce::Colour dot;
            juce::String stateStr;
            switch (state)
            {
                case LiveLoopEngine::State::Recording:    dot = juce::Colour(0xffff4444); stateStr = "RECORDING";  break;
                case LiveLoopEngine::State::Looping:      dot = juce::Colour(0xff44ff44); stateStr = "LOOPING";    break;
                case LiveLoopEngine::State::Armed:        dot = juce::Colour(0xffff44ff); stateStr = "ARMED";      break;
                case LiveLoopEngine::State::WaitingForBar:dot = juce::Colour(0xffffff44); stateStr = "WAIT-BAR";   break;
                default:                                  dot = juce::Colour(0xff444444); stateStr = "IDLE";       break;
            }

            const juce::String label = "Ch" + juce::String(ch);
            const juce::String info  = stateStr + "  vol:" + juce::String(ccVol, 2);
            drawIndicator(dot, label, info);
        }

        y += 4.0f;

        // ── Clip (Pad) Status (0-7) ──────────────────────────────────────────
        drawHeader("-- CLIPS / PADS (0-7) --");

        for (int pad = 0; pad < 8; ++pad)
        {
            const ClipState cs = launcher_.getClipState(pad);

            juce::Colour   dot;
            juce::String   stateStr;
            switch (cs)
            {
                case ClipState::Playing:  dot = juce::Colour(0xff44ff44); stateStr = "PLAYING"; break;
                case ClipState::Queued:   dot = juce::Colour(0xffffff44); stateStr = "QUEUED";  break;
                case ClipState::Stopping: dot = juce::Colour(0xffff8800); stateStr = "STOPPING";break;
                default:                  dot = juce::Colour(0xff444444); stateStr = "IDLE";    break;
            }

            drawIndicator(dot, "Pad" + juce::String(pad), stateStr);
        }

        y += 4.0f;

        // ── Active Recording Summary ─────────────────────────────────────────
        drawHeader("-- ACTIVE RECORDING --");

        bool anyRecording = false;
        for (int ch = 0; ch < 16; ++ch)
        {
            const auto s = engine_.liveLoopGetState(ch);
            if (s == LiveLoopEngine::State::Recording || s == LiveLoopEngine::State::Looping || s == LiveLoopEngine::State::Armed)
            {
                const char* tag = (s == LiveLoopEngine::State::Recording) ? "REC"
                                : (s == LiveLoopEngine::State::Looping)   ? "LOOP"
                                                                           : "ARM";
                const double len = engine_.liveLoopGetLength();
                drawRow("Ch" + juce::String(ch) + " " + tag,
                        juce::String(len, 0) + " beats (" + juce::String((int)(len/4)) + " bars)",
                        juce::Colour(0xffff6666));
                anyRecording = true;
            }
        }
        if (!anyRecording)
            drawRow("none", "--", juce::Colour(0xff666666));

        y += 4.0f;

        // ── CC Volume Quick View (all 8 channels) ────────────────────────────
        drawHeader("-- CC VOLUME (ch 0-7) --");

        const float barMaxW = w - indent * 2.0f - 40.0f;
        for (int ch = 0; ch < 8; ++ch)
        {
            const float vol = engine_.getCcChannelVolume(ch);

            // Label
            g.setFont(juce::FontOptions(11.0f));
            g.setColour(juce::Colour(0xff888888));
            g.drawText("Ch" + juce::String(ch),
                       (int)indent, (int)y, 28, (int)rowH, juce::Justification::centredLeft, false);

            // Bar
            const float barW = vol * barMaxW;
            g.setColour(juce::Colour(0xff334433));
            g.fillRect(juce::Rectangle<float>(indent + 30.0f, y + 3.0f, barMaxW, rowH - 6.0f));
            g.setColour(juce::Colour(0xff44cc44));
            g.fillRect(juce::Rectangle<float>(indent + 30.0f, y + 3.0f, barW,    rowH - 6.0f));

            // Numeric
            g.setColour(juce::Colour(0xffcccccc));
            g.drawText(juce::String(vol, 2),
                       (int)(indent + 30.0f + barMaxW + 2.0f), (int)y, 38, (int)rowH,
                       juce::Justification::centredLeft, false);

            y += rowH;
        }
    }

    void resized() override {}

private:
    void timerCallback() override { repaint(); }

    AudioEngine&  engine_;
    ClipLauncher& launcher_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LivePerformanceComponent)
};

/**  Thin DocumentWindow wrapper so the panel can float as a separate window. */
class LivePerformanceWindow : public juce::DocumentWindow
{
public:
    LivePerformanceWindow(AudioEngine& engine, ClipLauncher& launcher)
        : juce::DocumentWindow("Live Performance Debug",
                               juce::Colour(0xff1a1a1a),
                               juce::DocumentWindow::closeButton)
        , panel_(engine, launcher)
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&panel_, true);
        setResizable(true, false);
        centreWithSize(panel_.getWidth(), panel_.getHeight());
    }

    void closeButtonPressed() override { setVisible(false); }

private:
    LivePerformanceComponent panel_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LivePerformanceWindow)
};
