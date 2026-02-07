#pragma once

#include <JuceHeader.h>

namespace waive
{

/**
 * Sanitizes user-controlled path components to prevent path traversal attacks.
 *
 * Functions:
 * - sanitizePathComponent(): Rejects dangerous characters like "..", "/", "\", null bytes
 * - isWithinDirectory(): Validates that a resolved path is within an allowed base directory
 */
class PathSanitizer
{
public:
    /**
     * Sanitizes a single path component (filename or directory name).
     * Returns empty string if the component contains dangerous characters:
     * - ".." (path traversal)
     * - "/" or "\" (path separators)
     * - null bytes
     * - control characters
     *
     * @param component The path component to sanitize
     * @return The sanitized component, or empty string if rejected
     */
    static juce::String sanitizePathComponent (const juce::String& component);

    /**
     * Checks if a file path is within an allowed base directory.
     * Resolves both paths to their canonical forms before checking containment.
     *
     * @param candidate The file path to check
     * @param allowedBase The base directory that should contain the candidate
     * @return true if candidate is within allowedBase (after canonical resolution)
     */
    static bool isWithinDirectory (const juce::File& candidate, const juce::File& allowedBase);

    /**
     * Validates that a string matches the pattern [a-zA-Z0-9_-]+
     * Used for validating model IDs and other identifiers.
     *
     * @param str The string to validate
     * @return true if the string contains only allowed characters
     */
    static bool isValidIdentifier (const juce::String& str);
};

} // namespace waive
