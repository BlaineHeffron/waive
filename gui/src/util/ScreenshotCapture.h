#pragma once

#include <JuceHeader.h>

class MainComponent;

namespace waive
{

/** Captures screenshots of all major UI areas and writes them as compressed JPEGs. */
class ScreenshotCapture
{
public:
    struct Options
    {
        juce::File outputDir;
        int maxWidth = 1400;       // Max pixel width (maintains aspect ratio)
        float jpegQuality = 0.70f; // 0.0–1.0
    };

    /** Capture all areas of the given MainComponent. Returns number of images written. */
    static int captureAll (MainComponent& main, const Options& opts);

private:
    static bool saveImage (const juce::Image& img, const juce::File& path, const Options& opts);
    static juce::Image resizeIfNeeded (const juce::Image& img, int maxWidth);
};

} // namespace waive
