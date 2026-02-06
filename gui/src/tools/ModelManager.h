#pragma once

#include <JuceHeader.h>
#include <mutex>
#include <optional>
#include <vector>

namespace waive
{

struct ModelCatalogEntry
{
    juce::String modelID;
    juce::String displayName;
    juce::String description;
    juce::StringArray availableVersions;
    juce::String defaultVersion;
    int64 installSizeBytes = 0;
};

struct InstalledModelInfo
{
    juce::String modelID;
    juce::String displayName;
    juce::String version;
    juce::File installDirectory;
    int64 sizeBytes = 0;
    bool pinned = false;
};

class ModelManager
{
public:
    ModelManager();

    juce::File getStorageDirectory() const;
    void setStorageDirectory (const juce::File& newDirectory);

    juce::Result setQuotaBytes (int64 bytes);
    int64 getQuotaBytes() const;
    int64 getStorageUsageBytes() const;

    std::vector<ModelCatalogEntry> getAvailableModels() const;
    std::vector<InstalledModelInfo> getInstalledModels() const;

    juce::Result installModel (const juce::String& modelID,
                               const juce::String& version = {},
                               bool pinVersion = false);
    juce::Result uninstallModel (const juce::String& modelID,
                                 const juce::String& version = {});

    juce::Result pinModelVersion (const juce::String& modelID, const juce::String& version);
    juce::Result unpinModelVersion (const juce::String& modelID);
    juce::String getPinnedVersion (const juce::String& modelID) const;

    bool isInstalled (const juce::String& modelID, const juce::String& version = {}) const;
    std::optional<InstalledModelInfo> resolveInstalledModel (const juce::String& modelID,
                                                             const juce::String& requestedVersion = {}) const;

private:
    static juce::File getDefaultStorageDirectory();
    static std::vector<ModelCatalogEntry> makeDefaultCatalog();
    static int compareVersions (const juce::String& lhs, const juce::String& rhs);
    static int64 getDirectorySizeBytes (const juce::File& directory);

    juce::File getSettingsFile() const;
    void loadSettingsLocked();
    void saveSettingsLocked() const;

    const ModelCatalogEntry* findCatalogEntryLocked (const juce::String& modelID) const;
    std::vector<InstalledModelInfo> getInstalledModelsLocked() const;
    std::optional<InstalledModelInfo> resolveInstalledModelLocked (const juce::String& modelID,
                                                                   const juce::String& requestedVersion) const;
    juce::Result writePlaceholderModelFilesLocked (const InstalledModelInfo& info, int64 expectedSizeBytes) const;

    mutable std::mutex mutex;
    juce::File storageDirectory;
    int64 quotaBytes = 256 * 1024 * 1024;
    juce::NamedValueSet pinnedVersions;
    std::vector<ModelCatalogEntry> catalog;
};

} // namespace waive
