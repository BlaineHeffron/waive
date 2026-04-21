#pragma once

#include <juce_core/juce_core.h>

namespace tracktion { inline namespace engine { class Plugin; }}

namespace waive {

class PluginPresetManager
{
public:
    PluginPresetManager();

    bool savePreset (tracktion::engine::Plugin& plugin, const juce::String& presetName);
    bool loadPreset (tracktion::engine::Plugin& plugin, const juce::String& presetName);
    juce::StringArray getPresetsForPlugin (const juce::String& pluginIdentifier) const;
    bool deletePreset (const juce::String& pluginIdentifier, const juce::String& presetName);
    juce::File getPresetsDirectory() const;
    void setPresetsDirectory (const juce::File& directory);
    static juce::String getPluginIdentifier (tracktion::engine::Plugin& plugin);

private:
    juce::File getPresetFile (const juce::String& pluginIdentifier, const juce::String& presetName) const;

    juce::File presetsDirectoryOverride;
};

} // namespace waive
