#pragma once

#include <JuceHeader.h>
#include <vector>

#include "ExternalToolManifest.h"

namespace waive
{

class ProgressReporter;

struct ExternalToolOutput
{
    bool success = false;
    juce::String message;
    juce::var resultData;
    juce::File outputAudioFile;
};

class ExternalToolRunner
{
public:
    ExternalToolRunner();

    /** Run an external tool with given params. Blocks until done. */
    ExternalToolOutput run (const ExternalToolManifest& manifest,
                            const juce::var& params,
                            const juce::File& inputAudioFile,
                            ProgressReporter& reporter);

    /** Get the primary tools directory (app data). */
    juce::File getToolsDirectory() const;

    /** Add an additional directory to scan for tools. */
    void addToolsDirectory (const juce::File& dir);

    /** Get all configured tool directories. */
    const std::vector<juce::File>& getToolsDirectories() const { return toolsDirs; }

private:
    std::vector<juce::File> toolsDirs;
};

} // namespace waive
