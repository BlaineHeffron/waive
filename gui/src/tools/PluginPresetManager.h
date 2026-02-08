#pragma once

#include <juce_core/juce_core.h>

namespace tracktion { inline namespace engine { class Plugin; }}

namespace waive {

class PluginPresetManager
{
public:
    PluginPresetManager();

    // Save current plugin state as a named preset
    bool savePreset (tracktion::engine::Plugin& plugin, const juce::String& presetName);

    // Load a preset and apply to plugin
    bool loadPreset (tracktion::engine::Plugin& plugin, const juce::String& presetName);

    // Get list of preset names for a plugin
    juce::StringArray getPresetsForPlugin (const juce::String& pluginIdentifier) const;

    // Delete a preset
    bool deletePreset (const juce::String& pluginIdentifier, const juce::String& presetName);

    // Get the plugin identifier string used for folder names
    static juce::String getPluginIdentifier (tracktion::engine::Plugin& plugin);

private:
    juce::File getPresetsDirectory() const;
    juce::File getPresetFile (const juce::String& pluginIdentifier, const juce::String& presetName) const;
};

} // namespace waive
