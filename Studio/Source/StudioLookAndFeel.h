/*
  ==============================================================================
    StudioLookAndFeel.h  —  Retro Hardware theme (Cream / Red)
    Inspired by FIELDLAB TR-1 tabletop DAW aesthetic
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class StudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ---- Palette : cream chassis + red accent + phosphor display -------------
    static constexpr juce::uint32
        kChassis    = 0xffe6dec9,   // warm cream chassis
        kChassis2   = 0xffddd3ba,   // slightly darker cream
        kPanel      = 0xfff3ecda,   // lighter panel surface
        kPanelRim   = 0xffc7bb9a,   // warm tan border
        kDark       = 0xff2a251e,   // dark pad / step button
        kDarkMid    = 0xff1a1612,   // near-black
        kDarkDeep   = 0xff0a0805,   // deepest dark
        kAccent     = 0xffd8412a,   // saturated red
        kAccentHi   = 0xfff06b54,   // lighter red
        kText       = 0xff1a1612,   // near-black ink
        kTextDim    = 0xff4a4338,   // warm brown mid
        kTextFaint  = 0xff8c8170,   // muted brown
        kDisplayBg  = 0xff0d1410,   // near-black display bg
        kDisplayFg  = 0xffb9ff66,   // phosphor green
        kLedRed     = 0xffff4233,
        kLedAmber   = 0xffffb020,
        kLedGreen   = 0xff6cff6c,
        kLedOff     = 0xff6e624a;

    // Convenience color factories
    static juce::Colour chassis()   { return juce::Colour(kChassis); }
    static juce::Colour panel()     { return juce::Colour(kPanel); }
    static juce::Colour panelRim()  { return juce::Colour(kPanelRim); }
    static juce::Colour accent()    { return juce::Colour(kAccent); }
    static juce::Colour ink()       { return juce::Colour(kText); }
    static juce::Colour inkSoft()   { return juce::Colour(kTextDim); }
    static juce::Colour inkFaint()  { return juce::Colour(kTextFaint); }
    static juce::Colour displayBg() { return juce::Colour(kDisplayBg); }
    static juce::Colour displayFg() { return juce::Colour(kDisplayFg); }
    static juce::Colour topLight()  { return juce::Colours::white.withAlpha(0.5f); }
    static juce::Colour rimLight()  { return juce::Colours::white.withAlpha(0.3f); }
    static juce::Font monoFont(float height, int styleFlags = juce::Font::plain)
    {
        const bool bold = (styleFlags & juce::Font::bold) != 0;
        static auto regularFace = juce::Typeface::createSystemTypefaceFor(
            BinaryData::JetBrainsMonoRegular_ttf, (size_t)BinaryData::JetBrainsMonoRegular_ttfSize);
        static auto boldFace = juce::Typeface::createSystemTypefaceFor(
            BinaryData::JetBrainsMonoBold_ttf, (size_t)BinaryData::JetBrainsMonoBold_ttfSize);
        return juce::Font(juce::FontOptions(bold ? boldFace : regularFace).withHeight(height));
    }
    static juce::Font brandFont(float height, int styleFlags = juce::Font::plain)
    {
        return monoFont(height, styleFlags);   // fallback: JetBrains Mono
    }
    static juce::Font displayFont(float height, int styleFlags = juce::Font::plain)
    {
        static auto face = juce::Typeface::createSystemTypefaceFor(
            BinaryData::VT323Regular_ttf, (size_t)BinaryData::VT323Regular_ttfSize);
        return juce::Font(juce::FontOptions(face).withHeight(height));
    }

    StudioLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(kChassis));

        setColour(juce::PopupMenu::backgroundColourId,            juce::Colour(kPanel));
        setColour(juce::PopupMenu::textColourId,                  juce::Colour(kText));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(kAccent));
        setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colour(0xffffffff));

        setColour(juce::AlertWindow::backgroundColourId, juce::Colour(kPanel));
        setColour(juce::AlertWindow::textColourId,       juce::Colour(kText));
        setColour(juce::AlertWindow::outlineColourId,    juce::Colour(kPanelRim));

        setColour(juce::TextEditor::backgroundColourId,     juce::Colour(kDisplayBg));
        setColour(juce::TextEditor::textColourId,           juce::Colour(kDisplayFg));
        setColour(juce::TextEditor::outlineColourId,        juce::Colour(kPanelRim));
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kAccent));
        setColour(juce::TextEditor::highlightColourId,      juce::Colour(kAccent).withAlpha(0.25f));

        setColour(juce::Label::textColourId,       juce::Colour(kText));
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        setColour(juce::ComboBox::backgroundColourId, juce::Colour(kChassis2));
        setColour(juce::ComboBox::textColourId,       juce::Colour(kText));
        setColour(juce::ComboBox::outlineColourId,    juce::Colour(kPanelRim));
        setColour(juce::ComboBox::arrowColourId,      juce::Colour(kTextDim));

        setColour(juce::TextButton::buttonColourId,   juce::Colour(kChassis));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(kAccent));
        setColour(juce::TextButton::textColourOffId,  juce::Colour(kText));
        setColour(juce::TextButton::textColourOnId,   juce::Colour(0xffffffff));

        setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(kAccent));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(kDark));
        setColour(juce::Slider::thumbColourId,               juce::Colour(kPanel));
        setColour(juce::Slider::trackColourId,               juce::Colour(kAccent));
        setColour(juce::Slider::backgroundColourId,          juce::Colour(kDark));
        setColour(juce::Slider::textBoxTextColourId,         juce::Colour(kTextDim));
        setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colour(kChassis2));
        setColour(juce::Slider::textBoxOutlineColourId,      juce::Colour(kPanelRim));
        setColour(juce::Slider::textBoxHighlightColourId,    juce::Colour(kAccent).withAlpha(0.22f));

        setColour(juce::ScrollBar::thumbColourId, juce::Colour(kPanelRim).withAlpha(0.7f));

        setColour(juce::ToggleButton::textColourId,         juce::Colour(kText));
        setColour(juce::ToggleButton::tickColourId,         juce::Colour(kAccent));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(kTextFaint));
    }

    // =========================================================================
    // Button — cream hardware key with inset press + per-button active color
    // =========================================================================
    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                              const juce::Colour& /*bg*/,
                              bool isMouseOver, bool isButtonDown) override
    {
        const auto  b = btn.getLocalBounds().toFloat().reduced(0.5f);
        const float r = 6.0f;
        const bool  isOn = btn.getToggleState();

        // Active color: per-button (buttonOnColourId), falls back to kAccent
        const juce::Colour onCol = btn.findColour(juce::TextButton::buttonOnColourId,
                                                   true);

        if (isOn)
        {
            // Active: radial gradient with per-button LED color (reference style)
            const float cx = b.getX() + b.getWidth() * 0.3f;
            const float cy = b.getY() + b.getHeight() * 0.25f;
            juce::ColourGradient grad(onCol.withAlpha(0.85f), cx, cy,
                                      onCol.withAlpha(0.55f), b.getRight(), b.getBottom(), true);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(b, r);

            // Inset top highlight
            g.setColour(juce::Colours::white.withAlpha(0.35f));
            g.drawLine(b.getX() + r, b.getY() + 1.0f, b.getRight() - r, b.getY() + 1.0f, 1.0f);
            // Inner bottom shadow
            g.setColour(juce::Colours::black.withAlpha(0.22f));
            g.drawLine(b.getX() + r, b.getBottom() - 1.5f, b.getRight() - r, b.getBottom() - 1.5f, 1.5f);

            // Outer glow ring (simulate box-shadow glow)
            g.setColour(onCol.withAlpha(0.32f));
            g.drawRoundedRectangle(b.expanded(1.5f), r + 1.5f, 2.0f);
            g.setColour(onCol.withAlpha(0.14f));
            g.drawRoundedRectangle(b.expanded(3.0f), r + 3.0f, 2.5f);
        }
        else if (isButtonDown)
        {
            // Pressed: darker cream, inset shadow
            juce::ColourGradient grad(juce::Colour(kPanelRim), 0.0f, b.getY(),
                                      juce::Colour(kChassis2), 0.0f, b.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(b, r);
            g.setColour(juce::Colours::black.withAlpha(0.2f));
            g.fillRoundedRectangle(b.withHeight(5.0f), r);
        }
        else
        {
            // Normal: cream gradient (reference: #f3ecda → #c7bb9a 60% → #8d8268)
            juce::ColourGradient grad(juce::Colour(kPanel).brighter(0.06f), 0.0f, b.getY(),
                                      juce::Colour(0xff8d8268),             0.0f, b.getBottom(), false);
            grad.addColour(0.6, juce::Colour(kPanelRim));
            g.setGradientFill(grad);
            g.fillRoundedRectangle(b, r);

            // Top specular
            g.setColour(juce::Colours::white.withAlpha(0.55f));
            g.drawLine(b.getX() + r, b.getY() + 1.0f, b.getRight() - r, b.getY() + 1.0f, 1.0f);
            // Bottom shadow
            g.setColour(juce::Colours::black.withAlpha(0.38f));
            g.drawLine(b.getX() + r, b.getBottom() + 0.5f, b.getRight() - r, b.getBottom() + 0.5f, 1.5f);

            if (isMouseOver)
            {
                g.setColour(juce::Colours::white.withAlpha(0.12f));
                g.fillRoundedRectangle(b, r);
            }
        }

        // Outer rim (1px dark border)
        g.setColour(juce::Colour(0x80000000));
        g.drawRoundedRectangle(b, r, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& btn,
                        bool /*isMouseOver*/, bool isButtonDown) override
    {
        const bool   isOn  = btn.getToggleState();
        const juce::String text = btn.getButtonText();

        // Transport icon buttons: draw path icons instead of text
        static const juce::StringArray iconKeys {
            "PLAY", "PAUSE", "STOP", "REC", "REW", "FF", "LOOP"
        };
        if (iconKeys.contains(text))
        {
            const float W = (float)btn.getWidth();
            const float H = (float)btn.getHeight();
            const float cx = W * 0.5f, cy = H * 0.5f;

            // Icon color: white when active, dark ink otherwise
            juce::Colour col = isOn ? juce::Colour(0xffffffff)
                                    : juce::Colour(kText).withAlpha(isButtonDown ? 0.6f : 1.0f);
            g.setColour(col);

            if (text == "PLAY")
            {
                juce::Path p;
                p.addTriangle(cx - W*0.22f, cy - H*0.30f,
                              cx + W*0.30f, cy,
                              cx - W*0.22f, cy + H*0.30f);
                g.fillPath(p);
            }
            else if (text == "PAUSE")
            {
                const float bw = W * 0.15f, bh = H * 0.52f;
                g.fillRect(cx - W*0.22f, cy - bh*0.5f, bw, bh);
                g.fillRect(cx + W*0.07f, cy - bh*0.5f, bw, bh);
            }
            else if (text == "STOP")
            {
                const float s = juce::jmin(W, H) * 0.44f;
                g.fillRoundedRectangle(cx - s*0.5f, cy - s*0.5f, s, s, 1.5f);
            }
            else if (text == "REC")
            {
                const float rr = juce::jmin(W, H) * 0.28f;
                g.fillEllipse(cx - rr, cy - rr, rr*2.0f, rr*2.0f);
            }
            else if (text == "REW")
            {
                // Reference: one left-pointing triangle + vertical bar on left
                // SVG: polygon(11,2 5,7 11,12) + rect(3,2 w2 h10) in 14x14 viewBox
                const float sc = juce::jmin(W, H) / 14.0f;
                const float ox = cx - 7.0f * sc, oy = cy - 7.0f * sc;
                juce::Path p;
                p.addTriangle(ox + 11*sc, oy + 2*sc,
                              ox +  5*sc, oy + 7*sc,
                              ox + 11*sc, oy + 12*sc);
                g.fillPath(p);
                g.fillRect(ox + 3*sc, oy + 2*sc, 2*sc, 10*sc);
            }
            else if (text == "FF")
            {
                // Reference: one right-pointing triangle + vertical bar on right
                // SVG: polygon(3,2 9,7 3,12) + rect(9,2 w2 h10) in 14x14 viewBox
                const float sc = juce::jmin(W, H) / 14.0f;
                const float ox = cx - 7.0f * sc, oy = cy - 7.0f * sc;
                juce::Path p;
                p.addTriangle(ox + 3*sc, oy + 2*sc,
                              ox + 9*sc, oy + 7*sc,
                              ox + 3*sc, oy + 12*sc);
                g.fillPath(p);
                g.fillRect(ox + 9*sc, oy + 2*sc, 2*sc, 10*sc);
            }
            else if (text == "LOOP")
            {
                // Reference: arc M2 7 a5 5 0 1 1 5 5 in 14x14 viewBox
                // Center (7,7), start (2,7)=left, end (7,12)=bottom, ~270° clockwise arc
                // Arrowhead polyline: (5,11)→(7,12)→(8,10)
                const float sc  = juce::jmin(W, H) / 14.0f;
                const float ox  = cx - 7.0f * sc, oy = cy - 7.0f * sc;
                const float rr  = 5.0f * sc;
                const float acx = ox + 7*sc, acy = oy + 7*sc;  // arc circle center
                // Arc: from 180° (left) clockwise to 90° (downward in screen coords)
                // In JUCE: angles measured from 12 o'clock, clockwise
                // 180° screen = startAngle=pi (JUCE: measured from 3 o'clock CCW... use addArc)
                // addArc uses standard math angles: 0=right, pi/2=up, pi=left, 3pi/2=down
                // screen 180° (left) = math angle pi; screen 90° down = math angle -pi/2 (= 3pi/2)
                // We want 270° clockwise = -270° = from pi to -pi/2 going clockwise (decreasing angle)
                // JUCE addArc(x,y,w,h, fromAngle, toAngle, startNewSubPath):
                //   angles clockwise from 12 o'clock (top). fromAngle < toAngle = clockwise.
                // Top=0, Right=pi/2, Bottom=pi, Left=3pi/2 in JUCE convention.
                // Start at Left(3pi/2), going clockwise 270° to Bottom (adding 270°=3pi/2 → wraps to pi)
                // Actually easier: start pi (left in standard), end -pi/2 (down standard), clockwise → decreasing = not clockwise
                // Let's just use 3/4 arc from 270° to 540° (=180°) in JUCE coords (0=top, CW)
                // JUCE: left = 3pi/2, bottom = pi, right = pi/2, top = 0
                // Start (2,7) = left → JUCE angle = 3*pi/2
                // End (7,12) = bottom → JUCE angle = pi
                // Clockwise 270°: from 3pi/2 going +270° → 3pi/2 + 3pi/2 = 3pi = pi (after mod 2pi)
                // So fromAngle=3pi/2, toAngle=3pi/2+3pi/2=3pi
                juce::Path arc;
                const float pi = juce::MathConstants<float>::pi;
                arc.addArc(acx - rr, acy - rr, rr*2.0f, rr*2.0f,
                           3.0f*pi/2.0f, 3.0f*pi/2.0f + 3.0f*pi/2.0f, true);
                juce::PathStrokeType(1.4f * sc).createStrokedPath(arc, arc);
                g.fillPath(arc);
                // Arrowhead: polyline (5,11)→(7,12)→(8,10)
                juce::Path arrow;
                arrow.startNewSubPath(ox + 5*sc, oy + 11*sc);
                arrow.lineTo(ox + 7*sc, oy + 12*sc);
                arrow.lineTo(ox + 8*sc, oy + 10*sc);
                juce::PathStrokeType(1.4f * sc).createStrokedPath(arrow, arrow);
                g.fillPath(arrow);
            }
            return;
        }

        // Regular text buttons
        juce::Colour col = isOn ? btn.findColour(juce::TextButton::textColourOnId)
                                : btn.findColour(juce::TextButton::textColourOffId);
        if (isButtonDown) col = col.withAlpha(0.75f);
        g.setColour(col);
        const float h = btn.getHeight() <= 18 ? 8.5f : btn.getHeight() <= 28 ? 9.5f : 11.0f;
        g.setFont(monoFont(h, juce::Font::bold));
        g.drawFittedText(text, btn.getLocalBounds().reduced(5, 2),
                         juce::Justification::centred, 1, 0.85f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        const float h = buttonHeight <= 18 ? 8.5f : buttonHeight <= 28 ? 9.5f : 11.0f;
        return monoFont(h, juce::Font::bold);
    }

    // =========================================================================
    // Rotary — dark hardware knob with red indicator, knurling ring
    // =========================================================================
    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const float radius = (float)juce::jmin(width, height) * 0.5f - 4.0f;
        const float cx     = (float)x + (float)width  * 0.5f;
        const float cy     = (float)y + (float)height * 0.5f;
        const float angle  = rotaryStartAngle
                             + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float pi     = juce::MathConstants<float>::pi;

        // Drop shadow
        g.setColour(juce::Colour(0x60000000));
        g.fillEllipse(cx - radius, cy - radius + 2.5f, radius * 2.0f, radius * 2.0f);

        // Outer dark body (dark brown-black like hardware knob)
        {
            juce::ColourGradient body(
                juce::Colour(0xff4a4338), cx - radius * 0.5f, cy - radius * 0.5f,
                juce::Colour(0xff1a1612), cx + radius * 0.5f, cy + radius * 0.5f, true);
            g.setGradientFill(body);
            g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        }

        // Outer border
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.0f);

        // Top specular highlight arc
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawEllipse(cx - radius + 1.5f, cy - radius + 1.5f,
                      (radius - 1.5f) * 2.0f, (radius - 1.5f) * 2.0f, 1.0f);

        // Track ring (dark groove)
        {
            juce::Path bgArc;
            bgArc.addCentredArc(cx, cy, radius - 5.0f, radius - 5.0f, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(juce::Colour(kDarkDeep));
            g.strokePath(bgArc, juce::PathStrokeType(3.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Value arc (red accent)
        {
            const juce::Colour fill = slider.findColour(juce::Slider::rotarySliderFillColourId);
            juce::Path arc;
            arc.addCentredArc(cx, cy, radius - 5.0f, radius - 5.0f, 0.0f,
                              rotaryStartAngle, angle, true);
            g.setColour(fill.withAlpha(0.15f));
            g.strokePath(arc, juce::PathStrokeType(7.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(fill);
            g.strokePath(arc, juce::PathStrokeType(2.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Indicator needle (white for accent knobs, red otherwise)
        {
            const float dx  = std::cos(angle - pi * 0.5f);
            const float dy  = std::sin(angle - pi * 0.5f);
            const float in  = radius * 0.22f;
            const float out = radius - 6.0f;
            g.setColour(juce::Colour(kAccent));
            g.drawLine(cx + dx * in, cy + dy * in,
                       cx + dx * out, cy + dy * out, 2.0f);
            // Small bright tip
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.fillEllipse(cx + dx * out - 1.5f, cy + dy * out - 1.5f, 3.0f, 3.0f);
        }

        // Centre hub cap
        {
            const float hr = radius * 0.16f;
            juce::ColourGradient hub(
                juce::Colour(0xff2a251e), cx - hr, cy - hr,
                juce::Colour(0xff0a0806), cx + hr, cy + hr, true);
            g.setGradientFill(hub);
            g.fillEllipse(cx - hr, cy - hr, hr * 2.0f, hr * 2.0f);
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawEllipse(cx - hr, cy - hr, hr * 2.0f, hr * 2.0f, 0.5f);
        }
    }

    // =========================================================================
    // Linear Slider — dark groove track, cream fader thumb
    // =========================================================================
    void drawLinearSliderBackground(juce::Graphics& g,
                                    int x, int y, int width, int height,
                                    float sliderPos, float, float,
                                    juce::Slider::SliderStyle style,
                                    juce::Slider& slider) override
    {
        const float tt  = 3.0f;
        const bool  isV = (style == juce::Slider::LinearVertical);
        const juce::Colour fill = slider.findColour(juce::Slider::trackColourId);

        juce::Rectangle<float> track;
        if (isV)
            track = { (float)x + (float)width * 0.5f - tt * 0.5f,
                      (float)y, tt, (float)height };
        else
            track = { (float)x, (float)y + (float)height * 0.5f - tt * 0.5f,
                      (float)width, tt };

        // Inset dark groove
        g.setColour(juce::Colour(kDarkDeep));
        g.fillRoundedRectangle(track.expanded(1.0f), tt * 0.5f + 1.0f);
        g.setColour(juce::Colour(0xff1a1410));
        g.fillRoundedRectangle(track, tt * 0.5f);

        // Fill
        juce::Rectangle<float> filled;
        if (isV)
            filled = { track.getX(), sliderPos, tt, (float)(y + height) - sliderPos };
        else
            filled = { track.getX(), track.getY(), sliderPos - (float)x, tt };

        if (filled.getWidth() > 0 && filled.getHeight() > 0)
        {
            g.setColour(fill.withAlpha(0.14f));
            g.fillRoundedRectangle(filled.expanded(isV ? 2.0f : 0.0f,
                                                   isV ? 0.0f : 2.0f), tt);
            g.setColour(fill);
            g.fillRoundedRectangle(filled, tt * 0.5f);
        }
    }

    void drawLinearSliderThumb(juce::Graphics& g,
                               int x, int y, int width, int height,
                               float sliderPos, float, float,
                               juce::Slider::SliderStyle style,
                               juce::Slider&) override
    {
        const bool isV = (style == juce::Slider::LinearVertical);

        if (isV)
        {
            // Vertical fader — wide rect thumb (hardware fader cap style)
            const float tw = 22.0f, th = 14.0f;
            const float tx = (float)x + (float)width * 0.5f - tw * 0.5f;
            const float ty = sliderPos - th * 0.5f;

            // Shadow
            g.setColour(juce::Colour(0x50000000));
            g.fillRoundedRectangle(tx + 1.0f, ty + 2.0f, tw, th, 3.0f);

            // Cream fader cap gradient
            juce::ColourGradient cap(
                juce::Colour(kPanel),     tx, ty,
                juce::Colour(kChassis2),  tx, ty + th * 0.55f, false);
            cap.addColour(0.55, juce::Colour(kChassis2));
            cap.addColour(1.0,  juce::Colour(0xff8d8268));
            g.setGradientFill(cap);
            g.fillRoundedRectangle(tx, ty, tw, th, 3.0f);

            // Center accent stripe
            g.setColour(juce::Colour(kAccent));
            g.fillRoundedRectangle(tx + 3.0f, ty + th * 0.5f - 1.0f, tw - 6.0f, 2.0f, 1.0f);

            // Top highlight
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawLine(tx + 3.0f, ty + 1.0f, tx + tw - 3.0f, ty + 1.0f, 1.0f);

            // Border
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawRoundedRectangle(tx, ty, tw, th, 3.0f, 1.0f);
        }
        else
        {
            // Horizontal — round thumb
            const float tr = 8.0f;
            const juce::Point<float> pt { sliderPos, (float)y + (float)height * 0.5f };

            g.setColour(juce::Colour(0x44000000));
            g.fillEllipse(pt.x - tr + 1.0f, pt.y - tr + 2.0f, tr * 2.0f, tr * 2.0f);

            juce::ColourGradient face(
                juce::Colour(kPanel),    pt.x - tr * 0.4f, pt.y - tr * 0.4f,
                juce::Colour(kChassis2), pt.x + tr * 0.4f, pt.y + tr * 0.4f, true);
            g.setGradientFill(face);
            g.fillEllipse(pt.x - tr, pt.y - tr, tr * 2.0f, tr * 2.0f);

            g.setColour(juce::Colours::black.withAlpha(0.4f));
            g.drawEllipse(pt.x - tr, pt.y - tr, tr * 2.0f, tr * 2.0f, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.drawEllipse(pt.x - tr + 1.0f, pt.y - tr + 1.0f,
                          (tr - 1.0f) * 2.0f, (tr - 1.0f) * 2.0f, 0.5f);
        }
    }

    // =========================================================================
    // Scrollbar
    // =========================================================================
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&,
                       int x, int y, int width, int height,
                       bool, int thumbStart, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override
    {
        if (thumbSize <= 0) return;
        const bool vert = (height > width);
        juce::Rectangle<float> thumb;
        if (vert)
            thumb = { (float)x + 2.0f, (float)(y + thumbStart),
                      (float)width - 4.0f, (float)thumbSize };
        else
            thumb = { (float)(x + thumbStart), (float)y + 2.0f,
                      (float)thumbSize, (float)height - 4.0f };

        const float a = isMouseDown ? 0.55f : (isMouseOver ? 0.38f : 0.22f);
        g.setColour(juce::Colour(kTextDim).withAlpha(a));
        g.fillRoundedRectangle(thumb, 3.0f);
    }

    int getDefaultScrollbarWidth() override { return 8; }

    // =========================================================================
    // ComboBox
    // =========================================================================
    void drawComboBox(juce::Graphics& g, int width, int height,
                      bool, int, int, int, int,
                      juce::ComboBox& box) override
    {
        const juce::Rectangle<float> b(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f);
        const float r = 5.0f;
        const juce::Colour bg = box.findColour(juce::ComboBox::backgroundColourId);

        juce::ColourGradient grad(bg.brighter(0.06f), 0.0f, b.getY(),
                                  bg.darker(0.04f),   0.0f, b.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(b, r);

        g.setColour(juce::Colour(kPanelRim));
        g.drawRoundedRectangle(b, r, 1.0f);

        const float ax = (float)width - 16.0f;
        const float ay = (float)height * 0.5f - 2.5f;
        juce::Path chevron;
        chevron.startNewSubPath(ax, ay);
        chevron.lineTo(ax + 5.0f, ay + 5.0f);
        chevron.lineTo(ax + 10.0f, ay);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.strokePath(chevron, juce::PathStrokeType(1.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox& box) override
    {
        return monoFont((float)juce::jlimit(9, 12, box.getHeight() - 8), juce::Font::bold);
    }

    juce::Font getLabelFont(juce::Label& label) override
    {
        return monoFont((float)juce::jlimit(8, 12, label.getHeight() - 4),
                        label.getFont().boldened().isBold() ? juce::Font::bold : juce::Font::plain);
    }

    juce::Font getPopupMenuFont() override
    {
        return monoFont(11.0f, juce::Font::bold);
    }

    // =========================================================================
    // AlertWindow
    // =========================================================================
    void drawAlertBox(juce::Graphics& g, juce::AlertWindow& aw,
                      const juce::Rectangle<int>& textArea,
                      juce::TextLayout& textLayout) override
    {
        const auto b = aw.getLocalBounds().toFloat();
        g.setColour(juce::Colour(kPanel));
        g.fillRoundedRectangle(b, 10.0f);
        g.setColour(juce::Colour(kPanelRim));
        g.drawRoundedRectangle(b.reduced(0.5f), 10.0f, 1.0f);
        textLayout.draw(g, textArea.toFloat());
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioLookAndFeel)
};
