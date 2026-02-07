#include "PathSanitizer.h"

namespace waive
{

juce::String PathSanitizer::sanitizePathComponent (const juce::String& component)
{
    if (component.isEmpty())
    {
        juce::Logger::writeToLog ("PathSanitizer: component is empty");
        return juce::String();
    }

    // Reject components containing dangerous characters
    if (component.contains ("..") ||
        component.contains ("/") ||
        component.contains ("\\"))
    {
        juce::Logger::writeToLog ("PathSanitizer: component contains dangerous chars: " + component);
        return juce::String();
    }

    // Reject control characters (0x00-0x1F, 0x7F)
    for (int i = 0; i < component.length(); ++i)
    {
        auto ch = component[i];
        if (ch < 0x20 || ch == 0x7F)
        {
            juce::Logger::writeToLog ("PathSanitizer: component has control char at index " + juce::String(i) + ": " + component);
            return juce::String();
        }
    }

    return component;
}

bool PathSanitizer::isWithinDirectory (const juce::File& candidate, const juce::File& allowedBase)
{
    // Resolve to canonical paths (follows symlinks)
    auto canonicalCandidate = candidate.getLinkedTarget();
    auto canonicalBase = allowedBase.getLinkedTarget();

    // Check if candidate is child of or equal to base
    return canonicalCandidate.isAChildOf (canonicalBase) || canonicalCandidate == canonicalBase;
}

bool PathSanitizer::isValidIdentifier (const juce::String& str)
{
    if (str.isEmpty())
        return false;

    // Must match [a-zA-Z0-9_-]+
    for (int i = 0; i < str.length(); ++i)
    {
        auto ch = str[i];
        bool isValid = (ch >= 'a' && ch <= 'z') ||
                       (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') ||
                       ch == '_' ||
                       ch == '-';

        if (! isValid)
            return false;
    }

    return true;
}

} // namespace waive
