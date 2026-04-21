#pragma once

#include <JuceHeader.h>

namespace waive
{

class PathSanitizer
{
public:
    static juce::String sanitizePathComponent (const juce::String& component);
    static bool isWithinDirectory (const juce::File& candidate, const juce::File& allowedBase);
    static bool isValidIdentifier (const juce::String& str);
};

} // namespace waive
