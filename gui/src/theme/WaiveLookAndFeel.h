#pragma once

#include <JuceHeader.h>
#include "WaiveColours.h"

namespace waive
{

class WaiveLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WaiveLookAndFeel();
    ~WaiveLookAndFeel() override = default;

    const ColourPalette& getPalette() const { return palette; }

    // Button
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    // Linear slider
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    // Rotary slider
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    // Tab button
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;

    // Progress bar
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&,
                          int width, int height,
                          double progress, const juce::String& textToShow) override;

    // ComboBox
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    // TextEditor
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

private:
    void applyPalette();

    ColourPalette palette;
};

/** Returns the WaiveLookAndFeel palette from a component's current LookAndFeel,
    or nullptr if not using WaiveLookAndFeel. */
const ColourPalette* getWaivePalette (juce::Component& component);

} // namespace waive
