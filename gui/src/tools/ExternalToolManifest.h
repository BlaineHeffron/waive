#pragma once

#include <JuceHeader.h>
#include <vector>
#include <optional>

namespace waive
{

struct ExternalToolManifest
{
    juce::String name;
    juce::String displayName;
    juce::String version;
    juce::String description;
    juce::var inputSchema;
    juce::var defaultParams;
    juce::String executable;
    juce::StringArray arguments;
    juce::File baseDirectory;
    int timeoutMs = 300000;
    bool acceptsAudioInput = false;
    bool producesAudioOutput = false;
};

/** Parse a single .waive-tool.json manifest file. */
std::optional<ExternalToolManifest> parseManifest (const juce::File& manifestFile);

/** Scan a directory for all *.waive-tool.json files and parse them. */
std::vector<ExternalToolManifest> scanToolDirectory (const juce::File& directory);

} // namespace waive
