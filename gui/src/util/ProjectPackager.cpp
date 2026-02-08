#include "ProjectPackager.h"
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace waive {

juce::Array<juce::File> ProjectPackager::getAllReferencedFiles (te::Edit& edit)
{
    juce::Array<juce::File> files;

    for (auto* track : te::getAudioTracks (edit))
    {
        for (auto* clip : track->getClips())
        {
            if (auto audioClip = dynamic_cast<te::AudioClipBase*> (clip))
            {
                auto sourceFile = audioClip->getSourceFileReference().getFile();
                if (sourceFile.existsAsFile() && !files.contains (sourceFile))
                    files.add (sourceFile);
            }
        }
    }

    return files;
}

juce::Array<juce::File> ProjectPackager::findExternalMedia (te::Edit& edit, const juce::File& projectDir)
{
    juce::Array<juce::File> externalFiles;
    auto referencedFiles = getAllReferencedFiles (edit);

    for (const auto& file : referencedFiles)
    {
        if (!file.isAChildOf (projectDir))
            externalFiles.add (file);
    }

    return externalFiles;
}

juce::File ProjectPackager::getUniqueTargetFile (const juce::File& targetDir, const juce::String& baseName)
{
    auto targetFile = targetDir.getChildFile (baseName);

    if (!targetFile.existsAsFile())
        return targetFile;

    auto nameWithoutExt = targetFile.getFileNameWithoutExtension();
    auto extension = targetFile.getFileExtension();

    int suffix = 2;
    constexpr int maxAttempts = 10000;
    while (suffix < maxAttempts)
    {
        auto uniqueName = nameWithoutExt + "_" + juce::String (suffix) + extension;
        targetFile = targetDir.getChildFile (uniqueName);

        if (!targetFile.existsAsFile())
            return targetFile;

        ++suffix;
    }

    // Fallback: return file with timestamp suffix if max attempts exceeded
    auto timestamp = juce::Time::getCurrentTime().toMilliseconds();
    return targetDir.getChildFile (nameWithoutExt + "_" + juce::String (timestamp) + extension);
}

ProjectPackager::CollectResult ProjectPackager::collectAndSave (te::Edit& edit, const juce::File& projectDir)
{
    CollectResult result;

    // Check if projectDir is writable
    auto testFile = projectDir.getChildFile (".waive_write_test");
    if (!testFile.create())
    {
        result.errors.add ("Project directory is not writable: " + projectDir.getFullPathName());
        return result;
    }
    testFile.deleteFile();

    auto audioDir = projectDir.getChildFile ("Audio");
    if (!audioDir.exists())
    {
        if (!audioDir.createDirectory())
        {
            result.errors.add ("Failed to create Audio directory: " + audioDir.getFullPathName());
            return result;
        }
    }

    auto externalFiles = findExternalMedia (edit, projectDir);

    for (const auto& sourceFile : externalFiles)
    {
        auto targetFile = getUniqueTargetFile (audioDir, sourceFile.getFileName());

        if (!sourceFile.copyFileTo (targetFile))
        {
            result.errors.add ("Failed to copy: " + sourceFile.getFullPathName());
            continue;
        }

        // Verify copy succeeded before updating references
        if (!targetFile.existsAsFile() || targetFile.getSize() != sourceFile.getSize())
        {
            result.errors.add ("Copy verification failed for: " + sourceFile.getFullPathName());
            targetFile.deleteFile();
            continue;
        }

        result.filesCopied++;
        result.bytesCopied += sourceFile.getSize();

        // Update all clips that reference this file
        for (auto* track : te::getAudioTracks (edit))
        {
            for (auto* clip : track->getClips())
            {
                if (auto audioClip = dynamic_cast<te::AudioClipBase*> (clip))
                {
                    auto clipSourceFile = audioClip->getSourceFileReference().getFile();
                    if (clipSourceFile == sourceFile)
                    {
                        // Update the source file reference to point to the new location
                        audioClip->getSourceFileReference().setToDirectFileReference (targetFile, true);
                    }
                }
            }
        }
    }

    // Save the edit with updated paths
    if (result.filesCopied > 0)
    {
        if (!te::EditFileOperations (edit).save (true, true, false))
            result.errors.add ("Failed to save edit with updated paths");
    }

    return result;
}

bool ProjectPackager::isAudioFile (const juce::File& file)
{
    auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".flac" || ext == ".ogg" ||
           ext == ".aif" || ext == ".aiff" || ext == ".mp3";
}

juce::Array<juce::File> ProjectPackager::findUnusedMedia (te::Edit& edit, const juce::File& projectDir)
{
    juce::Array<juce::File> unusedFiles;

    auto audioDir = projectDir.getChildFile ("Audio");
    if (!audioDir.exists())
        return unusedFiles;

    auto referencedFiles = getAllReferencedFiles (edit);

    juce::Array<juce::File> audioFiles;
    for (const auto& iter : juce::RangedDirectoryIterator (audioDir, false, "*", juce::File::findFiles))
    {
        auto file = iter.getFile();
        if (isAudioFile (file))
            audioFiles.add (file);
    }

    for (const auto& file : audioFiles)
    {
        if (!referencedFiles.contains (file))
            unusedFiles.add (file);
    }

    return unusedFiles;
}

int ProjectPackager::removeUnusedMedia (te::Edit& edit, const juce::File& projectDir)
{
    auto unusedFiles = findUnusedMedia (edit, projectDir);

    if (unusedFiles.isEmpty())
        return 0;

    // Check if projectDir is writable
    auto testFile = projectDir.getChildFile (".waive_write_test");
    if (!testFile.create())
        return 0; // Cannot write, return 0 removed
    testFile.deleteFile();

    auto trashDir = projectDir.getChildFile (".trash");
    if (!trashDir.exists())
    {
        if (!trashDir.createDirectory())
            return 0; // Cannot create trash dir, return 0 removed
    }

    int removed = 0;
    for (const auto& file : unusedFiles)
    {
        auto targetFile = getUniqueTargetFile (trashDir, file.getFileName());
        if (file.moveFileTo (targetFile))
            removed++;
    }

    return removed;
}

bool ProjectPackager::packageAsZip (const juce::File& projectDir, const juce::File& outputZip)
{
    if (outputZip.existsAsFile())
        outputZip.deleteFile();

    // Check if output directory is writable
    auto outputDir = outputZip.getParentDirectory();
    if (!outputDir.exists() || !outputDir.hasWriteAccess())
        return false;

    juce::FileOutputStream outputStream (outputZip);
    if (!outputStream.openedOk())
        return false;

    juce::ZipFile::Builder builder;

    // Add .tracktionedit file
    for (const auto& iter : juce::RangedDirectoryIterator (projectDir, false, "*.tracktionedit", juce::File::findFiles))
    {
        auto file = iter.getFile();
        builder.addFile (file, 9, file.getFileName());
    }

    // Add .autosave if exists
    for (const auto& iter : juce::RangedDirectoryIterator (projectDir, false, "*.autosave", juce::File::findFiles))
    {
        auto file = iter.getFile();
        builder.addFile (file, 9, file.getFileName());
    }

    // Add Audio directory
    auto audioDir = projectDir.getChildFile ("Audio");
    if (audioDir.exists())
    {
        for (const auto& iter : juce::RangedDirectoryIterator (audioDir, false, "*", juce::File::findFiles))
        {
            auto file = iter.getFile();
            builder.addFile (file, 9, "Audio/" + file.getFileName());
        }
    }

    return builder.writeToStream (outputStream, nullptr);
}

} // namespace waive
