#include "PluginPresetManager.h"
#include "PathSanitizer.h"
#include <tracktion_engine/tracktion_engine.h>

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
    if (presetsDirectoryOverride != juce::File())
        return presetsDirectoryOverride;

    auto homeDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    return homeDir.getChildFile (".config/Waive/presets");
}

void PluginPresetManager::setPresetsDirectory (const juce::File& directory)
{
    presetsDirectoryOverride = directory;
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

    if (! plugin.state.isValid())
    {
        juce::Logger::writeToLog ("PluginPresetManager: plugin state is invalid");
        return false;
    }

    auto pluginId = getPluginIdentifier (plugin);
    auto presetFile = getPresetFile (pluginId, presetName);
    if (! presetFile.exists() && presetFile.getFullPathName().isEmpty())
        return false;

    auto parentDir = presetFile.getParentDirectory();
    if (! parentDir.exists())
    {
        auto result = parentDir.createDirectory();
        if (result.failed())
            return false;
    }

    auto pluginState = plugin.state.createCopy();

    juce::ValueTree presetTree ("WaivePreset");
    presetTree.setProperty ("name", presetName, nullptr);
    presetTree.setProperty ("plugin", pluginId, nullptr);
    presetTree.setProperty ("timestamp", juce::Time::getCurrentTime().toISO8601 (true), nullptr);
    juce::ValueTree pluginStateWrapper ("PluginState");
    pluginStateWrapper.appendChild (pluginState, nullptr);
    presetTree.appendChild (pluginStateWrapper, nullptr);

    auto xml = presetTree.createXml();
    if (! xml)
        return false;

    return xml->writeTo (presetFile, {});
}

bool PluginPresetManager::loadPreset (tracktion::engine::Plugin& plugin, const juce::String& presetName)
{
    if (presetName.trim().isEmpty())
        return false;

    auto pluginId = getPluginIdentifier (plugin);
    auto presetFile = getPresetFile (pluginId, presetName);
    if (! presetFile.exists() && presetFile.getFullPathName().isEmpty())
        return false;

    if (! presetFile.existsAsFile())
        return false;

    auto xml = juce::parseXML (presetFile);
    if (! xml)
        return false;

    auto presetTree = juce::ValueTree::fromXml (*xml);
    if (! presetTree.isValid() || presetTree.getType() != juce::Identifier ("WaivePreset"))
        return false;

    auto presetPluginId = presetTree.getProperty ("plugin").toString();
    if (presetPluginId != pluginId)
    {
        juce::Logger::writeToLog ("PluginPresetManager: preset plugin mismatch - expected "
                                   + pluginId + ", got " + presetPluginId);
        return false;
    }

    if (presetTree.getNumChildren() == 0)
        return false;

    auto pluginStateWrapper = presetTree.getChild (0);
    if (! pluginStateWrapper.isValid()
        || pluginStateWrapper.getType() != juce::Identifier ("PluginState")
        || pluginStateWrapper.getNumChildren() == 0)
        return false;

    plugin.restorePluginStateFromValueTree (pluginStateWrapper.getChild (0));
    return true;
}

juce::StringArray PluginPresetManager::getPresetsForPlugin (const juce::String& pluginIdentifier) const
{
    juce::StringArray presets;

    auto sanitizedPluginId = PathSanitizer::sanitizePathComponent (pluginIdentifier);
    if (sanitizedPluginId.isEmpty())
        return presets;

    auto pluginDir = getPresetsDirectory().getChildFile (sanitizedPluginId);
    if (! pluginDir.exists() || ! pluginDir.isDirectory())
        return presets;

    auto files = pluginDir.findChildFiles (juce::File::findFiles, false, "*.xml");
    for (auto& file : files)
        presets.add (file.getFileNameWithoutExtension());

    presets.sort (true);
    return presets;
}

bool PluginPresetManager::deletePreset (const juce::String& pluginIdentifier, const juce::String& presetName)
{
    if (presetName.trim().isEmpty())
        return false;

    auto presetFile = getPresetFile (pluginIdentifier, presetName);
    if (! presetFile.exists() && presetFile.getFullPathName().isEmpty())
        return false;

    if (! presetFile.existsAsFile())
        return false;

    return presetFile.deleteFile();
}

} // namespace waive
