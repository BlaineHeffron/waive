#pragma once

#include <juce_core/juce_core.h>

namespace tracktion { inline namespace engine { class Edit; }}

namespace waive {

class ProjectPackager
{
public:
    struct CollectResult
    {
        int filesCopied = 0;
        juce::int64 bytesCopied = 0;
        juce::StringArray errors;
    };

    struct RemoveResult
    {
        int filesRemoved = 0;
        juce::int64 bytesFreed = 0;
        juce::StringArray errors;
    };

    // Collect all referenced audio into project_dir/Audio/
    static CollectResult collectAndSave (tracktion::engine::Edit& edit,
                                         const juce::File& projectDir,
                                         const juce::File& projectFile = {});

    // Find audio files referenced by the edit that are outside the project directory
    static juce::Array<juce::File> findExternalMedia (tracktion::engine::Edit& edit, const juce::File& projectDir);

    // Find audio files in project directory that are NOT referenced by any clip
    static juce::Array<juce::File> findUnusedMedia (tracktion::engine::Edit& edit, const juce::File& projectDir);

    // Remove unused media files
    static RemoveResult removeUnusedMedia (tracktion::engine::Edit& edit, const juce::File& projectDir);

    // Package the current project as a zip file.
    static bool packageAsZip (const juce::File& projectFile, const juce::File& outputZip);

private:
    static juce::Array<juce::File> getAllReferencedFiles (tracktion::engine::Edit& edit);
    static juce::File getUniqueTargetFile (const juce::File& targetDir, const juce::String& baseName);
    static bool isAudioFile (const juce::File& file);
};

} // namespace waive
