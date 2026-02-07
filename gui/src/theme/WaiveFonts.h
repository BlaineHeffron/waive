#pragma once

#include <JuceHeader.h>

namespace waive
{

struct Fonts
{
    static juce::Font header()     { return juce::Font (juce::FontOptions (16.0f)); }
    static juce::Font subheader()  { return juce::Font (juce::FontOptions (13.0f, juce::Font::bold)); }
    static juce::Font body()       { return juce::Font (juce::FontOptions (13.0f)); }
    static juce::Font label()      { return juce::Font (juce::FontOptions (12.0f)); }
    static juce::Font caption()    { return juce::Font (juce::FontOptions (11.0f)); }
    static juce::Font mono()       { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)); }
    static juce::Font meter()      { return juce::Font (juce::FontOptions (10.0f)); }
};

} // namespace waive
