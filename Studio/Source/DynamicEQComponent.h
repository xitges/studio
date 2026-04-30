/*
  ==============================================================================

    DynamicEQComponent.h  —  Professional Dynamic EQ UI
    Frequency response canvas + draggable band handles + per-band parameter panel.
    Header-only inline implementation.

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "DynamicEQProcessor.h"

// ---------------------------------------------------------------------------
// Band colours (6 bands)
// ---------------------------------------------------------------------------
inline const juce::Colour kDynEQBandColours[6] =
{
    juce::Colour(0xff4c9fff),   // 1 — blue
    juce::Colour(0xff4cffaa),   // 2 — teal
    juce::Colour(0xffffc04c),   // 3 — amber
    juce::Colour(0xffff6b8a),   // 4 — rose
    juce::Colour(0xffb06bff),   // 5 — violet
    juce::Colour(0xff4ce8ff),   // 6 — cyan
};

// ---------------------------------------------------------------------------
// DynamicEQComponent
// ---------------------------------------------------------------------------
class DynamicEQComponent : public juce::Component,
                            public juce::Timer
{
public:
    DynamicEQComponent(DynamicEQProcessor& proc, const juce::String& trackName)
        : proc_(proc), trackName_(trackName)
    {
        for (int i = 0; i < DynamicEQProcessor::kMaxBands; ++i)
            params_[i] = proc_.getBand(i);

        // Enable toggle
        enableBtn_.setButtonText("EQ ON");
        enableBtn_.setToggleState(proc_.isEnabled(), juce::dontSendNotification);
        enableBtn_.setClickingTogglesState(true);
        enableBtn_.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xff3a6aaa));
        enableBtn_.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff2c2c2e));
        enableBtn_.onClick = [this] {
            proc_.setEnabled(enableBtn_.getToggleState());
        };
        addAndMakeVisible(enableBtn_);

        // Band select buttons
        for (int i = 0; i < DynamicEQProcessor::kMaxBands; ++i)
        {
            auto* btn = bandBtns_.add(new juce::TextButton(juce::String(i + 1)));
            btn->setClickingTogglesState(true);
            btn->setRadioGroupId(2001);
            btn->setColour(juce::TextButton::buttonOnColourId,
                           kDynEQBandColours[i].withAlpha(0.8f));
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2c2c2e));
            btn->onClick = [this, i] { selectBand(i); };
            addAndMakeVisible(btn);
        }
        bandBtns_[0]->setToggleState(true, juce::dontSendNotification);

        setupParamControls();
        updateParamControls();
        updateDynVisible();

        startTimerHz(30);
        setSize(700, 524);
    }

    ~DynamicEQComponent() override { stopTimer(); }

    void timerCallback() override
    {
        // Repaint canvas for real-time GR animation
        repaint(0, kHeaderH, getWidth(), kCanvasH);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff18181c));
        paintHeader(g);
        paintCanvas(g);
    }

    void resized() override
    {
        // Header row: enable button + band buttons
        enableBtn_.setBounds(8, 6, 60, 22);
        int bx = getWidth() - (DynamicEQProcessor::kMaxBands * 36 + 8);
        for (auto* b : bandBtns_) { b->setBounds(bx, 6, 32, 22); bx += 36; }

        layoutParamControls();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!inCanvas(e.y)) return;
        dragBand_ = hitTest(e.x, e.y);
        if (dragBand_ >= 0)
        {
            selectBand(dragBand_);
            dragStartFreq_ = params_[dragBand_].frequency;
            dragStartGain_ = params_[dragBand_].maxGainDb;
            dragStartX_    = e.x;
            dragStartY_    = e.y;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragBand_ < 0) return;
        auto& p = params_[dragBand_];

        // Horizontal → frequency, Vertical → gain
        const float newFreq = clampFreq(xToFreq((float)e.x));
        p.frequency = newFreq;

        if (p.type != DynEQFilterType::LowPass && p.type != DynEQFilterType::HighPass)
            p.maxGainDb = juce::jlimit(kMinDb, kMaxDb, yToGain((float)e.y));

        pushBand(dragBand_);
        updateParamControls();
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override { dragBand_ = -1; }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (!inCanvas(e.y)) return;
        // Enable the first free band at the clicked position
        for (int i = 0; i < DynamicEQProcessor::kMaxBands; ++i)
        {
            if (!params_[i].enabled)
            {
                params_[i].enabled   = true;
                params_[i].frequency = clampFreq(xToFreq((float)e.x));
                params_[i].maxGainDb = juce::jlimit(kMinDb, kMaxDb, yToGain((float)e.y));
                pushBand(i);
                selectBand(i);
                return;
            }
        }
    }

    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& w) override
    {
        // Scroll over a handle → adjust Q
        const int b = hitTest(e.x, e.y);
        if (b < 0 || !inCanvas(e.y)) return;
        params_[b].Q = juce::jlimit(0.1f, 10.0f, params_[b].Q + w.deltaY * 0.5f);
        pushBand(b);
        updateParamControls();
        repaint();
    }

private:
    static constexpr int   kHeaderH  = 34;
    static constexpr int   kCanvasH  = 280;
    static constexpr float kMinDb    = -18.0f;
    static constexpr float kMaxDb    =  18.0f;
    static constexpr float kMinHz    =  20.0f;
    static constexpr float kMaxHz    = 20000.0f;
    static constexpr float kDispSR   = 44100.0f;

    DynamicEQProcessor& proc_;
    juce::String        trackName_;
    DynEQBandParams     params_[DynamicEQProcessor::kMaxBands];
    int  selectedBand_  = 0;
    int  dragBand_      = -1;
    float dragStartFreq_ = 0, dragStartGain_ = 0;
    int   dragStartX_ = 0, dragStartY_ = 0;

    juce::TextButton                   enableBtn_;
    juce::OwnedArray<juce::TextButton> bandBtns_;

    // Static EQ params
    juce::ComboBox    typeBox_;
    juce::Slider      freqSlider_, gainSlider_, qSlider_;
    juce::Label       typeLabel_, freqLabel_, gainLabel_, qLabel_;
    juce::ToggleButton dynToggle_;

    // Dynamic params
    juce::Slider      threshSlider_, ratioSlider_, attackSlider_, releaseSlider_;
    juce::Label       threshLabel_,  ratioLabel_,  attackLabel_,  releaseLabel_;
    juce::ComboBox    dirBox_;
    juce::Label       dirLabel_;

    // ---- Coordinate helpers -----------------------------------------------

    bool  inCanvas(int y)  const { return y >= kHeaderH && y < kHeaderH + kCanvasH; }
    int   canvasW()        const { return getWidth(); }

    float freqToX(float f) const
    {
        return (float)canvasW() * std::log(f / kMinHz) / std::log(kMaxHz / kMinHz);
    }
    float xToFreq(float x) const
    {
        return kMinHz * std::pow(kMaxHz / kMinHz, x / (float)canvasW());
    }
    float gainToY(float db) const
    {
        return (float)kHeaderH + (kMaxDb - db) / (kMaxDb - kMinDb) * (float)kCanvasH;
    }
    float yToGain(float y) const
    {
        return kMaxDb - ((y - (float)kHeaderH) / (float)kCanvasH) * (kMaxDb - kMinDb);
    }
    float clampFreq(float f) const { return juce::jlimit(kMinHz, kMaxHz, f); }

    // ---- Handle hit test ------------------------------------------------

    int hitTest(int mx, int my) const
    {
        for (int i = DynamicEQProcessor::kMaxBands - 1; i >= 0; --i)
        {
            if (!params_[i].enabled) continue;
            const float hx = freqToX(params_[i].frequency);
            const float hy = gainToY(params_[i].maxGainDb);
            if ((mx-hx)*(mx-hx) + (my-hy)*(my-hy) < 100.0f) return i;
        }
        return -1;
    }

    // ---- Painting ---------------------------------------------------------

    void paintHeader(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff232328));
        g.fillRect(0, 0, getWidth(), kHeaderH);
        g.setColour(juce::Colour(0xff404050));
        g.drawLine(0.0f, (float)kHeaderH, (float)getWidth(), (float)kHeaderH, 1.0f);

        g.setColour(juce::Colour(0xffb0b0c0));
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)).boldened());
        g.drawText(juce::String::fromUTF8("Dynamic EQ  \xe2\x80\x94  ") + trackName_,
                   80, 0, 300, kHeaderH, juce::Justification::centredLeft);
    }

    void paintCanvas(juce::Graphics& g)
    {
        const int W = canvasW();
        const juce::Rectangle<int> r(0, kHeaderH, W, kCanvasH);

        g.setColour(juce::Colour(0xff0e0e12));
        g.fillRect(r);

        paintGrid(g, W);
        paintGRFill(g, W);
        paintCurve(g, W);
        paintHandles(g, W);

        g.setColour(juce::Colour(0xff404050));
        g.drawRect(r, 1);
    }

    void paintGrid(juce::Graphics& g, int W)
    {
        // Vertical frequency lines
        const float freqLines[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        for (float f : freqLines)
        {
            const int x = (int)freqToX(f);
            g.setColour(juce::Colour(0xff282830));
            g.drawVerticalLine(x, (float)kHeaderH, (float)(kHeaderH + kCanvasH));
        }

        // Horizontal dB lines
        for (float db : { -12.f, -6.f, 0.f, 6.f, 12.f })
        {
            const int y = (int)gainToY(db);
            g.setColour(db == 0.f ? juce::Colour(0xff464656) : juce::Colour(0xff282830));
            g.drawHorizontalLine(y, 0.0f, (float)W);
        }

        // Labels
        g.setColour(juce::Colour(0xff555568));
        g.setFont(juce::Font(juce::FontOptions().withHeight(9.0f)));
        const std::pair<float, const char*> fLbls[] =
            { {100,"100"},{200,"200"},{500,"500"},{1000,"1k"},{2000,"2k"},{5000,"5k"},{10000,"10k"} };
        for (auto [f, s] : fLbls)
            g.drawText(s, (int)freqToX(f)-12, kHeaderH + kCanvasH - 14, 24, 12,
                       juce::Justification::centred);
        for (float db : { -12.f, -6.f, 0.f, 6.f, 12.f })
            g.drawText(juce::String((int)db), 4, (int)gainToY(db)-6, 28, 12,
                       juce::Justification::left);
    }

    void paintGRFill(juce::Graphics& g, int W)
    {
        // Shade the dynamic gain reduction region per band
        for (int i = 0; i < DynamicEQProcessor::kMaxBands; ++i)
        {
            const auto& p = params_[i];
            if (!p.enabled || !p.dynamicOn) continue;
            const float gr = proc_.getGainReductionDb(i);
            if (gr < 0.1f) continue;

            const float cx    = freqToX(p.frequency);
            const float qSpan = (float)W * 0.025f * std::log10(juce::jmax(0.5f, p.Q) + 1.0f) * 3.0f;
            const float y0    = gainToY(p.maxGainDb);
            const float y1    = gainToY(p.maxGainDb - gr);

            juce::Path fill;
            fill.addRoundedRectangle(cx - qSpan, y0, qSpan * 2.0f,
                                     y1 - y0, 2.0f);
            g.setColour(kDynEQBandColours[i].withAlpha(0.14f));
            g.fillPath(fill);
        }
    }

    void paintCurve(juce::Graphics& g, int W)
    {
        juce::Path curve;
        bool started = false;

        for (int px = 0; px <= W; px += 2)
        {
            const float pf = xToFreq((float)px);
            const float w  = juce::MathConstants<float>::twoPi * pf / kDispSR;

            float totalDb = 0.0f;
            for (int i = 0; i < DynamicEQProcessor::kMaxBands; ++i)
            {
                const auto& p = params_[i];
                if (!p.enabled) continue;

                const float dispGain = p.dynamicOn
                    ? p.maxGainDb - proc_.getGainReductionDb(i)
                    : p.maxGainDb;

                totalDb += evalBandResponse(p, dispGain, w);
            }

            const float y = gainToY(juce::jlimit(kMinDb - 4.f, kMaxDb + 4.f, totalDb));
            if (!started) { curve.startNewSubPath((float)px, y); started = true; }
            else          { curve.lineTo((float)px, y); }
        }

        // Filled area under curve
        if (started)
        {
            juce::Path fill = curve;
            fill.lineTo((float)W, gainToY(0.0f));
            fill.lineTo(0.0f, gainToY(0.0f));
            fill.closeSubPath();
            g.setColour(juce::Colour(0xff4c9fff).withAlpha(0.05f));
            g.fillPath(fill);
        }

        g.setColour(juce::Colour(0xffb4b4d0));
        g.strokePath(curve, juce::PathStrokeType(1.5f));
    }

    void paintHandles(juce::Graphics& g, int W)
    {
        for (int i = 0; i < DynamicEQProcessor::kMaxBands; ++i)
        {
            if (!params_[i].enabled) continue;
            const float hx = freqToX(params_[i].frequency);
            const float hy = gainToY(params_[i].maxGainDb);
            const float r  = (i == selectedBand_) ? 7.5f : 5.5f;
            const bool  sel = (i == selectedBand_);

            // Outer glow for selected
            if (sel)
            {
                g.setColour(kDynEQBandColours[i].withAlpha(0.25f));
                g.fillEllipse(hx - r - 3, hy - r - 3, (r+3)*2, (r+3)*2);
            }

            g.setColour(kDynEQBandColours[i].withAlpha(sel ? 1.0f : 0.75f));
            g.fillEllipse(hx - r, hy - r, r*2, r*2);

            g.setColour(juce::Colours::white.withAlpha(sel ? 0.9f : 0.5f));
            g.drawEllipse(hx - r, hy - r, r*2, r*2, sel ? 1.5f : 1.0f);

            // Dynamic indicator dot
            if (params_[i].dynamicOn)
            {
                g.setColour(juce::Colour(0xffffcc44));
                g.fillEllipse(hx + r - 4, hy - r, 4, 4);
            }

            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.setFont(juce::Font(juce::FontOptions().withHeight(8.0f)).boldened());
            g.drawText(juce::String(i + 1), (int)(hx-5), (int)(hy-5), 10, 10,
                       juce::Justification::centred);
        }
        juce::ignoreUnused(W);
    }

    // ---- Frequency response evaluation ------------------------------------

    // Returns band's dB contribution at angular frequency w = 2π * pixelFreq / SR
    float evalBandResponse(const DynEQBandParams& p, float dispGain, float w) const
    {
        float b0, b1, b2, a1, a2;
        computeDisplayCoeffs(p, dispGain, b0, b1, b2, a1, a2);

        const float cosW  = std::cos(w),  sinW  = std::sin(w);
        const float cos2W = 2*cosW*cosW - 1.0f;
        const float sin2W = 2*sinW*cosW;

        const float nR = b0 + b1*cosW + b2*cos2W;
        const float nI =    - b1*sinW - b2*sin2W;
        const float dR = 1  + a1*cosW + a2*cos2W;
        const float dI =    - a1*sinW - a2*sin2W;

        const float den2 = dR*dR + dI*dI;
        if (den2 < 1e-12f) return 0.0f;
        return 10.0f * std::log10(juce::jmax(1e-12f, (nR*nR + nI*nI) / den2));
    }

    // RBJ biquad coefficients for display (mirrors DynamicEQProcessor formulas)
    void computeDisplayCoeffs(const DynEQBandParams& p, float gainDb,
                               float& b0, float& b1, float& b2,
                               float& a1, float& a2) const
    {
        const float f    = juce::jlimit(kMinHz, kDispSR * 0.499f, p.frequency);
        const float Q    = juce::jmax(0.1f, p.Q);
        const float w0   = juce::MathConstants<float>::twoPi * f / kDispSR;
        const float cosW = std::cos(w0), sinW = std::sin(w0);

        switch (p.type)
        {
            case DynEQFilterType::Bell:
            {
                const float A = std::pow(10.0f, gainDb / 40.0f);
                const float al = sinW / (2.0f * Q);
                const float inv = 1.0f / (1.0f + al / A);
                b0 = (1.0f + al*A)*inv;  b1 = -2*cosW*inv;
                b2 = (1.0f - al*A)*inv;  a1 = b1;  a2 = (1-al/A)*inv;
                break;
            }
            case DynEQFilterType::LowShelf:
            {
                const float A = std::pow(10.0f, gainDb / 40.0f);
                const float al = sinW*0.5f*std::sqrt((A+1.0f/A)*(1.0f/Q-1.0f)+2.0f);
                const float sq = std::sqrt(A);
                const float inv = 1.0f / ((A+1)+(A-1)*cosW+2*sq*al);
                b0 = A*((A+1)-(A-1)*cosW+2*sq*al)*inv;  b1 = 2*A*((A-1)-(A+1)*cosW)*inv;
                b2 = A*((A+1)-(A-1)*cosW-2*sq*al)*inv;  a1 = -2*((A-1)+(A+1)*cosW)*inv;
                a2 = ((A+1)+(A-1)*cosW-2*sq*al)*inv;
                break;
            }
            case DynEQFilterType::HighShelf:
            {
                const float A = std::pow(10.0f, gainDb / 40.0f);
                const float al = sinW*0.5f*std::sqrt((A+1.0f/A)*(1.0f/Q-1.0f)+2.0f);
                const float sq = std::sqrt(A);
                const float inv = 1.0f / ((A+1)-(A-1)*cosW+2*sq*al);
                b0 = A*((A+1)+(A-1)*cosW+2*sq*al)*inv;  b1 = -2*A*((A-1)+(A+1)*cosW)*inv;
                b2 = A*((A+1)+(A-1)*cosW-2*sq*al)*inv;  a1 = 2*((A-1)-(A+1)*cosW)*inv;
                a2 = ((A+1)-(A-1)*cosW-2*sq*al)*inv;
                break;
            }
            case DynEQFilterType::LowPass:
            {
                const float al = sinW / (2.0f * Q);
                const float inv = 1.0f / (1.0f + al);
                b0 = (1-cosW)*0.5f*inv;  b1 = (1-cosW)*inv;  b2 = b0;
                a1 = -2*cosW*inv;  a2 = (1-al)*inv;
                break;
            }
            case DynEQFilterType::HighPass:
            {
                const float al = sinW / (2.0f * Q);
                const float inv = 1.0f / (1.0f + al);
                b0 = (1+cosW)*0.5f*inv;  b1 = -(1+cosW)*inv;  b2 = b0;
                a1 = -2*cosW*inv;  a2 = (1-al)*inv;
                break;
            }
        }
    }

    // ---- Band management --------------------------------------------------

    void selectBand(int i)
    {
        selectedBand_ = i;
        if (i >= 0 && i < bandBtns_.size())
            bandBtns_[i]->setToggleState(true, juce::dontSendNotification);
        updateParamControls();
        repaint();
    }

    void pushBand(int i)
    {
        proc_.setBand(i, params_[i]);
    }

    // ---- Parameter controls -----------------------------------------------

    void setupParamControls()
    {
        // Type
        typeLabel_.setText("Type", juce::dontSendNotification);
        typeLabel_.setJustificationType(juce::Justification::centred);
        typeLabel_.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        addAndMakeVisible(typeLabel_);

        typeBox_.addItem("Bell",       1);
        typeBox_.addItem("Low Shelf",  2);
        typeBox_.addItem("High Shelf", 3);
        typeBox_.addItem("Low Pass",   4);
        typeBox_.addItem("High Pass",  5);
        typeBox_.onChange = [this]
        {
            params_[selectedBand_].type = (DynEQFilterType)(typeBox_.getSelectedId() - 1);
            pushBand(selectedBand_);
            repaint();
        };
        addAndMakeVisible(typeBox_);

        // Knob helper
        auto addKnob = [this](juce::Slider& s, juce::Label& lbl, const juce::String& name,
                               double lo, double hi, double step)
        {
            lbl.setText(name, juce::dontSendNotification);
            lbl.setJustificationType(juce::Justification::centred);
            lbl.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
            addAndMakeVisible(lbl);

            s.setRange(lo, hi, step);
            s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
            addAndMakeVisible(s);
        };

        addKnob(freqSlider_,  freqLabel_,  "Freq",    20.0,  20000.0, 1.0);
        addKnob(gainSlider_,  gainLabel_,  "Gain dB", -18.0, 18.0,   0.1);
        addKnob(qSlider_,     qLabel_,     "Q",       0.1,   10.0,   0.01);

        freqSlider_.setSkewFactorFromMidPoint(1000.0);
        freqSlider_.onValueChange = [this]
        {
            params_[selectedBand_].frequency = (float)freqSlider_.getValue();
            pushBand(selectedBand_); repaint();
        };
        gainSlider_.onValueChange = [this]
        {
            params_[selectedBand_].maxGainDb = (float)gainSlider_.getValue();
            pushBand(selectedBand_); repaint();
        };
        qSlider_.onValueChange = [this]
        {
            params_[selectedBand_].Q = (float)qSlider_.getValue();
            pushBand(selectedBand_); repaint();
        };

        // Dynamic toggle
        dynToggle_.setButtonText("Dynamic");
        dynToggle_.onClick = [this]
        {
            params_[selectedBand_].dynamicOn = dynToggle_.getToggleState();
            pushBand(selectedBand_);
            updateDynVisible();
        };
        addAndMakeVisible(dynToggle_);

        // Dynamic section knobs
        addKnob(threshSlider_,  threshLabel_,  "Thresh dB", -60.0, 0.0,   0.5);
        addKnob(ratioSlider_,   ratioLabel_,   "Ratio",      1.0,  20.0,  0.1);
        addKnob(attackSlider_,  attackLabel_,  "Atk ms",     0.1,  200.0, 0.1);
        addKnob(releaseSlider_, releaseLabel_, "Rel ms",     5.0,  2000.0, 1.0);

        threshSlider_.onValueChange  = [this] { params_[selectedBand_].threshold  = (float)threshSlider_.getValue();  pushBand(selectedBand_); };
        ratioSlider_.onValueChange   = [this] { params_[selectedBand_].ratio      = (float)ratioSlider_.getValue();   pushBand(selectedBand_); };
        attackSlider_.onValueChange  = [this] { params_[selectedBand_].attackMs   = (float)attackSlider_.getValue();  pushBand(selectedBand_); };
        releaseSlider_.onValueChange = [this] { params_[selectedBand_].releaseMs  = (float)releaseSlider_.getValue(); pushBand(selectedBand_); };

        dirLabel_.setText("Direction", juce::dontSendNotification);
        dirLabel_.setJustificationType(juce::Justification::centred);
        dirLabel_.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        addAndMakeVisible(dirLabel_);

        dirBox_.addItem("Downward", 1);
        dirBox_.addItem("Upward",   2);
        dirBox_.onChange = [this]
        {
            params_[selectedBand_].direction = (dirBox_.getSelectedId() == 1)
                                             ? DynEQDirection::Downward
                                             : DynEQDirection::Upward;
            pushBand(selectedBand_);
        };
        addAndMakeVisible(dirBox_);
    }

    void layoutParamControls()
    {
        const int panelY = kHeaderH + kCanvasH + 6;
        const int kW = 72, kH = 68, lH = 14;

        // Row 1: type box + freq / gain / Q knobs + dynamic toggle
        typeLabel_.setBounds(8,      panelY,       88, lH);
        typeBox_  .setBounds(8,      panelY + lH,  88, 22);

        int x = 104;
        auto placeKnob = [&](juce::Label& lbl, juce::Slider& s)
        {
            lbl.setBounds(x, panelY,      kW, lH);
            s  .setBounds(x, panelY + lH, kW, kH);
            x += kW + 6;
        };
        placeKnob(freqLabel_, freqSlider_);
        placeKnob(gainLabel_, gainSlider_);
        placeKnob(qLabel_,    qSlider_);

        dynToggle_.setBounds(x + 4, panelY + 28, 80, 22);

        // Row 2: dynamic params
        const int row2Y = panelY + lH + kH + 8;

        dirLabel_.setBounds(8,      row2Y,       88, lH);
        dirBox_  .setBounds(8,      row2Y + lH,  88, 22);

        x = 104;
        auto placeKnob2 = [&](juce::Label& lbl, juce::Slider& s)
        {
            lbl.setBounds(x, row2Y,      kW, lH);
            s  .setBounds(x, row2Y + lH, kW, kH);
            x += kW + 6;
        };
        placeKnob2(threshLabel_,  threshSlider_);
        placeKnob2(ratioLabel_,   ratioSlider_);
        placeKnob2(attackLabel_,  attackSlider_);
        placeKnob2(releaseLabel_, releaseSlider_);
    }

    void updateParamControls()
    {
        const auto& p = params_[selectedBand_];
        typeBox_     .setSelectedId((int)p.type + 1,       juce::dontSendNotification);
        freqSlider_  .setValue(p.frequency,                juce::dontSendNotification);
        gainSlider_  .setValue(p.maxGainDb,                juce::dontSendNotification);
        qSlider_     .setValue(p.Q,                        juce::dontSendNotification);
        dynToggle_   .setToggleState(p.dynamicOn,          juce::dontSendNotification);
        threshSlider_.setValue(p.threshold,                juce::dontSendNotification);
        ratioSlider_ .setValue(p.ratio,                    juce::dontSendNotification);
        attackSlider_.setValue(p.attackMs,                 juce::dontSendNotification);
        releaseSlider_.setValue(p.releaseMs,               juce::dontSendNotification);
        dirBox_      .setSelectedId(p.direction == DynEQDirection::Downward ? 1 : 2,
                                    juce::dontSendNotification);
    }

    void updateDynVisible()
    {
        const bool on = params_[selectedBand_].dynamicOn;
        threshSlider_ .setVisible(on);  threshLabel_ .setVisible(on);
        ratioSlider_  .setVisible(on);  ratioLabel_  .setVisible(on);
        attackSlider_ .setVisible(on);  attackLabel_ .setVisible(on);
        releaseSlider_.setVisible(on);  releaseLabel_.setVisible(on);
        dirBox_       .setVisible(on);  dirLabel_    .setVisible(on);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicEQComponent)
};

// ---------------------------------------------------------------------------
// DynamicEQWindow
// ---------------------------------------------------------------------------
class DynamicEQWindow : public juce::DocumentWindow
{
public:
    DynamicEQWindow(DynamicEQProcessor& proc, const juce::String& trackName)
        : juce::DocumentWindow(juce::String::fromUTF8("Dynamic EQ  \xe2\x80\x94  ") + trackName,
                               juce::Colour(0xff18181c),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new DynamicEQComponent(proc, trackName), true);
        setResizable(false, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynamicEQWindow)
};
