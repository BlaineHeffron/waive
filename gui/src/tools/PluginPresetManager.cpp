#include "PluginPresetManager.h"
#include "PathSanitizer.h"
#include <tracktion_engine/tracktion_engine.h>

// Security Model for Preset Storage:
// - Preset directory (~/.config/Waive/presets/) is implicitly trusted as user-owned storage
// - All preset file paths are constructed using PathSanitizer::sanitizePathComponent() which:
//   * Rejects ".." (path traversal)
//   * Rejects "/" and "\" (path separators)
//   * Rejects null bytes and control characters
// - This prevents directory traversal attacks within the preset directory
// - Files outside ~/.config/Waive/presets/ cannot be accessed via preset operations
// - No validation against CommandHandler::allowedMediaDirectories is performed because:
//   * Presets are not user-specified file paths (unlike load_audio_file)
//   * Presets are system-generated paths within a fixed base directory
//   * Users cannot specify arbitrary file paths for preset save/load

namespace waive {

PluginPresetManager::PluginPresetManager() = default;

namespace
{
juce::String buildStablePluginIdentifier (tracktion::engine::Plugin& plugin)
{
    juce::StringArray parts;

    auto format = plugin.state.getProperty ("pluginFormatName", {}).toString().trim();
    auto fileIdentifier = plugin.state.getProperty ("fileOrIdentifier", {}).toString().trim();
    auto type = plugin.state.getProperty (tracktion::engine::IDs::type, {}).toString().trim();
    auto name = plugin.getName().trim();
    auto manufacturer = plugin.state.getProperty ("manufacturer", {}).toString().trim();

    if (format.isNotEmpty())
        parts.add (format);
    else
        parts.add (plugin.getPluginType());

    if (fileIdentifier.isNotEmpty())
        parts.add (fileIdentifier);
    else if (type.isNotEmpty())
        parts.add (type);
    else if (name.isNotEmpty())
        parts.add (name);

    if (manufacturer.isNotEmpty())
        parts.add (manufacturer);

    auto identifier = parts.joinIntoString ("_");
    auto sanitized = PathSanitizer::sanitizePathComponent (identifier);

    if (sanitized.isNotEmpty())
        return sanitized;

    identifier = identifier.replaceCharacters ("/\\:*?\"<>|. ", "____________");
    sanitized = PathSanitizer::sanitizePathComponent (identifier);
    return sanitized.isNotEmpty() ? sanitized : "Unknown";
}
}

juce::File PluginPresetManager::getPresetsDirectory() const
{
    auto homeDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    return homeDir.getChildFile (".config/Waive/presets");
}

juce::String PluginPresetManager::getPluginIdentifier (tracktion::engine::Plugin& plugin)
{
    return buildStablePluginIdentifier (plugin);
}

juce::File PluginPresetManager::getPresetFile (const juce::String& pluginIdentifier,
                                                const juce::String& presetName) const
{
    auto sanitizedPluginId = PathSanitizer::sanitizePathComponent (pluginIdentifier);
    auto sanitizedPresetName = PathSanitizer::sanitizePathComponent (presetName);

    // If sanitization fails, return invalid file
    if (sanitizedPluginId.isEmpty() || sanitizedPresetName.isEmpty())
    {
        juce::Logger::writeToLog ("PluginPresetManager: rejected dangerous path component");
        return juce::File();
    }

    auto presetsDir = getPresetsDirectory();
    auto pluginDir = presetsDir.getChildFile (sanitizedPluginId);
    return pluginDir.getChildFile (sanitizedPresetName + ".xml");
}

bool PluginPresetManager::savePreset (tracktion::engine::Plugin& plugin, const juce::String& presetName)
{
    if (presetName.trim().isEmpty())
        return false;

    // Validate plugin state before copying
    if (!plugin.state.isValid())
    {
        juce::Logger::writeToLog ("PluginPresetManager: plugin state is invalid");
        return false;
    }

    auto pluginId = getPluginIdentifier (plugin);
    auto presetFile = getPresetFile (pluginId, presetName);

    // getPresetFile returns invalid file if sanitization failed
    if (!presetFile.exists() && presetFile.getFullPathName().isEmpty())
        return false;

    // Ensure directory exists
    auto parentDir = presetFile.getParentDirectory();
    if (!parentDir.exists())
    {
        auto result = parentDir.createDirectory();
        if (result.failed())
            return false;
    }

    // Get plugin state as ValueTree
    auto pluginState = plugin.state.createCopy();

    // Wrap in preset metadata
    juce::ValueTree presetTree ("WaivePreset");
    presetTree.setProperty ("name", presetName, nullptr);
    presetTree.setProperty ("plugin", pluginId, nullptr);
    presetTree.setProperty ("timestamp", juce::Time::getCurrentTime().toISO8601 (true), nullptr);
    juce::ValueTree pluginStateWrapper ("PluginState");
    pluginStateWrapper.appendChild (pluginState, nullptr);
    presetTree.appendChild (pluginStateWrapper, nullptr);

    // Convert to XML
    auto xml = presetTree.createXml();
    if (!xml)
        return false;

    // Write to file
    return xml->writeTo (presetFile, {});
}

bool PluginPresetManager::loadPreset (tracktion::engine::Plugin& plugin, const juce::String& presetName)
{
    if (presetName.trim().isEmpty())
        return false;

    auto pluginId = getPluginIdentifier (plugin);
    auto presetFile = getPresetFile (pluginId, presetName);

    // getPresetFile returns invalid file if sanitization failed
    if (!presetFile.exists() && presetFile.getFullPathName().isEmpty())
        return false;

    if (!presetFile.existsAsFile())
        return false;

    // Parse XML
    auto xml = juce::parseXML (presetFile);
    if (!xml)
        return false;

    // Convert to ValueTree
    auto presetTree = juce::ValueTree::fromXml (*xml);
    if (!presetTree.isValid() || presetTree.getType() != juce::Identifier ("WaivePreset"))
        return false;

    // Validate preset is for this plugin type
    auto presetPluginId = presetTree.getProperty ("plugin").toString();
    if (presetPluginId != pluginId)
    {
        juce::Logger::writeToLog ("PluginPresetManager: preset plugin mismatch - expected "
                                   + pluginId + ", got " + presetPluginId);
        return false;
    }

    // Extract plugin state from the documented PluginState wrapper.
    if (presetTree.getNumChildren() == 0)
        return false;

    auto pluginStateWrapper = presetTree.getChild (0);
    if (! pluginStateWrapper.isValid()
        || pluginStateWrapper.getType() != juce::Identifier ("PluginState")
        || pluginStateWrapper.getNumChildren() == 0)
        return false;

    auto pluginState = pluginStateWrapper.getChild (0);

    // Restore plugin state
    plugin.restorePluginStateFromValueTree (pluginState);

    return true;
}

juce::StringArray PluginPresetManager::getPresetsForPlugin (const juce::String& pluginIdentifier) const
{
    juce::StringArray presets;

    auto sanitizedPluginId = PathSanitizer::sanitizePathComponent (pluginIdentifier);
    if (sanitizedPluginId.isEmpty())
        return presets;

    auto presetsDir = getPresetsDirectory();
    auto pluginDir = presetsDir.getChildFile (sanitizedPluginId);

    if (!pluginDir.exists() || !pluginDir.isDirectory())
        return presets;

    auto files = pluginDir.findChildFiles (juce::File::findFiles, false, "*.xml");

    for (auto& file : files)
    {
        // Remove .xml extension
        auto presetName = file.getFileNameWithoutExtension();
        presets.add (presetName);
    }

    presets.sort (true);
    return presets;
}

bool PluginPresetManager::deletePreset (const juce::String& pluginIdentifier, const juce::String& presetName)
{
    if (presetName.trim().isEmpty())
        return false;

    auto presetFile = getPresetFile (pluginIdentifier, presetName);

    // getPresetFile returns invalid file if sanitization failed
    if (!presetFile.exists() && presetFile.getFullPathName().isEmpty())
        return false;

    if (!presetFile.existsAsFile())
        return false;

    return presetFile.deleteFile();
}

} // namespace waive
