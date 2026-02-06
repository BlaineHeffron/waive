#include "WaiveLookAndFeel.h"

namespace waive
{

WaiveLookAndFeel::WaiveLookAndFeel()
    : palette (makeDarkPalette())
{
    applyPalette();
}

void WaiveLookAndFeel::applyPalette()
{
    // Window
    setColour (juce::ResizableWindow::backgroundColourId, palette.windowBg);
    setColour (juce::DocumentWindow::textColourId, palette.textPrimary);

    // Button
    setColour (juce::TextButton::buttonColourId, palette.surfaceBg);
    setColour (juce::TextButton::buttonOnColourId, palette.primary);
    setColour (juce::TextButton::textColourOffId, palette.textPrimary);
    setColour (juce::TextButton::textColourOnId, palette.textOnPrimary);

    // Toggle button
    setColour (juce::ToggleButton::textColourId, palette.textPrimary);
    setColour (juce::ToggleButton::tickColourId, palette.primary);
    setColour (juce::ToggleButton::tickDisabledColourId, palette.textMuted);

    // Slider
    setColour (juce::Slider::backgroundColourId, palette.insetBg);
    setColour (juce::Slider::trackColourId, palette.primary);
    setColour (juce::Slider::thumbColourId, palette.textPrimary);
    setColour (juce::Slider::rotarySliderFillColourId, palette.primary);
    setColour (juce::Slider::rotarySliderOutlineColourId, palette.insetBg);
    setColour (juce::Slider::textBoxTextColourId, palette.textPrimary);
    setColour (juce::Slider::textBoxBackgroundColourId, palette.insetBg);
    setColour (juce::Slider::textBoxOutlineColourId, palette.borderSubtle);

    // Label
    setColour (juce::Label::textColourId, palette.textPrimary);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    // TextEditor
    setColour (juce::TextEditor::backgroundColourId, palette.insetBg);
    setColour (juce::TextEditor::textColourId, palette.textPrimary);
    setColour (juce::TextEditor::outlineColourId, palette.borderSubtle);
    setColour (juce::TextEditor::focusedOutlineColourId, palette.borderFocused);
    setColour (juce::TextEditor::highlightColourId, palette.selection);
    setColour (juce::TextEditor::highlightedTextColourId, palette.textOnPrimary);

    // ComboBox
    setColour (juce::ComboBox::backgroundColourId, palette.surfaceBg);
    setColour (juce::ComboBox::textColourId, palette.textPrimary);
    setColour (juce::ComboBox::outlineColourId, palette.borderSubtle);
    setColour (juce::ComboBox::arrowColourId, palette.textSecondary);
    setColour (juce::ComboBox::focusedOutlineColourId, palette.borderFocused);

    // ListBox
    setColour (juce::ListBox::backgroundColourId, palette.panelBg);
    setColour (juce::ListBox::textColourId, palette.textPrimary);
    setColour (juce::ListBox::outlineColourId, palette.borderSubtle);

    // PopupMenu
    setColour (juce::PopupMenu::backgroundColourId, palette.surfaceBg);
    setColour (juce::PopupMenu::textColourId, palette.textPrimary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, palette.primary);
    setColour (juce::PopupMenu::highlightedTextColourId, palette.textOnPrimary);

    // ScrollBar
    setColour (juce::ScrollBar::thumbColourId, palette.border);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    // TabbedComponent
    setColour (juce::TabbedButtonBar::tabOutlineColourId, palette.borderSubtle);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, palette.primary);
    setColour (juce::TabbedComponent::backgroundColourId, palette.panelBg);
    setColour (juce::TabbedComponent::outlineColourId, palette.borderSubtle);

    // Tooltip
    setColour (juce::TooltipWindow::backgroundColourId, palette.surfaceBg);
    setColour (juce::TooltipWindow::textColourId, palette.textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, palette.borderSubtle);

    // ProgressBar
    setColour (juce::ProgressBar::backgroundColourId, palette.insetBg);
    setColour (juce::ProgressBar::foregroundColourId, palette.primary);

    // MenuBar
    setColour (juce::PopupMenu::headerTextColourId, palette.textSecondary);
}

//==============================================================================
void WaiveLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                              const juce::Colour& backgroundColour,
                                              bool shouldDrawButtonAsHighlighted,
                                              bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;

    if (shouldDrawButtonAsDown)
        colour = colour.darker (0.05f);
    else if (shouldDrawButtonAsHighlighted)
        colour = colour.brighter (0.08f);

    if (button.getToggleState())
        colour = palette.primary;

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (palette.borderSubtle);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void WaiveLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    (void) minSliderPos;
    (void) maxSliderPos;

    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearVertical)
    {
        const bool isHorizontal = style == juce::Slider::LinearHorizontal;
        const float trackThickness = 4.0f;

        juce::Rectangle<float> trackBounds;
        if (isHorizontal)
        {
            float trackY = (float) y + ((float) height - trackThickness) * 0.5f;
            trackBounds = { (float) x, trackY, (float) width, trackThickness };
        }
        else
        {
            float trackX = (float) x + ((float) width - trackThickness) * 0.5f;
            trackBounds = { trackX, (float) y, trackThickness, (float) height };
        }

        // Track background
        g.setColour (palette.insetBg);
        g.fillRoundedRectangle (trackBounds, 2.0f);

        // Filled portion
        g.setColour (palette.primary);
        if (isHorizontal)
        {
            auto fillW = sliderPos - (float) x;
            g.fillRoundedRectangle (trackBounds.withWidth (juce::jmax (0.0f, fillW)), 2.0f);
        }
        else
        {
            auto fillH = (float) (y + height) - sliderPos;
            g.fillRoundedRectangle (trackBounds.getX(), sliderPos,
                                    trackBounds.getWidth(), juce::jmax (0.0f, fillH), 2.0f);
        }

        // Thumb
        const float thumbSize = 14.0f;
        juce::Point<float> thumbCentre;
        if (isHorizontal)
            thumbCentre = { sliderPos, trackBounds.getCentreY() };
        else
            thumbCentre = { trackBounds.getCentreX(), sliderPos };

        g.setColour (palette.textPrimary);
        g.fillEllipse (thumbCentre.x - thumbSize * 0.5f,
                        thumbCentre.y - thumbSize * 0.5f,
                        thumbSize, thumbSize);
    }
    else
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                           minSliderPos, maxSliderPos, style, slider);
    }
}

void WaiveLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPosProportional, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centreX = bounds.getCentreX();
    auto centreY = bounds.getCentreY();
    auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Background arc
    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centreX, centreY, radius, radius,
                                  0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (palette.insetBg);
    g.strokePath (backgroundArc, juce::PathStrokeType (3.0f));

    // Value arc
    juce::Path valueArc;
    valueArc.addCentredArc (centreX, centreY, radius, radius,
                             0.0f, rotaryStartAngle, angle, true);
    g.setColour (palette.primary);
    g.strokePath (valueArc, juce::PathStrokeType (3.0f));

    // Pointer dot
    auto pointerLength = radius * 0.7f;
    auto pointerX = centreX + pointerLength * std::cos (angle - juce::MathConstants<float>::halfPi);
    auto pointerY = centreY + pointerLength * std::sin (angle - juce::MathConstants<float>::halfPi);
    g.setColour (palette.textPrimary);
    g.fillEllipse (pointerX - 3.0f, pointerY - 3.0f, 6.0f, 6.0f);
}

void WaiveLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                                       bool isMouseOver, bool isMouseDown)
{
    auto bounds = button.getLocalBounds().toFloat();
    bool isFront = button.isFrontTab();

    // Background
    g.setColour (isFront ? palette.surfaceBg : palette.panelBg);
    if (isMouseDown)
        g.setColour (palette.surfaceBg.darker (0.05f));
    else if (isMouseOver && ! isFront)
        g.setColour (palette.panelBg.brighter (0.04f));

    g.fillRect (bounds);

    // Text
    g.setColour (isFront ? palette.textPrimary : palette.textMuted);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (button.getButtonText(), bounds.toNearestInt(), juce::Justification::centred, true);

    // Underline for active tab
    if (isFront)
    {
        g.setColour (palette.primary);
        g.fillRect (bounds.removeFromBottom (2.0f));
    }
}

void WaiveLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&,
                                         int width, int height,
                                         double progress, const juce::String& textToShow)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    // Track
    g.setColour (palette.insetBg);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Fill
    if (progress >= 0.0 && progress <= 1.0)
    {
        g.setColour (palette.primary);
        g.fillRoundedRectangle (bounds.withWidth (bounds.getWidth() * (float) progress), 4.0f);
    }

    // Text
    if (textToShow.isNotEmpty())
    {
        g.setColour (palette.textPrimary);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (textToShow, bounds.toNearestInt(), juce::Justification::centred, false);
    }
}

void WaiveLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                      int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (palette.surfaceBg);
    g.fillRoundedRectangle (bounds, 4.0f);

    auto outlineColour = box.hasKeyboardFocus (true) ? palette.borderFocused : palette.borderSubtle;
    g.setColour (outlineColour);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    // Chevron arrow
    auto arrowBounds = bounds.removeFromRight (24.0f).reduced (6.0f);
    juce::Path arrow;
    arrow.addTriangle (arrowBounds.getX(), arrowBounds.getCentreY() - 2.0f,
                        arrowBounds.getRight(), arrowBounds.getCentreY() - 2.0f,
                        arrowBounds.getCentreX(), arrowBounds.getCentreY() + 3.0f);
    g.setColour (isButtonDown ? palette.textPrimary : palette.textSecondary);
    g.fillPath (arrow);
}

void WaiveLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (palette.insetBg);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 4.0f);
}

void WaiveLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    auto colour = editor.hasKeyboardFocus (true) ? palette.borderFocused : palette.borderSubtle;
    g.setColour (colour);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
}

//==============================================================================
const ColourPalette* getWaivePalette (juce::Component& component)
{
    if (auto* lnf = dynamic_cast<WaiveLookAndFeel*> (&component.getLookAndFeel()))
        return &lnf->getPalette();

    return nullptr;
}

} // namespace waive
