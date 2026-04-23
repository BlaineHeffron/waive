#include "PluginPresetManager.h"
#include "PathSanitizer.h"
#include <tracktion_engine/tracktion_engine.h>

namespace waive {

PluginPresetManager::PluginPresetManager() = default;

namespace
{
bool isReservedWindowsFileName (const juce::String& value)
{
    static const juce::StringArray reservedNames {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };

    return reservedNames.contains (value.toUpperCase());
}

juce::String sanitisePresetFileComponent (juce::String value)
{
    value = value.trim();
    if (value.isEmpty())
        return {};

    value = value.replaceCharacters ("<>:\"/\\|?*", "_________");
    while (value.endsWithChar (' ') || value.endsWithChar ('.'))
        value = value.dropLastCharacters (1);

    if (value.isEmpty() || isReservedWindowsFileName (value))
        return {};

    return PathSanitizer::sanitizePathComponent (value);
}

juce::String sanitiseIdentifierPart (juce::String value)
{
    value = value.trim();

    if (value.isEmpty())
        return {};

    value = value.replaceCharacters ("/\\:*?\"<>|. ", "____________");
    return PathSanitizer::sanitizePathComponent (value);
}

juce::String buildStablePluginIdentifier (tracktion::engine::Plugin& plugin)
{
    juce::StringArray parts;

    auto format = plugin.state.getProperty ("pluginFormatName", {}).toString().trim();
    auto fileIdentifier = plugin.state.getProperty ("fileOrIdentifier", {}).toString().trim();
    auto type = plugin.state.getProperty (tracktion::engine::IDs::type, {}).toString().trim();
    auto name = plugin.getName().trim();
    auto manufacturer = plugin.state.getProperty ("manufacturer", {}).toString().trim();

    auto stableId = type;

    if (stableId.isEmpty())
    {
        if (fileIdentifier.isNotEmpty())
        {
            if (juce::File::isAbsolutePath (fileIdentifier))
                stableId = juce::File (fileIdentifier).getFileNameWithoutExtension();
            else
                stableId = fileIdentifier;
        }
        else if (name.isNotEmpty())
        {
            stableId = name;
        }
    }

    if (auto sanitizedFormat = sanitiseIdentifierPart (format.isNotEmpty() ? format : plugin.getPluginType());
        sanitizedFormat.isNotEmpty())
        parts.add (sanitizedFormat);

    if (auto sanitizedManufacturer = sanitiseIdentifierPart (manufacturer); sanitizedManufacturer.isNotEmpty())
        parts.add (sanitizedManufacturer);

    if (auto sanitizedStableId = sanitiseIdentifierPart (stableId); sanitizedStableId.isNotEmpty())
        parts.add (sanitizedStableId);
    else if (auto sanitizedName = sanitiseIdentifierPart (name); sanitizedName.isNotEmpty())
        parts.add (sanitizedName);

    if (parts.isEmpty())
        return "Unknown";

    return parts.joinIntoString ("_");
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
    auto sanitizedPluginId = sanitisePresetFileComponent (pluginIdentifier);
    auto sanitizedPresetName = sanitisePresetFileComponent (presetName);

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

    auto sanitizedPluginId = sanitisePresetFileComponent (pluginIdentifier);
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
