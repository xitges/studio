/*
  ==============================================================================
    StudioLookAndFeel.h  — M11 custom dark theme for Studio DAW
  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class StudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel()
    {
        // ---- Global palette
        setColour(juce::ResizableWindow::backgroundColourId,  juce::Colour(0xff11111f));

        // PopupMenu
        setColour(juce::PopupMenu::backgroundColourId,             juce::Colour(0xff1a1a2e));
        setColour(juce::PopupMenu::textColourId,                   juce::Colour(0xffecf0f1));
        setColour(juce::PopupMenu::highlightedBackgroundColourId,  juce::Colour(0xff0f3460));
        setColour(juce::PopupMenu::highlightedTextColourId,        juce::Colours::white);

        // AlertWindow / dialogs
        setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff1a1a2e));
        setColour(juce::AlertWindow::textColourId,       juce::Colour(0xffecf0f1));
        setColour(juce::AlertWindow::outlineColourId,    juce::Colour(0xff0f3460));

        // TextEditor
        setColour(juce::TextEditor::backgroundColourId,     juce::Colour(0xff11111f));
        setColour(juce::TextEditor::textColourId,           juce::Colour(0xffecf0f1));
        setColour(juce::TextEditor::outlineColourId,        juce::Colour(0xff3498db));
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff2980b9));
        setColour(juce::TextEditor::highlightColourId,      juce::Colour(0xff0f3460));

        // Label
        setColour(juce::Label::textColourId,       juce::Colour(0xffecf0f1));
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

        // ComboBox
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0f3460));
        setColour(juce::ComboBox::textColourId,       juce::Colours::white);
        setColour(juce::ComboBox::outlineColourId,    juce::Colours::transparentBlack);
        setColour(juce::ComboBox::arrowColourId,      juce::Colour(0xff3498db));
        setColour(juce::ComboBox::buttonColourId,     juce::Colour(0xff0f3460));

        // TextButton defaults
        setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2c2c54));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3498db));
        setColour(juce::TextButton::textColourOffId,  juce::Colours::white);
        setColour(juce::TextButton::textColourOnId,   juce::Colours::white);

        // Slider
        setColour(juce::Slider::rotarySliderFillColourId,    juce::Colour(0xff3498db));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2c2c54));
        setColour(juce::Slider::thumbColourId,               juce::Colour(0xff3498db));
        setColour(juce::Slider::trackColourId,               juce::Colour(0xff3498db));
        setColour(juce::Slider::backgroundColourId,          juce::Colour(0xff2c2c54));
        setColour(juce::Slider::textBoxTextColourId,         juce::Colour(0xffecf0f1));
        setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colour(0xff11111f));
        setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId,    juce::Colour(0xff0f3460));

        // ScrollBar
        setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff3498db));
        setColour(juce::ScrollBar::trackColourId, juce::Colour(0xff1a1a2e));

        // ToggleButton
        setColour(juce::ToggleButton::textColourId,  juce::Colour(0xffecf0f1));
        setColour(juce::ToggleButton::tickColourId,  juce::Colour(0xff3498db));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff555566));
    }

    // -------------------------------------------------------------------------
    // Button
    // -------------------------------------------------------------------------
    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                               const juce::Colour& bg,
                               bool isMouseOver, bool isButtonDown) override
    {
        const auto  bounds = btn.getLocalBounds().toFloat().reduced(0.5f);
        const float corner = 4.0f;

        juce::Colour fill = bg;
        if      (isButtonDown) fill = fill.brighter(0.35f);
        else if (isMouseOver)  fill = fill.brighter(0.14f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, corner);

        g.setColour(fill.brighter(0.22f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& btn,
                        bool /*isMouseOver*/, bool /*isButtonDown*/) override
    {
        const auto col = btn.getToggleState()
                         ? btn.findColour(juce::TextButton::textColourOnId)
                         : btn.findColour(juce::TextButton::textColourOffId);
        g.setColour(col);
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        g.drawFittedText(btn.getButtonText(),
                         btn.getLocalBounds().reduced(4, 2),
                         juce::Justification::centred, 1, 0.85f);
    }

    // -------------------------------------------------------------------------
    // Rotary Slider
    // -------------------------------------------------------------------------
    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const float radius  = (float)juce::jmin(width, height) / 2.0f - 4.0f;
        const float centreX = (float)x + (float)width  * 0.5f;
        const float centreY = (float)y + (float)height * 0.5f;
        const float angle   = rotaryStartAngle
                              + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Background track
        juce::Path bgArc;
        bgArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff2c2c54));
        g.strokePath(bgArc, juce::PathStrokeType(3.5f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value fill
        juce::Path arc;
        arc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                          rotaryStartAngle, angle, true);
        const juce::Colour fillCol = slider.findColour(juce::Slider::rotarySliderFillColourId);
        g.setColour(fillCol);
        g.strokePath(arc, juce::PathStrokeType(3.5f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Thumb dot
        const float thumbX = centreX + (radius - 5.0f)
                             * std::cos(angle - juce::MathConstants<float>::halfPi);
        const float thumbY = centreY + (radius - 5.0f)
                             * std::sin(angle - juce::MathConstants<float>::halfPi);
        g.setColour(fillCol.brighter(0.3f));
        g.fillEllipse(thumbX - 3.5f, thumbY - 3.5f, 7.0f, 7.0f);
    }

    // -------------------------------------------------------------------------
    // Linear Slider
    // -------------------------------------------------------------------------
    void drawLinearSliderBackground(juce::Graphics& g,
                                    int x, int y, int width, int height,
                                    float, float, float,
                                    juce::Slider::SliderStyle,
                                    juce::Slider&) override
    {
        const float trackH = 3.0f;
        const juce::Rectangle<float> track(
            (float)x,
            (float)y + (float)height * 0.5f - trackH * 0.5f,
            (float)width, trackH);
        g.setColour(juce::Colour(0xff2c2c54));
        g.fillRoundedRectangle(track, trackH * 0.5f);
    }

    void drawLinearSliderThumb(juce::Graphics& g,
                               int x, int y, int width, int height,
                               float sliderPos, float, float,
                               juce::Slider::SliderStyle style,
                               juce::Slider& slider) override
    {
        const float thumbR = 5.0f;
        juce::Point<float> pt;
        if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
            pt = { sliderPos, (float)y + (float)height * 0.5f };
        else
            pt = { (float)x + (float)width * 0.5f, sliderPos };

        const juce::Colour tc = slider.findColour(juce::Slider::thumbColourId);
        g.setColour(tc);
        g.fillEllipse(pt.x - thumbR, pt.y - thumbR, thumbR * 2.0f, thumbR * 2.0f);
        g.setColour(tc.brighter(0.4f));
        g.drawEllipse(pt.x - thumbR, pt.y - thumbR, thumbR * 2.0f, thumbR * 2.0f, 1.0f);
    }

    // -------------------------------------------------------------------------
    // Scrollbar — thin, dark
    // -------------------------------------------------------------------------
    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&,
                       int x, int y, int width, int height,
                       bool /*isScrollbarVertical*/,
                       int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override
    {
        const juce::Rectangle<float> bounds((float)x, (float)y,
                                            (float)width, (float)height);
        g.setColour(juce::Colour(0xff1a1a2e));
        g.fillRoundedRectangle(bounds, 3.0f);

        if (thumbSize > 0)
        {
            const bool vert = (height > width);
            juce::Rectangle<float> thumb;
            if (vert)
                thumb = { (float)x + 2.0f, (float)(y + thumbStartPosition),
                          (float)width - 4.0f, (float)thumbSize };
            else
                thumb = { (float)(x + thumbStartPosition), (float)y + 2.0f,
                          (float)thumbSize, (float)height - 4.0f };

            const float alpha = isMouseDown ? 0.9f : (isMouseOver ? 0.7f : 0.5f);
            g.setColour(juce::Colour(0xff3498db).withAlpha(alpha));
            g.fillRoundedRectangle(thumb, 3.0f);
        }
    }

    int getDefaultScrollbarWidth() override { return 8; }

    // -------------------------------------------------------------------------
    // ComboBox popup arrow
    // -------------------------------------------------------------------------
    void drawComboBox(juce::Graphics& g, int width, int height,
                      bool, int, int, int, int,
                      juce::ComboBox& box) override
    {
        const juce::Rectangle<float> bounds(0.0f, 0.0f, (float)width, (float)height);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(bounds, 4.0f);

        const float arrowSize = 8.0f;
        const float arrowX = (float)width - arrowSize - 6.0f;
        const float arrowY = ((float)height - arrowSize * 0.6f) * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath(arrowX, arrowY);
        arrow.lineTo(arrowX + arrowSize * 0.5f, arrowY + arrowSize * 0.6f);
        arrow.lineTo(arrowX + arrowSize, arrowY);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.strokePath(arrow, juce::PathStrokeType(1.5f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // -------------------------------------------------------------------------
    // AlertWindow — dark modal dialog
    // -------------------------------------------------------------------------
    juce::AlertWindow* createAlertWindow(const juce::String& title,
                                         const juce::String& message,
                                         const juce::String& btn1,
                                         const juce::String& btn2,
                                         const juce::String& btn3,
                                         juce::MessageBoxIconType iconType,
                                         int numButtons,
                                         juce::Component* associatedComp) override
    {
        auto* aw = LookAndFeel_V4::createAlertWindow(
            title, message, btn1, btn2, btn3, iconType, numButtons, associatedComp);
        return aw;
    }

    void drawAlertBox(juce::Graphics& g, juce::AlertWindow& aw,
                      const juce::Rectangle<int>& textArea,
                      juce::TextLayout& textLayout) override
    {
        g.setColour(aw.findColour(juce::AlertWindow::backgroundColourId));
        g.fillRoundedRectangle(aw.getLocalBounds().toFloat(), 6.0f);
        g.setColour(aw.findColour(juce::AlertWindow::outlineColourId));
        g.drawRoundedRectangle(aw.getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
        textLayout.draw(g, textArea.toFloat());
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioLookAndFeel)
};
