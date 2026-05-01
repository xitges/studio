#pragma once
#include <JuceHeader.h>
#include "ProjectModel.h"
#include "ProjectSerializer.h"
#include "StudioLookAndFeel.h"
#include "ToolbarComponent.h"
#include "ChannelRackComponent.h"
#include "Audio/AudioEngine.h"
#include "PlaylistComponent.h"
#include "PianoRollComponent.h"
#include "MixerComponent.h"
#include "SynthEditorComponent.h"
#include "FXEditorComponent.h"
#include "AutoTuneEditorComponent.h"
#include "LaunchpadComponent.h"
#include "SampleBrowserComponent.h"
#include "PluginBrowserComponent.h"
#include "DynamicEQComponent.h"
#include "TrackpadController.h"
#include "LivePerformance/ClipLauncher.h"
#include "LivePerformance/LivePerformanceComponent.h"
#include "LivePerformance/LiveLoopWindow.h"

// Tab bar: INSTRUMENT / SEQUENCER / MIXER  (Phase-6 redesign)
class InspectorTabBar : public juce::Component
{
public:
    std::function<void(int)> onTabChanged;

    void setTab(int t) { activeTab_ = t; repaint(); }
    int  getTab() const { return activeTab_; }

    // Dynamic sub-label setters — call from MainComponent when state changes
    void setInstrumentSub(int chIdx, const juce::String& name)
    {
        instrSub_ = "CH " + juce::String(chIdx + 1).paddedLeft('0', 2)
                  + juce::String::fromUTF8("  \xe2\x80\x94  ") + name.toUpperCase();
        gutterSub_ = instrSub_;
        repaint();
    }
    void setSequencerSub(const juce::String& patternName)
    {
        seqSub_ = patternName.toUpperCase();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        using LF = StudioLookAndFeel;
        const int W = getWidth();
        const int H = getHeight();

        juce::Colour topWhite (0xfffafafa);
            juce::Colour bottomGrey (0xfff0f0f0);

            // 2. 그라데이션 설정 (0.0f에서 전체 높이 H까지)
        juce::ColourGradient bg (topWhite,   0.0f, 0.0f,
                                 bottomGrey, 0.0f, (float)H, false);
        g.setGradientFill(bg);
        g.fillAll();

        // Top + bottom hairlines
        g.setColour(juce::Colour(0xffdcdcdc));
        g.drawLine(0.0f, 0.5f, (float)W, 0.5f, 1.0f);
        g.drawLine(0.0f, (float)(H - 1), (float)W, (float)(H - 1), 1.0f);

        const int tw = W / 3;

        static const char* kLabels[] = { "INSTRUMENT", "SEQUENCER", "MIXER" };
        const juce::String kSubs[3] = {
            instrSub_,
            seqSub_,
            juce::String::fromUTF8("8 INSERT  \xc2\xb7  1 MASTER"),
        };

        for (int i = 0; i < 3; ++i)
        {
            const bool on = (i == activeTab_);
            juce::Rectangle<int> tab(i * tw, 0, (i == 2) ? W - i * tw : tw, H);

            if (on)
            {
                // 3. 선택된 탭 배경: 순백색으로 강조
                g.setColour(juce::Colours::white);
                g.fillRect(tab.reduced(1, 0));

                // 4. 선택된 탭 좌우 경계선: 부드러운 회색
                g.setColour(juce::Colour(0xffdcdcdc));
                g.drawLine((float)(tab.getX() + 1), 0.0f, (float)(tab.getX() + 1), (float)H, 1.0f);
                g.drawLine((float)(tab.getRight() - 1), 0.0f, (float)(tab.getRight() - 1), (float)H, 1.0f);

                // 5. 상단 액센트 바 (포인트 색상)
                g.setColour(juce::Colour(LF::kAccent));
                g.fillRect(tab.getX() + 2, 0, tab.getWidth() - 4, 3); // 2px -> 3px로 살짝 두껍게
            }
            else
            {
                // 선택되지 않은 탭들 사이의 구분선 (옵션)
                if (i < 2) {
                    g.setColour(juce::Colour(0xffe8e8e8));
                    g.drawLine((float)tab.getRight(), 10.0f, (float)tab.getRight(), (float)H - 10.0f, 1.0f);
                }
            }

            // Main label — bigger font, centred in tall tab
            g.setFont(LF::monoFont(11.0f, juce::Font::bold));
            g.setColour(on ? juce::Colour(0xff333333) : juce::Colour(0xff888888));
            g.drawText(kLabels[i], tab.getX(), H / 2 - 18, tab.getWidth(), 18,
                       juce::Justification::centred);

            // Sub-label
            g.setFont(LF::monoFont(8.5f, juce::Font::bold));
            g.setColour(on ? juce::Colour(LF::kAccent) : juce::Colour(0xffaaaaaa));
            g.drawText(kSubs[i], tab.getX() + 2, H / 2 + 2, tab.getWidth() - 4, 14,
                       juce::Justification::centred, true);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int tw = getWidth() / 3;
        const int t  = juce::jlimit(0, 2, e.x / tw);
        if (t != activeTab_)
        {
            activeTab_ = t;
            repaint();
            if (onTabChanged) onTabChanged(activeTab_);
        }
    }

private:
    int          activeTab_ = 1;   // default: SEQUENCER
    juce::String instrSub_  = "CH 01";
    juce::String seqSub_    = "PATTERN";
    juce::String gutterSub_ = "CH 01";
};

// =========================================================================
// Step Inspector strip — right side of the inspector tab bar row
// =========================================================================
class StepInspectorStrip : public juce::Component
{
public:
    std::function<void(int ch, int step, const StepParams&)> onParamsChanged;

    StepInspectorStrip()
    {
        using LF = StudioLookAndFeel;
        auto setup = [&](juce::Slider& s, double lo, double hi, double step, juce::Colour col)
        {
            s.setRange(lo, hi, step);
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            s.setColour(juce::Slider::trackColourId,       col);
            s.setColour(juce::Slider::backgroundColourId,  juce::Colour(LF::kDark));
            s.setColour(juce::Slider::thumbColourId,       juce::Colour(LF::kPanel));
            addChildComponent(s);
        };
        setup(velSlider_,   0.0, 2.0,   0.01, juce::Colour(0xff3a9ad9));
        setup(gateSlider_,  0.0, 2.0,   0.01, juce::Colour(0xff27ae60));
        setup(probSlider_,  0.0, 1.0,   0.01, juce::Colour(0xffe6b32b));
        setup(pitchSlider_, -12.0, 12.0, 1.0, juce::Colour(StudioLookAndFeel::kAccent));
        setup(cutoffSlider_, -3.0, 3.0,  0.01, juce::Colour(0xffcc6699));
        setup(timingSlider_, -0.5, 0.5,  0.01, juce::Colour(0xff9977cc));

        velSlider_   .onValueChange = [this] { pushChange(); };
        gateSlider_  .onValueChange = [this] { pushChange(); };
        probSlider_  .onValueChange = [this] { pushChange(); };
        pitchSlider_ .onValueChange = [this] { pushChange(); };
        cutoffSlider_.onValueChange = [this] { pushChange(); };
        timingSlider_.onValueChange = [this] { pushChange(); };

        addChildComponent(resetBtn_);
        resetBtn_.setButtonText("RESET");
        resetBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(StudioLookAndFeel::kChassis));
        resetBtn_.onClick = [this]
        {
            if (ch_ < 0) return;
            StepParams def;
            updateFromExternal(def);
            pushChange();
        };
    }

    void setStep(int ch, int step, const juce::String& chName, const StepParams& p)
    {
        ch_ = ch; step_ = step; chName_ = chName;
        currentParams_ = p;
        syncSliders();
        velSlider_   .setVisible(true);
        gateSlider_  .setVisible(true);
        probSlider_  .setVisible(true);
        pitchSlider_ .setVisible(true);
        cutoffSlider_.setVisible(true);
        timingSlider_.setVisible(true);
        resetBtn_    .setVisible(true);
        repaint();
    }

    void clearStep()
    {
        ch_ = step_ = -1; chName_ = {};
        velSlider_   .setVisible(false);
        gateSlider_  .setVisible(false);
        probSlider_  .setVisible(false);
        pitchSlider_ .setVisible(false);
        cutoffSlider_.setVisible(false);
        timingSlider_.setVisible(false);
        resetBtn_    .setVisible(false);
        repaint();
    }

    void updateFromExternal(const StepParams& p)
    {
        currentParams_ = p;
        syncSliders();
        repaint();
    }

    void resized() override
    {
        const int W = getWidth(), H = getHeight();
        if (W < 10 || H < 10) return;

        // Layout: header (22px) → row1 (26px) → row2 (26px)
        constexpr int kLblW  = 46;  // label column
        constexpr int kValW  = 38;  // value text column
        constexpr int kGap   = 8;
        const int     slH    = 14;
        const int     col3W  = (W - kGap*4) / 3;  // each of 3 columns
        const int     slW    = col3W - kLblW - kValW;

        auto colX = [&](int c) { return kGap + c * (col3W + kGap); };
        auto slX  = [&](int c) { return colX(c) + kLblW; };

        const int row1Y = 22 + (26 - slH) / 2;
        const int row2Y = 22 + 26 + (26 - slH) / 2;

        velSlider_   .setBounds(slX(0), row1Y, slW, slH);
        gateSlider_  .setBounds(slX(1), row1Y, slW, slH);
        probSlider_  .setBounds(slX(2), row1Y, slW, slH);
        pitchSlider_ .setBounds(slX(0), row2Y, slW, slH);
        cutoffSlider_.setBounds(slX(1), row2Y, slW, slH);
        timingSlider_.setBounds(slX(2), row2Y, slW, slH);

        resetBtn_.setBounds(W - 60, 2, 56, 18);
    }

    void paint(juce::Graphics& g) override
    {
        using LF = StudioLookAndFeel;
        const int W = getWidth(), H = getHeight();

        juce::Colour topWhite (0xfffafafa);
        juce::Colour bottomGrey (0xfff0f0f0);

        // 2. 그라데이션 설정 (상단 0.0f에서 하단 H까지)
        juce::ColourGradient bg (topWhite,   0.0f, 0.0f,
                                 bottomGrey, 0.0f, (float)H, false);
        g.setGradientFill(bg);
        g.fillAll();

        // Top + bottom hairlines (matching InspectorTabBar)
        g.setColour(juce::Colour(0xffdcdcdc));
        g.drawLine(0.0f, 0.5f, (float)W, 0.5f, 1.0f);
        g.drawLine(0.0f, (float)(H - 1), (float)W, (float)(H - 1), 1.0f);

        // Left separator
        g.setColour(juce::Colour(0xffdcdcdc));
        g.drawLine(1.0f, 4.0f, 1.0f, (float)(H - 4), 0.8f);

        // Header
        g.setFont(LF::monoFont(9.5f, juce::Font::bold));
        g.setColour(juce::Colour(LF::kTextFaint));
        g.drawText("STEP INSPECTOR", 8, 0, 130, 22, juce::Justification::centredLeft);

        if (ch_ < 0)
        {
            g.setFont(LF::monoFont(9.0f));
            g.setColour(juce::Colour(LF::kTextFaint).withAlpha(0.45f));
            g.drawText(juce::String::fromUTF8("— select a step"), 140, 0, W - 200, 22,
                       juce::Justification::centredLeft);
            return;
        }

        // Step + channel badge
        const juce::String info = "S" + juce::String(step_ + 1).paddedLeft('0', 2)
                                + juce::String::fromUTF8("  \xe2\x80\x94  ") + chName_.toUpperCase();
        g.setFont(LF::monoFont(10.0f, juce::Font::bold));
        g.setColour(juce::Colour(LF::kAccent));
        g.drawText(info, 140, 0, W - 210, 22, juce::Justification::centredLeft);

        // Param labels + values in 3-column grid
        struct ParamInfo { const char* label; juce::uint32 col; juce::Slider* slider; bool isPitch; };
        ParamInfo rows[2][3] = {
            { {"VEL",   0xff3a9ad9, &velSlider_,    false},
              {"GATE",  0xff27ae60, &gateSlider_,   false},
              {"PROB",  0xffe6b32b, &probSlider_,   false} },
            { {"PITCH", LF::kAccent, &pitchSlider_, true },
              {"CUT",   0xffcc6699, &cutoffSlider_, false},
              {"TIMING",0xff9977cc, &timingSlider_, false} }
        };

        constexpr int kLblW = 46;
        constexpr int kGap  = 8;
        const int col3W = (W - kGap*4) / 3;

        for (int row = 0; row < 2; ++row)
        {
            const int rowY = 22 + row * 26;
            for (int col = 0; col < 3; ++col)
            {
                const auto& p  = rows[row][col];
                const int   cx = kGap + col * (col3W + kGap);

                // Colored label
                g.setFont(LF::monoFont(8.0f, juce::Font::bold));
                g.setColour(juce::Colour(p.col).withAlpha(0.9f));
                g.drawText(p.label, cx, rowY + 4, kLblW - 4, 18,
                           juce::Justification::centredRight);

                // Value text (right of slider)
                const double v = p.slider->getValue();
                juce::String val;
                if (p.isPitch)
                    val = (v >= 0 ? "+" : "") + juce::String((int)v) + "st";
                else
                    val = juce::String(v, 2);

                const int slW  = col3W - kLblW - 38;
                const int slRight = cx + kLblW + slW;
                g.setFont(LF::monoFont(8.0f, juce::Font::bold));
                g.setColour(juce::Colour(LF::kTextDim));
                g.drawText(val, slRight + 2, rowY + 4, 36, 18,
                           juce::Justification::centredLeft);
            }
        }
    }

private:
    juce::Slider     velSlider_, gateSlider_, probSlider_;
    juce::Slider     pitchSlider_, cutoffSlider_, timingSlider_;
    juce::TextButton resetBtn_;
    int              ch_ = -1, step_ = -1;
    juce::String     chName_;
    StepParams       currentParams_;

    void syncSliders()
    {
        velSlider_   .setValue(currentParams_.velocity,             juce::dontSendNotification);
        gateSlider_  .setValue(currentParams_.gate,                 juce::dontSendNotification);
        probSlider_  .setValue(currentParams_.probability,           juce::dontSendNotification);
        pitchSlider_ .setValue((double)currentParams_.pitchOffset,  juce::dontSendNotification);
        cutoffSlider_.setValue(currentParams_.cutoffMod,            juce::dontSendNotification);
        timingSlider_.setValue(currentParams_.timingOffset,         juce::dontSendNotification);
    }

    void pushChange()
    {
        if (ch_ < 0 || step_ < 0) return;
        currentParams_.velocity      = (float)velSlider_.getValue();
        currentParams_.gate          = (float)gateSlider_.getValue();
        currentParams_.probability   = (float)probSlider_.getValue();
        currentParams_.pitchOffset   = (int)std::round(pitchSlider_.getValue());
        currentParams_.cutoffMod     = (float)cutoffSlider_.getValue();
        currentParams_.timingOffset  = (float)timingSlider_.getValue();
        if (onParamsChanged) onParamsChanged(ch_, step_, currentParams_);
        repaint();  // refresh value text
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepInspectorStrip)
};

// =========================================================================
// Instrument Panel — shown when INSTRUMENT (tab 0) is active
//
// Layout:
//   Left col (kLeftW): OSC1 waveform frame (top) + OSC2 waveform frame (below)
//   Right col         : OSCILLATORS / FILTER / ENVELOPE sections with sliders
// =========================================================================
class InstrumentPanel : public juce::Component
{
public:
    std::function<void(float semitones)>    onTuneChanged;
    std::function<void(const SynthParams&)> onSynthParamsChanged;

    InstrumentPanel()
    {
        // ---- Wave-select buttons (OSC 2 synth waveform) ----
        static const char* kWaveLbls[]  = { "SINE", "SAW", "SQ", "TRI", "NOISE" };
        static const int   kWaveIdx[]   = { 0, 1, 2, 3, 5 };
        for (int i = 0; i < 5; ++i)
        {
            waveButtons_[i].setButtonText(kWaveLbls[i]);
            waveButtons_[i].setClickingTogglesState(false);
            waveButtons_[i].onClick = [this, wi = kWaveIdx[i]] { setWaveform(wi); };
            addAndMakeVisible(waveButtons_[i]);
        }

        // ---- Filter type buttons (LP / BP / HP) ----
        static const char* kFiltLbls[] = { "LP", "BP", "HP" };
        static const int   kFiltIdx[]  = { 0, 2, 1 }; // maps to synthParams_.filterType
        for (int i = 0; i < 3; ++i)
        {
            filtButtons_[i].setButtonText(kFiltLbls[i]);
            filtButtons_[i].setClickingTogglesState(false);
            filtButtons_[i].onClick = [this, fi = kFiltIdx[i]] { setFilterType(fi); };
            addAndMakeVisible(filtButtons_[i]);
        }

        // ---- Sliders ----
        auto mkSlider = [&](juce::Slider& s, double lo, double hi, double step)
        {
            s.setRange(lo, hi, step);
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, kRowH);
            s.setColour(juce::Slider::textBoxTextColourId,       juce::Colours::black);
            s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xffe8e8ecu));
            s.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0xffd0d0d4u));
            addAndMakeVisible(s);
        };

        mkSlider(tuneSlider_,    -24.0,  24.0,   0.5);
        mkSlider(detSlider_,       0.0, 100.0,   0.1);
        mkSlider(driveSlider_,     0.0,   1.0,  0.01);
        mkSlider(cutoffSlider_,  200.0, 20000.0, 10.0);
        mkSlider(resSlider_,       0.0,   1.0,  0.01);
        mkSlider(envAmtSlider_,   -1.0,   1.0,  0.01);
        mkSlider(attackSlider_,    0.0, 5000.0,   1.0);
        mkSlider(decaySlider_,     0.0, 5000.0,   1.0);
        mkSlider(sustainSlider_,   0.0,   1.0,  0.01);
        mkSlider(releaseSlider_,   0.0, 5000.0,   1.0);

        tuneSlider_.onValueChange = [this] {
            tune_ = (float)tuneSlider_.getValue();
            if (onTuneChanged) onTuneChanged(tune_);
        };
        auto synthCb = [this] { pullSynthParams(); if (onSynthParamsChanged) onSynthParamsChanged(synthParams_); };
        detSlider_.onValueChange = driveSlider_.onValueChange =
        cutoffSlider_.onValueChange = resSlider_.onValueChange =
        envAmtSlider_.onValueChange = attackSlider_.onValueChange =
        decaySlider_.onValueChange  = sustainSlider_.onValueChange =
        releaseSlider_.onValueChange = synthCb;

        formatManager_.registerBasicFormats();
    }

    void setChannel(int ch, const juce::String& name, const juce::String& samplePath,
                    const SynthParams& sp, float tuneSemitones)
    {
        selectedChannel_ = ch;
        channelName_     = name;
        samplePath_      = samplePath;
        synthParams_     = sp;
        tune_            = tuneSemitones;
        loadWaveform(samplePath);
        syncControls();
        updateWaveBtnColors();
        updateFiltBtnColors();
        repaint();
    }

    int getNeededHeight() const
    {
        // header + top-pad + right-column total + bottom-pad
        return kHeaderH + kPadV + rightColumnHeight() + kPadV;
    }

    // -------------------------------------------------------------------------
    void paint(juce::Graphics& g) override
    {
        using LF = StudioLookAndFeel;
        const int W = getWidth();

        g.fillAll(juce::Colour(0xfffafafa));

            // =========================================================================
            // [1] 상단 패널 탭 바 (INSTRUMENT | A · ANALOG POLY) - 높이 22px
            // =========================================================================
            g.setColour(juce::Colour(0xfff0f0f0));
            g.fillRect(0, 0, W, 22);
            
            g.setColour(juce::Colour(0xffdcdcdc));
            g.drawLine(0.0f, 21.5f, (float)W, 21.5f, 1.0f);

            // Title ("INSTRUMENT")
            g.setFont(LF::monoFont(9.0f, juce::Font::bold));
            g.setColour(juce::Colour(LF::kAccent)); // 메인 액센트 컬러
            g.drawText("INSTRUMENT", 14, 0, 90, 22, juce::Justification::centredLeft);

            // Sub ("A · ANALOG POLY")
            g.setFont(LF::monoFont(8.5f, juce::Font::bold));
            g.setColour(juce::Colour(0xff888888));
            g.drawText(juce::String::fromUTF8("A \xc2\xb7 ANALOG POLY"), 94, 0, 200, 22, juce::Justification::centredLeft);

            // =========================================================================
            // [2] 메인 헤더 타이틀 영역 (POLY-6 mk2 + Segment Display) - 높이 44px
            // =========================================================================
        const int hY = 28;
        const int hH = 44;

        // 1. 메인 타이틀 (채널 이름 사용, 예: "KICK", "SNARE", "TRACK 1")
        g.setFont(LF::monoFont(16.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff222222));
        g.drawText(channelName_.toUpperCase(), 14, hY + 6, 300, 22, juce::Justification::centredLeft);

        // 2. 서브 타이틀 구성 (샘플 유무에 따라 다르게 표시)
        juce::String subTitle;
        if (samplePath_.isNotEmpty())
        {
            // 수정됨: fromUTF8로 특수문자를 안전하게 처리
            subTitle = juce::String::fromUTF8("SAMPLE \xc2\xb7 \"") + juce::File(samplePath_).getFileName() + "\"";
        }
        else
        {
            static const char* kWaveLbls[] = { "SINE", "SAW", "SQUARE", "TRI", "", "NOISE" };
            int waveIdx = juce::jlimit(0, 5, synthParams_.waveform);
            // 수정됨: fromUTF8로 특수문자를 안전하게 처리
            subTitle = juce::String::fromUTF8("SYNTH \xc2\xb7 ") + kWaveLbls[waveIdx];
        }

        g.setFont(LF::monoFont(9.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xff888888));
        g.drawText(subTitle, 14, hY + 26, 300, 14, juce::Justification::centredLeft);

        // =========================================================================
        // [3] 우측 SegmentDisplay (A4 · 64v)
        // =========================================================================
        const int segW = 90;
        const int segH = 26;
        const int segX = W - segW - 14;
        const int segY = hY + (hH - segH) / 2; // 세로 중앙 정렬 (alignItems:"center")

        juce::Rectangle<float> segBox((float)segX, (float)segY, (float)segW, (float)segH);
        
        // 디스플레이 배경 (어두운 LCD 느낌)
        g.setColour(juce::Colour(0xff1a1a20));
        g.fillRoundedRectangle(segBox, 4.0f);
        
        // 디스플레이 테두리 (내부 그림자 느낌)
        g.setColour(juce::Colour(0x66000000));
        g.drawRoundedRectangle(segBox.reduced(0.5f), 4.0f, 1.0f);

        // 디스플레이 텍스트 (빛나는 네온 그린 색상)
        g.setFont(LF::monoFont(12.0f, juce::Font::bold));
        g.setColour(juce::Colour(0xffb9ff66));
        g.drawText(juce::String::fromUTF8("A4 \xc2\xb7 64v"),
                   segBox.toNearestInt(), juce::Justification::centred);

        // 헤더와 본문 사이의 구분선
        g.setColour(juce::Colour(0xffdcdcdc));
        g.drawLine(0.0f, (float)kHeaderH - 0.5f, (float)W, (float)kHeaderH - 0.5f, 1.0f);

        // Waveform OSC frames (left column)
        const int contentTopY = kHeaderH + kPadV;
        juce::Rectangle<int> osc1(kPadH, contentTopY, kLeftW, kOscH);
        juce::Rectangle<int> osc2(kPadH, contentTopY + kOscH + kOscGap, kLeftW, kOscH);

        paintOscFrame(g, osc1, juce::String::fromUTF8("OSC 1  \xe2\x80\x94  ORIGINAL SAMPLE"));
        paintSampleWaveform(g, osc1.reduced(3, 18));

        paintOscFrame(g, osc2, juce::String::fromUTF8("OSC 2  \xe2\x80\x94  SYNTH SHAPE"));
        paintSynthWaveform(g, osc2.reduced(3, 18));

        // Right column section headers + param labels
        const int rx = kPadH + kLeftW + kInnerGap;
        const int rw = W - rx - kPadH;
        if (rw < 60) return;

        int ry = contentTopY;

        // OSCILLATORS section
        paintSectionHeader(g, rx, ry, rw, "OSCILLATORS", "DUAL VCO");
        ry += kSecHeaderH;
        // wave label row
        g.setFont(LF::monoFont(7.5f, juce::Font::bold));
        g.setColour(juce::Colour(0xff888898u));
        g.drawText("WAVE", rx, ry + 2, kLblW - 4, kRowH - 2, juce::Justification::centredRight);
        ry += kRowH + kRowGap;
        paintSliderLabel(g, "TUNE",  0xff3a9ad9u, rx, ry);  ry += kRowH + kRowGap;
        paintSliderLabel(g, "DET",   0xff27ae60u, rx, ry);  ry += kRowH + kRowGap;
        paintSliderLabel(g, "DRIVE", 0xffe6b32bu, rx, ry);  ry += kRowH + kRowGap + kSecGap;

        // FILTER section
        paintSectionHeader(g, rx, ry, rw, "FILTER", "STATE-VARIABLE 24dB");
        ry += kSecHeaderH;
        paintSliderLabel(g, "CUT",  0xffcc6699u, rx + kFiltBtnW + kFiltGap, ry);  ry += kRowH + kRowGap;
        paintSliderLabel(g, "RES",  0xff9977ccu, rx + kFiltBtnW + kFiltGap, ry);  ry += kRowH + kRowGap;
        paintSliderLabel(g, "ENV",  0xff8899ccu, rx + kFiltBtnW + kFiltGap, ry);  ry += kRowH + kRowGap + kSecGap;

        // ENVELOPE section
        paintSectionHeader(g, rx, ry, rw, "ENVELOPE", "ADSR  \xc2\xb7  AMP");
        ry += kSecHeaderH;
        paintSliderLabel(g, "ATK", 0xff3a9ad9u, rx, ry);  ry += kRowH + kRowGap;
        paintSliderLabel(g, "DEC", 0xff27ae60u, rx, ry);  ry += kRowH + kRowGap;
        paintSliderLabel(g, "SUS", 0xffe6b32bu, rx, ry);  ry += kRowH + kRowGap;
        paintSliderLabel(g, "REL", 0xffcc6699u, rx, ry);
    }

    void resized() override
    {
        const int W = getWidth();
        if (W < 80) return;

        const int rx  = kPadH + kLeftW + kInnerGap;
        const int rw  = W - rx - kPadH;
        if (rw < 60) return;

        const int slW = rw - kLblW;
        const int sx  = rx + kLblW;

        // Filter-side slider starts after filter buttons
        const int filtSlW = rw - kFiltBtnW - kFiltGap - kLblW;
        const int filtSx  = rx + kFiltBtnW + kFiltGap + kLblW;

        int ry = kHeaderH + kPadV;

        // OSCILLATORS section
        ry += kSecHeaderH;

        // Wave buttons row
        const int btnW = (rw - kLblW) / 5;
        for (int i = 0; i < 5; ++i)
            waveButtons_[i].setBounds(sx + i * btnW, ry, btnW - 2, kRowH);
        ry += kRowH + kRowGap;

        tuneSlider_ .setBounds(sx, ry, slW, kRowH);  ry += kRowH + kRowGap;
        detSlider_  .setBounds(sx, ry, slW, kRowH);  ry += kRowH + kRowGap;
        driveSlider_.setBounds(sx, ry, slW, kRowH);  ry += kRowH + kRowGap + kSecGap;

        // FILTER section
        ry += kSecHeaderH;

        // LP/BP/HP buttons stacked vertically on the left
        for (int i = 0; i < 3; ++i)
            filtButtons_[i].setBounds(rx, ry + i * (kRowH + kRowGap), kFiltBtnW - 2, kRowH);

        cutoffSlider_.setBounds(filtSx, ry, filtSlW, kRowH);  ry += kRowH + kRowGap;
        resSlider_   .setBounds(filtSx, ry, filtSlW, kRowH);  ry += kRowH + kRowGap;
        envAmtSlider_.setBounds(filtSx, ry, filtSlW, kRowH);  ry += kRowH + kRowGap + kSecGap;

        // ENVELOPE section
        ry += kSecHeaderH;
        attackSlider_ .setBounds(sx, ry, slW, kRowH);  ry += kRowH + kRowGap;
        decaySlider_  .setBounds(sx, ry, slW, kRowH);  ry += kRowH + kRowGap;
        sustainSlider_.setBounds(sx, ry, slW, kRowH);  ry += kRowH + kRowGap;
        releaseSlider_.setBounds(sx, ry, slW, kRowH);
    }

private:
    // Layout constants
    static constexpr int kHeaderH   = 75;
    static constexpr int kPadH      = 10;
    static constexpr int kPadV      = 8;
    static constexpr int kLeftW     = 650;
    static constexpr int kInnerGap  = 12;
    static constexpr int kOscH      = 100;
    static constexpr int kOscGap    = 8;
    static constexpr int kRowH      = 22;
    static constexpr int kRowGap    = 4;
    static constexpr int kLblW      = 52;
    static constexpr int kSecHeaderH= 22;
    static constexpr int kSecGap    = 10;
    static constexpr int kFiltBtnW  = 36;
    static constexpr int kFiltGap   = 4;

    int          selectedChannel_ = 0;
    juce::String channelName_;
    juce::String samplePath_;
    SynthParams  synthParams_;
    float        tune_ = 0.0f;

    std::vector<float> wfMin_, wfMax_;

    juce::TextButton waveButtons_[5];
    juce::TextButton filtButtons_[3];

    juce::Slider tuneSlider_, detSlider_, driveSlider_;
    juce::Slider cutoffSlider_, resSlider_, envAmtSlider_;
    juce::Slider attackSlider_, decaySlider_, sustainSlider_, releaseSlider_;

    juce::AudioFormatManager formatManager_;

    // ---- helpers ----

    int rightColumnHeight() const
    {
        // OSCILLATORS: header + wave-row + 3 sliders + gap
        const int oscSec  = kSecHeaderH + (kRowH + kRowGap) * 4 + kSecGap;
        // FILTER: header + 3 sliders + gap
        const int filtSec = kSecHeaderH + (kRowH + kRowGap) * 3 + kSecGap;
        // ENVELOPE: header + 4 sliders
        const int envSec  = kSecHeaderH + (kRowH + kRowGap) * 4;
        return oscSec + filtSec + envSec;
    }

    void paintOscFrame(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label)
    {
        g.setColour(juce::Colour(0xff1a1a22u));
        g.fillRoundedRectangle(r.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff363644u));
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 5.0f, 1.0f);

        g.setFont(StudioLookAndFeel::monoFont(7.5f, juce::Font::bold));
        g.setColour(juce::Colour(0xff666672u));
        g.drawText(label, r.getX() + 7, r.getY() + 4, r.getWidth() - 14, 13,
                   juce::Justification::centredLeft);
    }

    void paintSampleWaveform(juce::Graphics& g, juce::Rectangle<int> r)
    {
        if (wfMin_.empty())
        {
            g.setFont(StudioLookAndFeel::monoFont(8.0f));
            g.setColour(juce::Colour(0xff555560u));
            g.drawText("NO SAMPLE LOADED", r, juce::Justification::centred);
            return;
        }
        const int   numBins = (int)wfMin_.size();
        const float cy      = r.getCentreY();
        const float ampH    = r.getHeight() * 0.46f;
        const float dx      = (float)r.getWidth() / (float)numBins;
        g.setColour(juce::Colour(0xffb9ff66u).withAlpha(0.75f));
        for (int i = 0; i < numBins; ++i)
        {
            const float x  = r.getX() + i * dx;
            const float y1 = cy - wfMax_[i] * ampH;
            const float y2 = cy - wfMin_[i] * ampH;
            g.fillRect(x, y1, juce::jmax(1.0f, dx - 0.5f), juce::jmax(1.0f, y2 - y1));
        }
        g.setColour(juce::Colour(0xff4a4a5au));
        g.drawLine((float)r.getX(), cy, (float)r.getRight(), cy, 0.5f);
    }

    void paintSynthWaveform(juce::Graphics& g, juce::Rectangle<int> r)
    {
        const float cy   = r.getCentreY();
        const float ampH = r.getHeight() * 0.44f;
        const int   N    = r.getWidth();
        if (N < 2) return;
        juce::Path p;
        for (int i = 0; i < N; ++i)
        {
            const float t = (float)i / (float)N;
            float y = 0.0f;
            switch (synthParams_.waveform)
            {
                case 0: y = std::sin(t * juce::MathConstants<float>::twoPi); break;
                case 1: y = 1.0f - 2.0f * t; break;
                case 2: y = (t < 0.5f ? 1.0f : -1.0f); break;
                case 3: y = (t < 0.25f ? 4.0f*t : t < 0.75f ? 2.0f - 4.0f*t : -4.0f + 4.0f*t); break;
                case 5: y = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f; break;
                case 6:
                    for (int s = 0; s < 5; ++s) { float tt = std::fmod(t*(1.0f+(s-2)*0.04f),1.0f); y += (1.0f-2.0f*tt)*0.25f; }
                    break;
                default: break;
            }
            const float px = (float)(r.getX() + i);
            const float py = cy - y * ampH;
            if (i == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
        }
        g.setColour(juce::Colour(0xffb9ff66u));
        g.strokePath(p, juce::PathStrokeType(1.5f));
        g.setColour(juce::Colour(0xff4a4a5au));
        g.drawLine((float)r.getX(), cy, (float)r.getRight(), cy, 0.5f);
    }

    void paintSectionHeader(juce::Graphics& g, int x, int y, int w,
                            const char* title, const char* sub)
    {
        using LF = StudioLookAndFeel;
        // Hairline + gradient pill background
        g.setColour(juce::Colour(0xffdcdce0u));
        g.drawLine((float)x, (float)y + (float)kSecHeaderH - 1.0f,
                   (float)(x + w), (float)y + (float)kSecHeaderH - 1.0f, 0.8f);

        g.setFont(LF::monoFont(8.5f, juce::Font::bold));
        g.setColour(juce::Colour(0xff333340u));
        g.drawText(title, x, y + 2, 130, kSecHeaderH - 4, juce::Justification::centredLeft);

        g.setFont(LF::monoFont(7.5f));
        g.setColour(juce::Colour(0xffaaaaB0u));
        g.drawText(juce::String::fromUTF8(sub), x + 132, y + 2, w - 132, kSecHeaderH - 4,
                   juce::Justification::centredLeft);
    }

    void paintSliderLabel(juce::Graphics& g, const char* txt, juce::uint32 col, int x, int y)
    {
        g.setFont(StudioLookAndFeel::monoFont(8.0f, juce::Font::bold));
        g.setColour(juce::Colour(col));
        g.drawText(txt, x, y + 2, kLblW - 4, kRowH - 2, juce::Justification::centredRight);
    }

    void setWaveform(int waveIdx)
    {
        synthParams_.waveform = waveIdx;
        updateWaveBtnColors();
        repaint();
        if (onSynthParamsChanged) onSynthParamsChanged(synthParams_);
    }

    void setFilterType(int typeIdx)
    {
        synthParams_.filterType = typeIdx;
        updateFiltBtnColors();
        if (onSynthParamsChanged) onSynthParamsChanged(synthParams_);
    }

    void updateWaveBtnColors()
    {
        static const int kIdx[] = { 0, 1, 2, 3, 5 };
        for (int i = 0; i < 5; ++i)
        {
            const bool on = (synthParams_.waveform == kIdx[i]);
            waveButtons_[i].setColour(juce::TextButton::buttonColourId,
                on ? juce::Colour(0xff3a3a4au) : juce::Colour(0xff252530u));
            waveButtons_[i].setColour(juce::TextButton::textColourOffId,
                on ? juce::Colour(0xffb9ff66u) : juce::Colour(0xff888890u));
        }
    }

    void updateFiltBtnColors()
    {
        static const int kIdx[] = { 0, 2, 1 }; // LP=0, BP=2, HP=1
        for (int i = 0; i < 3; ++i)
        {
            const bool on = (synthParams_.filterType == kIdx[i]);
            filtButtons_[i].setColour(juce::TextButton::buttonColourId,
                on ? juce::Colour(StudioLookAndFeel::kAccent) : juce::Colour(0xffe8e8ecu));
            filtButtons_[i].setColour(juce::TextButton::textColourOffId,
                on ? juce::Colours::white : juce::Colour(0xff444450u));
        }
    }

    void pullSynthParams()
    {
        synthParams_.unisonDetune    = (float)detSlider_.getValue();
        synthParams_.filterDrive     = (float)driveSlider_.getValue();
        synthParams_.cutoff          = (float)cutoffSlider_.getValue();
        synthParams_.resonance       = (float)resSlider_.getValue();
        synthParams_.filterEnvAmount = (float)envAmtSlider_.getValue();
        synthParams_.attack          = (float)attackSlider_.getValue();
        synthParams_.decay           = (float)decaySlider_.getValue();
        synthParams_.sustain         = (float)sustainSlider_.getValue();
        synthParams_.release         = (float)releaseSlider_.getValue();
    }

    void syncControls()
    {
        tuneSlider_   .setValue(tune_,                        juce::dontSendNotification);
        detSlider_    .setValue(synthParams_.unisonDetune,    juce::dontSendNotification);
        driveSlider_  .setValue(synthParams_.filterDrive,     juce::dontSendNotification);
        cutoffSlider_ .setValue(synthParams_.cutoff,          juce::dontSendNotification);
        resSlider_    .setValue(synthParams_.resonance,       juce::dontSendNotification);
        envAmtSlider_ .setValue(synthParams_.filterEnvAmount, juce::dontSendNotification);
        attackSlider_ .setValue(synthParams_.attack,          juce::dontSendNotification);
        decaySlider_  .setValue(synthParams_.decay,           juce::dontSendNotification);
        sustainSlider_.setValue(synthParams_.sustain,         juce::dontSendNotification);
        releaseSlider_.setValue(synthParams_.release,         juce::dontSendNotification);
    }

    void loadWaveform(const juce::String& path)
    {
        wfMin_.clear();
        wfMax_.clear();
        if (path.isEmpty()) return;
        juce::File file(path);
        if (!file.existsAsFile()) return;
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
        if (!reader) return;
        const int totalSamples = (int)juce::jmin(reader->lengthInSamples, (juce::int64)1000000);
        if (totalSamples < 2) return;
        constexpr int kBins = 240;
        const int spb = juce::jmax(1, totalSamples / kBins);
        juce::AudioBuffer<float> buf(1, spb);
        wfMin_.resize(kBins, 0.0f);
        wfMax_.resize(kBins, 0.0f);
        for (int bin = 0; bin < kBins; ++bin)
        {
            const juce::int64 start = (juce::int64)bin * spb;
            const int count = juce::jmin(spb, totalSamples - (int)start);
            if (count <= 0) break;
            buf.clear();
            reader->read(&buf, 0, count, start, true, false);
            float mn = 0.0f, mx = 0.0f;
            for (int s = 0; s < count; ++s) { const float v = buf.getSample(0,s); mn=juce::jmin(mn,v); mx=juce::jmax(mx,v); }
            wfMin_[bin] = mn;
            wfMax_[bin] = mx;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentPanel)
};

// =========================================================================

class MainComponent : public juce::Component,
                      public juce::Timer,
                      public juce::DragAndDropContainer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    StudioLookAndFeel    lookAndFeel;    // M11 — must be first so it outlives all components

    Project              project;
    AudioEngine          audioEngine;
    ToolbarComponent     toolbar;
    PlaylistComponent    playlist;
    juce::Viewport       playlistViewport;
    juce::ComboBox       playlistSnapBox;
    juce::TextButton     playlistZoomInBtn  { "+" };
    juce::TextButton     playlistZoomOutBtn { "-" };
    ChannelRackComponent channelRack;
    juce::Viewport       channelRackViewport;
    MixerComponent       mixer;

    // M15 — sample browser panel
    SampleBrowserComponent sampleBrowser;
    juce::Viewport         browserViewport;
    juce::TextButton       browserCollapseBtn;   // always-visible collapse/expand tab
    bool isBrowserOpen = true;                   // default: open on launch

    // M3 — floating piano roll window
    std::unique_ptr<PianoRollWindow> pianoRollWindow;
    int  pianoRollChannel = -1;
    bool showMixer = false;
    bool pianoRollPlaybackOverridesPlayMode = false;
    PlayMode playModeBeforePianoRollPlayback = PlayMode::Pattern;

    // M13/M14 — floating synth + FX editors
    std::unique_ptr<SynthEditorWindow> synthEditorWindow;
    std::unique_ptr<FXEditorWindow>    fxEditorWindow;
    std::unique_ptr<AutoTuneEditorWindow> autoTuneEditorWindow;
    int autoTuneEditorTrack = -1;
    int synthEditorChannel = -1;
    int fxEditorTrack      = -1;

    // Inspector tab bar (INSTRUMENT=0 / SEQUENCER=1 / MIXER=2)
    InspectorTabBar      inspectorTabBar_;
    StepInspectorStrip   stepInspector_;
    InstrumentPanel      instrumentPanel_;
    juce::Viewport       instrumentViewport_;
    int              inspectorTab_ = 1;   // default: SEQUENCER (channel rack)
    juce::Rectangle<int> rightPanelBounds_;        // cached in resized(), used in paint()
    juce::Rectangle<int> inspectorContentBounds_;  // area below tab bar (inspector content)

    // Launchpad — inline right panel (toggled)
    LaunchpadPanel   launchpadPanel;
    bool             showLaunchpad = false;

    std::unique_ptr<juce::DocumentWindow> audioDeviceWindow;

    // M8 — plugin browser + per-channel plugin editor windows
    std::unique_ptr<PluginBrowserWindow>                    pluginBrowserWindow;
    std::array<std::unique_ptr<PluginEditorWindow>, 16>     pluginEditorWindows;

    // Dynamic EQ windows (0-7 = mixer tracks, 8 = master)
    std::array<std::unique_ptr<DynamicEQWindow>, 9>         dynEQWindows;

    // Trackpad multitouch → launchpad pads 0-15
    TrackpadController trackpadController;

    // M2 — pattern management state
    int activePatternId = 1;

    // Pause/resume — saved positions when stopped (< 0 = no saved state)
    double pausedBarSong     = -1.0;
    double pausedPatternBeat = -1.0;
    int patternStartStep = 0;
    int pianoRollStartStep = 0;

    // Double-Space detection: timestamp when Space last resumed playback
    juce::int64 lastSpaceResumeTime = 0;

    Pattern* findPattern(int id);
    int      nextPatternId() const;
    void     selectPattern(int id);
    void     syncPatternToEngine();
    void     syncChannelRackToProject();
    int      ensureAutoBassChannel();
    void     focusPianoRollChannel(int channel);
    void     switchToPatternModeForEditing();
    void     followPlaylistPlayhead();
    void     beginPianoRollPatternPlayback();
    void     restorePlayModeAfterPianoRollPlayback();
    void     exportCurrentPianoRollToMidi();
    void     importCurrentPianoRollFromMidi();
    void     refreshSynthEditorPresetList(const juce::String& selectPresetName = {});

    // M6 — undo/redo
    juce::UndoManager undoManager;

    // M4 — file state
    juce::File currentFile;
    bool       projectDirty = false;

    // Rebuilds all UI from the current project (used after load / new)
    void reloadProjectIntoUI();
    void markDirty();

    // File operations
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();

    // Kept alive until async chooser completes
    std::shared_ptr<juce::FileChooser> fileChooser;

    bool recordTransitioning_ = false;
    bool loopRecordEnabled_   = false;   // mirrors audioEngine.isLoopRecording() for message thread
    bool liveMode_            = false;   // Live Performance Mode active state

    ClipLauncher clipLauncher_;          // Live Performance clip launch state (message thread)

    // Live Performance debug window
    std::unique_ptr<LivePerformanceWindow> liveDebugWindow_;
    std::unique_ptr<LiveLoopWindow>        liveLoopWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
