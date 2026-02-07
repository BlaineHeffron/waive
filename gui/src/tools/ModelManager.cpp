#include "ModelManager.h"
#include "PathSanitizer.h"

#include <algorithm>

namespace
{
juce::String normaliseVersionString (const juce::String& version)
{
    return version.trim();
}

int compareVersionTokens (const juce::String& lhsToken, const juce::String& rhsToken)
{
    const auto lhsInt = lhsToken.getIntValue();
    const auto rhsInt = rhsToken.getIntValue();

    if (lhsToken.containsOnly ("0123456789") && rhsToken.containsOnly ("0123456789"))
    {
        if (lhsInt < rhsInt) return -1;
        if (lhsInt > rhsInt) return 1;
        return 0;
    }

    return lhsToken.compareNatural (rhsToken);
}
}

namespace waive
{

ModelManager::ModelManager()
    : storageDirectory (getDefaultStorageDirectory()),
      catalog (makeDefaultCatalog())
{
    std::lock_guard<std::mutex> lock (mutex);
    storageDirectory.createDirectory();
    loadSettingsLocked();
}

juce::File ModelManager::getStorageDirectory() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return storageDirectory;
}

void ModelManager::setStorageDirectory (const juce::File& newDirectory)
{
    std::lock_guard<std::mutex> lock (mutex);

    storageDirectory = newDirectory != juce::File() ? newDirectory : getDefaultStorageDirectory();
    storageDirectory.createDirectory();
    loadSettingsLocked();
}

juce::Result ModelManager::setQuotaBytes (int64 bytes)
{
    std::lock_guard<std::mutex> lock (mutex);

    const auto clamped = juce::jlimit<int64> (64 * 1024, 8LL * 1024 * 1024 * 1024, bytes);
    const auto currentUsage = getDirectorySizeBytes (storageDirectory);
    if (clamped < currentUsage)
    {
        return juce::Result::fail ("Quota is below current usage ("
                                   + juce::String (currentUsage) + " bytes)");
    }

    quotaBytes = clamped;
    saveSettingsLocked();
    return juce::Result::ok();
}

int64 ModelManager::getQuotaBytes() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return quotaBytes;
}

int64 ModelManager::getStorageUsageBytes() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return getDirectorySizeBytes (storageDirectory);
}

std::vector<ModelCatalogEntry> ModelManager::getAvailableModels() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return catalog;
}

std::vector<InstalledModelInfo> ModelManager::getInstalledModels() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return getInstalledModelsLocked();
}

juce::Result ModelManager::installModel (const juce::String& modelID,
                                         const juce::String& version,
                                         bool pinVersion)
{
    std::lock_guard<std::mutex> lock (mutex);

    // Validate modelID against path traversal and invalid characters
    if (! PathSanitizer::isValidIdentifier (modelID))
        return juce::Result::fail ("Invalid model ID: must match [a-zA-Z0-9_-]+");

    auto sanitizedModelID = PathSanitizer::sanitizePathComponent (modelID);
    if (sanitizedModelID.isEmpty())
        return juce::Result::fail ("Model ID contains invalid characters");

    auto* entry = findCatalogEntryLocked (modelID);
    if (entry == nullptr)
        return juce::Result::fail ("Unknown model: " + modelID);

    auto selectedVersion = normaliseVersionString (version);
    if (selectedVersion.isEmpty())
        selectedVersion = entry->defaultVersion;

    auto sanitizedVersion = PathSanitizer::sanitizePathComponent (selectedVersion);
    if (sanitizedVersion.isEmpty())
        return juce::Result::fail ("Version string contains invalid characters");

    if (! entry->availableVersions.contains (selectedVersion))
    {
        return juce::Result::fail ("Model '" + modelID + "' does not provide version " + selectedVersion);
    }

    auto installDirectory = storageDirectory.getChildFile (sanitizedModelID)
                                           .getChildFile (sanitizedVersion);

    if (! installDirectory.exists())
    {
        const auto usageBytes = getDirectorySizeBytes (storageDirectory);
        if (usageBytes + entry->installSizeBytes > quotaBytes)
        {
            return juce::Result::fail ("Installing model would exceed quota");
        }

        installDirectory.createDirectory();

        InstalledModelInfo info;
        info.modelID = modelID;
        info.displayName = entry->displayName;
        info.version = selectedVersion;
        info.installDirectory = installDirectory;
        info.sizeBytes = entry->installSizeBytes;

        auto writeResult = writePlaceholderModelFilesLocked (info, entry->installSizeBytes);
        if (writeResult.failed())
            return writeResult;
    }

    if (pinVersion)
    {
        pinnedVersions.set (modelID, selectedVersion);
        saveSettingsLocked();
    }

    return juce::Result::ok();
}

juce::Result ModelManager::uninstallModel (const juce::String& modelID,
                                           const juce::String& version)
{
    std::lock_guard<std::mutex> lock (mutex);

    // Sanitize inputs to prevent path traversal
    auto sanitizedModelID = PathSanitizer::sanitizePathComponent (modelID);
    if (sanitizedModelID.isEmpty())
        return juce::Result::fail ("Model ID contains invalid characters");

    const auto baseDirectory = storageDirectory.getChildFile (sanitizedModelID);
    if (! baseDirectory.exists())
        return juce::Result::ok();

    // Validate baseDirectory is within storageDirectory before deletion
    if (! PathSanitizer::isWithinDirectory (baseDirectory, storageDirectory))
        return juce::Result::fail ("Directory is outside allowed storage location");

    const auto targetVersion = normaliseVersionString (version);
    if (targetVersion.isNotEmpty())
    {
        auto sanitizedVersion = PathSanitizer::sanitizePathComponent (targetVersion);
        if (sanitizedVersion.isEmpty())
            return juce::Result::fail ("Version string contains invalid characters");

        auto versionDirectory = baseDirectory.getChildFile (sanitizedVersion);

        // Validate versionDirectory is within storageDirectory before deletion
        if (versionDirectory.exists())
        {
            if (! PathSanitizer::isWithinDirectory (versionDirectory, storageDirectory))
                return juce::Result::fail ("Directory is outside allowed storage location");

            (void) versionDirectory.deleteRecursively();
        }

        const auto pinned = pinnedVersions[modelID].toString();
        if (pinned == targetVersion)
            pinnedVersions.remove (modelID);
    }
    else
    {
        (void) baseDirectory.deleteRecursively();
        pinnedVersions.remove (modelID);
    }

    saveSettingsLocked();
    return juce::Result::ok();
}

juce::Result ModelManager::pinModelVersion (const juce::String& modelID, const juce::String& version)
{
    std::lock_guard<std::mutex> lock (mutex);

    if (! resolveInstalledModelLocked (modelID, version).has_value())
        return juce::Result::fail ("Model version is not installed");

    pinnedVersions.set (modelID, version);
    saveSettingsLocked();
    return juce::Result::ok();
}

juce::Result ModelManager::unpinModelVersion (const juce::String& modelID)
{
    std::lock_guard<std::mutex> lock (mutex);
    pinnedVersions.remove (modelID);
    saveSettingsLocked();
    return juce::Result::ok();
}

juce::String ModelManager::getPinnedVersion (const juce::String& modelID) const
{
    std::lock_guard<std::mutex> lock (mutex);
    return pinnedVersions[modelID].toString();
}

bool ModelManager::isInstalled (const juce::String& modelID, const juce::String& version) const
{
    std::lock_guard<std::mutex> lock (mutex);
    return resolveInstalledModelLocked (modelID, version).has_value();
}

std::optional<InstalledModelInfo> ModelManager::resolveInstalledModel (const juce::String& modelID,
                                                                       const juce::String& requestedVersion) const
{
    std::lock_guard<std::mutex> lock (mutex);
    return resolveInstalledModelLocked (modelID, requestedVersion);
}

juce::File ModelManager::getDefaultStorageDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                      .getChildFile ("Waive")
                      .getChildFile ("models");
}

std::vector<ModelCatalogEntry> ModelManager::makeDefaultCatalog()
{
    std::vector<ModelCatalogEntry> entries;

    ModelCatalogEntry stem;
    stem.modelID = "stem_separator";
    stem.displayName = "Stem Separator";
    stem.description = "Offline deterministic source separation model.";
    stem.availableVersions = { "1.0.0", "1.1.0" };
    stem.defaultVersion = "1.1.0";
    stem.installSizeBytes = 256 * 1024;
    entries.push_back (stem);

    ModelCatalogEntry autoMix;
    autoMix.modelID = "auto_mix_suggester";
    autoMix.displayName = "Auto-Mix Suggester";
    autoMix.description = "Track balancing and stereo placement suggestion model.";
    autoMix.availableVersions = { "1.0.0" };
    autoMix.defaultVersion = "1.0.0";
    autoMix.installSizeBytes = 128 * 1024;
    entries.push_back (autoMix);

    return entries;
}

int ModelManager::compareVersions (const juce::String& lhs, const juce::String& rhs)
{
    if (lhs == rhs)
        return 0;

    juce::StringArray lhsTokens;
    lhsTokens.addTokens (lhs, ".", "");
    juce::StringArray rhsTokens;
    rhsTokens.addTokens (rhs, ".", "");

    const auto numParts = juce::jmax (lhsTokens.size(), rhsTokens.size());
    for (int i = 0; i < numParts; ++i)
    {
        const auto lhsPart = i < lhsTokens.size() ? lhsTokens[i] : "0";
        const auto rhsPart = i < rhsTokens.size() ? rhsTokens[i] : "0";

        const auto cmp = compareVersionTokens (lhsPart, rhsPart);
        if (cmp != 0)
            return cmp;
    }

    return 0;
}

int64 ModelManager::getDirectorySizeBytes (const juce::File& directory)
{
    if (! directory.isDirectory())
        return directory.existsAsFile() ? directory.getSize() : 0;

    int64 totalSize = 0;
    juce::Array<juce::File> children;
    directory.findChildFiles (children, juce::File::findFilesAndDirectories, false);

    for (const auto& child : children)
        totalSize += getDirectorySizeBytes (child);

    return totalSize;
}

juce::File ModelManager::getSettingsFile() const
{
    return storageDirectory.getChildFile ("model_manager_settings.json");
}

void ModelManager::loadSettingsLocked()
{
    pinnedVersions.clear();

    const auto settingsFile = getSettingsFile();
    if (! settingsFile.existsAsFile())
        return;

    const auto parsed = juce::JSON::parse (settingsFile);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return;

    if (root->hasProperty ("quota_bytes"))
        quotaBytes = std::max<int64> (64 * 1024, (int64) root->getProperty ("quota_bytes"));

    if (root->hasProperty ("pinned_versions"))
    {
        if (auto* pinnedObj = root->getProperty ("pinned_versions").getDynamicObject())
        {
            for (const auto& prop : pinnedObj->getProperties())
                pinnedVersions.set (prop.name.toString(), prop.value.toString());
        }
    }
}

void ModelManager::saveSettingsLocked() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("quota_bytes", quotaBytes);

    auto* pinnedObj = new juce::DynamicObject();
    for (const auto& pinned : pinnedVersions)
        pinnedObj->setProperty (pinned.name.toString(), pinned.value.toString());

    root->setProperty ("pinned_versions", juce::var (pinnedObj));
    (void) getSettingsFile().replaceWithText (juce::JSON::toString (juce::var (root), true));
}

const ModelCatalogEntry* ModelManager::findCatalogEntryLocked (const juce::String& modelID) const
{
    auto found = std::find_if (catalog.begin(), catalog.end(),
                               [&] (const auto& entry) { return entry.modelID == modelID; });
    return found != catalog.end() ? &(*found) : nullptr;
}

std::vector<InstalledModelInfo> ModelManager::getInstalledModelsLocked() const
{
    std::vector<InstalledModelInfo> installed;

    juce::Array<juce::File> modelDirectories;
    storageDirectory.findChildFiles (modelDirectories, juce::File::findDirectories, false);

    for (const auto& modelDir : modelDirectories)
    {
        const auto modelID = modelDir.getFileName();
        juce::Array<juce::File> versionDirectories;
        modelDir.findChildFiles (versionDirectories, juce::File::findDirectories, false);

        for (const auto& versionDir : versionDirectories)
        {
            const auto version = versionDir.getFileName();
            const auto manifestFile = versionDir.getChildFile ("manifest.json");
            if (! manifestFile.existsAsFile())
                continue;

            InstalledModelInfo info;
            info.modelID = modelID;
            info.version = version;
            info.installDirectory = versionDir;
            info.sizeBytes = getDirectorySizeBytes (versionDir);
            info.pinned = (pinnedVersions[modelID].toString() == version);

            if (auto* catalogEntry = findCatalogEntryLocked (modelID))
                info.displayName = catalogEntry->displayName;
            else
                info.displayName = modelID;

            installed.push_back (std::move (info));
        }
    }

    return installed;
}

std::optional<InstalledModelInfo> ModelManager::resolveInstalledModelLocked (const juce::String& modelID,
                                                                              const juce::String& requestedVersion) const
{
    auto installed = getInstalledModelsLocked();
    std::vector<InstalledModelInfo> matches;
    for (auto& model : installed)
    {
        if (model.modelID == modelID)
            matches.push_back (std::move (model));
    }

    if (matches.empty())
        return std::nullopt;

    auto versionToResolve = normaliseVersionString (requestedVersion);
    if (versionToResolve.isEmpty())
    {
        auto pinned = pinnedVersions[modelID].toString().trim();
        if (pinned.isNotEmpty())
            versionToResolve = pinned;
    }

    if (versionToResolve.isNotEmpty())
    {
        auto found = std::find_if (matches.begin(), matches.end(),
                                   [&] (const auto& model) { return model.version == versionToResolve; });
        if (found != matches.end())
            return *found;
        return std::nullopt;
    }

    auto best = std::max_element (matches.begin(), matches.end(),
                                  [] (const auto& lhs, const auto& rhs)
                                  {
                                      return ModelManager::compareVersions (lhs.version, rhs.version) < 0;
                                  });
    if (best == matches.end())
        return std::nullopt;

    return *best;
}

juce::Result ModelManager::writePlaceholderModelFilesLocked (const InstalledModelInfo& info,
                                                             int64 expectedSizeBytes) const
{
    auto modelFile = info.installDirectory.getChildFile ("model.bin");
    auto manifestFile = info.installDirectory.getChildFile ("manifest.json");

    auto stream = std::unique_ptr<juce::FileOutputStream> (modelFile.createOutputStream());
    if (stream == nullptr)
        return juce::Result::fail ("Failed to create model file");

    constexpr int chunkSize = 4096;
    juce::HeapBlock<char> chunk ((size_t) chunkSize, true);
    const auto seed = std::abs ((info.modelID + ":" + info.version).hashCode());
    for (int i = 0; i < chunkSize; ++i)
        chunk[i] = (char) ((seed + i * 31) & 0xff);

    int64 written = 0;
    while (written < expectedSizeBytes)
    {
        const auto bytesThisChunk = (int) std::min<int64> ((int64) chunkSize, expectedSizeBytes - written);
        if (! stream->write (chunk.getData(), (size_t) bytesThisChunk))
            return juce::Result::fail ("Failed to write model payload");

        written += bytesThisChunk;
    }

    auto* manifest = new juce::DynamicObject();
    manifest->setProperty ("model_id", info.modelID);
    manifest->setProperty ("display_name", info.displayName);
    manifest->setProperty ("version", info.version);
    manifest->setProperty ("size_bytes", expectedSizeBytes);
    manifest->setProperty ("installed_at_utc", juce::Time::getCurrentTime().toISO8601 (true));

    if (! manifestFile.replaceWithText (juce::JSON::toString (juce::var (manifest), true)))
        return juce::Result::fail ("Failed to write model manifest");

    return juce::Result::ok();
}

} // namespace waive
