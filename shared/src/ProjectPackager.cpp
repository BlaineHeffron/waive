#include "ProjectPackager.h"
#include <tracktion_engine/tracktion_engine.h>
#include <filesystem>

namespace te = tracktion;

namespace waive {

juce::StringArray ProjectPackager::validateReferencedMedia (te::Edit& edit)
{
    juce::StringArray errors;

    for (auto* track : te::getAudioTracks (edit))
    {
        if (track == nullptr)
            continue;

        for (auto* clip : track->getClips())
        {
            auto* audioClip = dynamic_cast<te::AudioClipBase*> (clip);
            if (audioClip == nullptr)
                continue;

            auto& sourceRef = audioClip->getSourceFileReference();
            const auto sourceDescription = sourceRef.source.get().trim();

            if (sourceDescription.isEmpty())
            {
                errors.add ("Clip '" + audioClip->getName() + "' is missing a source file reference");
                continue;
            }

            const auto sourceFile = canonicalisePath (sourceRef.getFile());
            if (sourceFile == juce::File())
            {
                errors.add ("Clip '" + audioClip->getName() + "' could not resolve source: " + sourceDescription);
                continue;
            }

            if (! sourceFile.existsAsFile())
                errors.add ("Referenced media is missing: " + sourceFile.getFullPathName());
        }
    }

    return errors;
}

juce::File ProjectPackager::canonicalisePath (const juce::File& file)
{
    if (file == juce::File())
        return {};

    std::error_code ec;
    const auto canonicalPath = std::filesystem::weakly_canonical (std::filesystem::path (file.getFullPathName().toStdString()), ec);
    if (! ec)
        return juce::File (juce::String (canonicalPath.string()));

    return juce::File (file.getFullPathName());
}

bool ProjectPackager::isWithinProjectDirectory (const juce::File& file, const juce::File& projectDir)
{
    const auto canonicalFile = canonicalisePath (file);
    const auto canonicalProjectDir = canonicalisePath (projectDir);
    return canonicalFile == canonicalProjectDir || canonicalFile.isAChildOf (canonicalProjectDir);
}

bool ProjectPackager::fileArrayContainsCanonical (const juce::Array<juce::File>& files, const juce::File& candidate)
{
    const auto canonicalCandidate = canonicalisePath (candidate);

    for (const auto& file : files)
        if (canonicalisePath (file) == canonicalCandidate)
            return true;

    return false;
}

void ProjectPackager::rollbackCollectedMedia (const std::vector<std::pair<te::AudioClipBase*, juce::String>>& updatedReferences,
                                              const juce::Array<juce::File>& copiedFiles)
{
    for (const auto& [clip, originalSource] : updatedReferences)
        if (clip != nullptr)
            clip->getSourceFileReference().source = originalSource;

    for (const auto& copiedFile : copiedFiles)
        if (copiedFile.existsAsFile())
            (void) copiedFile.deleteFile();
}

juce::Array<juce::File> ProjectPackager::getAllReferencedFiles (te::Edit& edit)
{
    juce::Array<juce::File> files;

    for (auto* track : te::getAudioTracks (edit))
        for (auto* clip : track->getClips())
            if (auto audioClip = dynamic_cast<te::AudioClipBase*> (clip))
            {
                auto sourceFile = canonicalisePath (audioClip->getSourceFileReference().getFile());
                if (sourceFile != juce::File() && ! fileArrayContainsCanonical (files, sourceFile))
                    files.add (sourceFile);
            }

    return files;
}

juce::Array<juce::File> ProjectPackager::findExternalMedia (te::Edit& edit, const juce::File& projectDir)
{
    juce::Array<juce::File> externalFiles;
    auto referencedFiles = getAllReferencedFiles (edit);

    for (const auto& file : referencedFiles)
        if (! isWithinProjectDirectory (file, projectDir))
            externalFiles.add (file);

    return externalFiles;
}

void ProjectPackager::rewriteProjectMediaReferencesRelativeToProject (te::Edit& edit,
                                                                      const juce::File& projectDir,
                                                                      std::vector<std::pair<te::AudioClipBase*, juce::String>>& updatedReferences)
{
    for (auto* track : te::getAudioTracks (edit))
        for (auto* clip : track->getClips())
            if (auto audioClip = dynamic_cast<te::AudioClipBase*> (clip))
            {
                auto& sourceRef = audioClip->getSourceFileReference();

                if (sourceRef.isUsingProjectReference())
                    continue;

                const auto sourceFile = canonicalisePath (sourceRef.getFile());
                if (! sourceFile.existsAsFile() || ! isWithinProjectDirectory (sourceFile, projectDir))
                    continue;

                const auto previousSource = sourceRef.source.get();
                sourceRef.setToDirectFileReference (sourceFile, true);

                if (sourceRef.source.get() != previousSource)
                    updatedReferences.emplace_back (audioClip, previousSource);
            }
}

juce::File ProjectPackager::getUniqueTargetFile (const juce::File& targetDir, const juce::String& baseName)
{
    auto targetFile = targetDir.getChildFile (baseName);
    if (! targetFile.existsAsFile())
        return targetFile;

    auto nameWithoutExt = targetFile.getFileNameWithoutExtension();
    auto extension = targetFile.getFileExtension();

    for (int suffix = 2; suffix < 10000; ++suffix)
    {
        auto candidate = targetDir.getChildFile (nameWithoutExt + "_" + juce::String (suffix) + extension);
        if (! candidate.existsAsFile())
            return candidate;
    }

    auto timestamp = juce::Time::getCurrentTime().toMilliseconds();
    return targetDir.getChildFile (nameWithoutExt + "_" + juce::String (timestamp) + extension);
}

ProjectPackager::CollectResult ProjectPackager::collectAndSave (te::Edit& edit,
                                                                const juce::File& projectDir,
                                                                const juce::File& projectFile)
{
    CollectResult result;
    std::vector<std::pair<te::AudioClipBase*, juce::String>> updatedReferences;
    juce::Array<juce::File> copiedFiles;

    if (! projectDir.exists() || ! projectDir.isDirectory())
    {
        result.errors.add ("Project directory does not exist: " + projectDir.getFullPathName());
        return result;
    }

    auto mediaValidationErrors = validateReferencedMedia (edit);
    if (! mediaValidationErrors.isEmpty())
    {
        result.errors = mediaValidationErrors;
        result.errors.insert (0, "Collect and save aborted because one or more referenced media files are missing or invalid");
        return result;
    }

    auto testFile = projectDir.getChildFile (".waive_write_test");
    if (! testFile.create())
    {
        result.errors.add ("Project directory is not writable: " + projectDir.getFullPathName());
        return result;
    }
    testFile.deleteFile();

    auto audioDir = projectDir.getChildFile ("Audio");
    if (! audioDir.exists() && ! audioDir.createDirectory())
    {
        result.errors.add ("Failed to create Audio directory: " + audioDir.getFullPathName());
        return result;
    }

    auto saveTarget = projectFile != juce::File() ? projectFile : te::EditFileOperations (edit).getEditFile();
    const bool saveTargetExisted = saveTarget.existsAsFile();
    if (saveTarget == juce::File())
    {
        result.errors.add ("Failed to save edit: no target project file is available");
        return result;
    }

    auto saveParentDir = saveTarget.getParentDirectory();
    if (saveParentDir == juce::File() || ! saveParentDir.exists())
    {
        result.errors.add ("Failed to save edit: target directory does not exist");
        return result;
    }

    if (! saveTarget.existsAsFile())
    {
        if (! saveTarget.create())
        {
            result.errors.add ("Failed to prepare target project file: " + saveTarget.getFullPathName());
            return result;
        }
    }

    bool copyFailed = false;
    for (const auto& sourceFile : findExternalMedia (edit, projectDir))
    {
        auto targetFile = getUniqueTargetFile (audioDir, sourceFile.getFileName());
        if (! sourceFile.copyFileTo (targetFile))
        {
            result.errors.add ("Failed to copy: " + sourceFile.getFullPathName());
            copyFailed = true;
            continue;
        }

        if (! targetFile.existsAsFile() || targetFile.getSize() != sourceFile.getSize())
        {
            result.errors.add ("Copy verification failed for: " + sourceFile.getFullPathName());
            copyFailed = true;
            targetFile.deleteFile();
            continue;
        }

        ++result.filesCopied;
        result.bytesCopied += sourceFile.getSize();

        for (auto* track : te::getAudioTracks (edit))
            for (auto* clip : track->getClips())
                if (auto audioClip = dynamic_cast<te::AudioClipBase*> (clip))
                {
                    auto clipSourceFile = canonicalisePath (audioClip->getSourceFileReference().getFile());
                    if (clipSourceFile == sourceFile)
                    {
                        updatedReferences.emplace_back (audioClip, audioClip->getSourceFileReference().source.get());
                        audioClip->getSourceFileReference().setToDirectFileReference (targetFile, true);
                    }
                }

        copiedFiles.add (targetFile);
    }

    if (copyFailed)
    {
        rollbackCollectedMedia (updatedReferences, copiedFiles);
        if (! saveTargetExisted && saveTarget.existsAsFile())
            (void) saveTarget.deleteFile();
        result.filesCopied = 0;
        result.bytesCopied = 0;
        result.errors.insert (0, "Collect and save aborted because one or more media files could not be copied");
        return result;
    }

    rewriteProjectMediaReferencesRelativeToProject (edit, projectDir, updatedReferences);

    edit.flushState();
    if (! te::EditFileOperations (edit).saveAs (saveTarget, true))
    {
        rollbackCollectedMedia (updatedReferences, copiedFiles);
        if (! saveTargetExisted && saveTarget.existsAsFile())
            (void) saveTarget.deleteFile();
        result.filesCopied = 0;
        result.bytesCopied = 0;
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
    if (! audioDir.exists())
        return unusedFiles;

    auto referencedFiles = getAllReferencedFiles (edit);

    for (const auto& iter : juce::RangedDirectoryIterator (audioDir, true, "*", juce::File::findFiles))
    {
        auto file = iter.getFile();
        if (isAudioFile (file) && ! fileArrayContainsCanonical (referencedFiles, file))
            unusedFiles.add (file);
    }

    return unusedFiles;
}

ProjectPackager::RemoveResult ProjectPackager::removeUnusedMedia (te::Edit& edit, const juce::File& projectDir)
{
    RemoveResult result;

    if (! projectDir.exists() || ! projectDir.isDirectory())
    {
        result.errors.add ("Project directory does not exist: " + projectDir.getFullPathName());
        return result;
    }

    auto unusedFiles = findUnusedMedia (edit, projectDir);
    if (unusedFiles.isEmpty())
        return result;

    auto testFile = projectDir.getChildFile (".waive_write_test");
    if (! testFile.create())
    {
        result.errors.add ("Project directory is not writable: " + projectDir.getFullPathName());
        return result;
    }
    testFile.deleteFile();

    auto trashDir = projectDir.getChildFile (".trash");
    if (! trashDir.exists() && ! trashDir.createDirectory())
    {
        result.errors.add ("Failed to create trash directory: " + trashDir.getFullPathName());
        return result;
    }

    for (const auto& file : unusedFiles)
    {
        auto fileSize = file.getSize();
        auto targetFile = getUniqueTargetFile (trashDir, file.getFileName());
        if (file.moveFileTo (targetFile))
        {
            ++result.filesRemoved;
            result.bytesFreed += fileSize;
        }
        else
        {
            result.errors.add ("Failed to move unused media to trash: " + file.getFullPathName());
        }
    }

    return result;
}

bool ProjectPackager::packageAsZip (const juce::File& projectFile, const juce::File& outputZip)
{
    if (! projectFile.existsAsFile())
        return false;

    auto projectDir = projectFile.getParentDirectory();
    if (outputZip.existsAsFile())
        outputZip.deleteFile();

    auto outputDir = outputZip.getParentDirectory();
    if (! outputDir.exists() || ! outputDir.hasWriteAccess())
        return false;

    juce::FileOutputStream outputStream (outputZip);
    if (! outputStream.openedOk())
        return false;

    juce::ZipFile::Builder builder;
    builder.addFile (projectFile, 9, projectFile.getFileName());

    for (const auto& iter : juce::RangedDirectoryIterator (projectDir, false, ".waive-autosave-*.tracktionedit", juce::File::findFiles))
        builder.addFile (iter.getFile(), 9, iter.getFile().getFileName());

    auto audioDir = projectDir.getChildFile ("Audio");
    if (audioDir.exists())
        for (const auto& iter : juce::RangedDirectoryIterator (audioDir, true, "*", juce::File::findFiles))
        {
            const auto audioFile = iter.getFile();
            if (canonicalisePath (audioFile) == canonicalisePath (outputZip))
                continue;

            builder.addFile (audioFile, 9, audioFile.getRelativePathFrom (projectDir).replaceCharacter ('\\', '/'));
        }

    return builder.writeToStream (outputStream, nullptr);
}

} // namespace waive
