#pragma once

#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

namespace tracktion { inline namespace engine { class Edit; class AudioClipBase; }}

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

    struct PackageResult
    {
        int filesCopied = 0;
        juce::int64 bytesCopied = 0;
        juce::StringArray errors;
    };

    static CollectResult collectAndSave (tracktion::engine::Edit& edit,
                                         const juce::File& projectDir,
                                         const juce::File& projectFile = {});
    static juce::Array<juce::File> findExternalMedia (tracktion::engine::Edit& edit, const juce::File& projectDir);
    static juce::Array<juce::File> findUnusedMedia (tracktion::engine::Edit& edit, const juce::File& projectDir);
    static RemoveResult removeUnusedMedia (tracktion::engine::Edit& edit, const juce::File& projectDir);
    static PackageResult packageEditAsZip (tracktion::engine::Edit& edit,
                                           const juce::File& projectFile,
                                           const juce::File& outputZip);
    static bool packageAsZip (const juce::File& projectFile, const juce::File& outputZip);
    static bool isWithinProjectDirectory (const juce::File& file, const juce::File& projectDir);

private:
    static juce::StringArray validateReferencedMedia (tracktion::engine::Edit& edit);
    static void rollbackCollectedMedia (const std::vector<std::pair<tracktion::engine::AudioClipBase*, juce::String>>& updatedReferences,
                                        const juce::Array<juce::File>& copiedFiles);
    static juce::File canonicalisePath (const juce::File& file);
    static bool fileArrayContainsCanonical (const juce::Array<juce::File>& files, const juce::File& candidate);
    static void rewriteProjectMediaReferencesRelativeToProject (tracktion::engine::Edit& edit,
                                                                const juce::File& projectDir,
                                                                std::vector<std::pair<tracktion::engine::AudioClipBase*, juce::String>>& updatedReferences);
    static juce::Array<juce::File> getAllReferencedFiles (tracktion::engine::Edit& edit);
    static juce::File getUniqueTargetFile (const juce::File& targetDir, const juce::String& baseName);
    static bool isAudioFile (const juce::File& file);
    static juce::File createTemporaryZipOutput (const juce::File& outputZip);
};

} // namespace waive
