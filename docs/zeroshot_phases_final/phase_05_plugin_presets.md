# Phase 05: Plugin Preset Management

## Objective
Add a preset save/load system for audio plugins. Users can save the current state of any plugin as a named preset, browse and load presets, and organize them. Presets persist to disk in a user presets folder.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Context

### Current Plugin System
- `gui/src/ui/PluginBrowserComponent.h/.cpp` — plugin scanning, browsing, insertion, parameter editing
- `engine/src/CommandHandler.cpp` — `load_plugin`, `remove_plugin`, `bypass_plugin`, `get_plugin_parameters`, `set_parameter` commands
- Plugin state is managed by Tracktion Engine via `te::Plugin::getPluginStateValueTree()`
- No preset save/load, no preset browser, no preset organization

### Tracktion Engine Plugin State API
```cpp
te::Plugin& plugin = ...;

// Get full plugin state as XML
juce::ValueTree state = plugin.state;

// For external plugins (VST3/AU), the binary state is stored in the ValueTree
// Restoring: assign the state back
plugin.restorePluginStateFromValueTree (savedState);

// Alternative: use JUCE's MemoryBlock for VST state
if (auto* externalPlugin = dynamic_cast<te::ExternalPlugin*> (&plugin))
{
    juce::MemoryBlock stateBlock;
    externalPlugin->getPluginStateFromTree (stateBlock);
    // Save stateBlock to file...

    // Restore:
    externalPlugin->setPluginStateFromTree (stateBlock);
}
```

### Preset Storage Location
User presets should be stored in:
```
~/.config/Waive/presets/<PluginIdentifier>/<PresetName>.xml
```
Where `<PluginIdentifier>` is a sanitized version of the plugin's unique ID (e.g., "VST3_FabFilter_Pro-Q3").

## Implementation Tasks

### 1. Create PluginPresetManager

Create `gui/src/tools/PluginPresetManager.h` and `gui/src/tools/PluginPresetManager.cpp`.

**Responsibilities:**
- Save a plugin's current state to an XML file with a name
- Load a preset from file and apply to a plugin
- List available presets for a given plugin identifier
- Delete presets
- Get/set the presets directory

**API:**
```cpp
class PluginPresetManager
{
public:
    PluginPresetManager();

    // Save current plugin state as a named preset
    bool savePreset (te::Plugin& plugin, const juce::String& presetName);

    // Load a preset and apply to plugin
    bool loadPreset (te::Plugin& plugin, const juce::String& presetName);

    // Get list of preset names for a plugin
    juce::StringArray getPresetsForPlugin (const juce::String& pluginIdentifier) const;

    // Delete a preset
    bool deletePreset (const juce::String& pluginIdentifier, const juce::String& presetName);

    // Get the plugin identifier string used for folder names
    static juce::String getPluginIdentifier (const te::Plugin& plugin);

private:
    juce::File getPresetsDirectory() const;
    juce::File getPresetFile (const juce::String& pluginIdentifier, const juce::String& presetName) const;
};
```

**Preset file format:** XML wrapping the plugin's ValueTree state:
```xml
<?xml version="1.0"?>
<WaivePreset name="My Clean Sound" plugin="VST3_FabFilter_Pro-Q3" timestamp="2026-02-07T12:00:00">
  <PluginState>
    <!-- ValueTree XML from plugin.state -->
  </PluginState>
</WaivePreset>
```

### 2. Create PluginPresetBrowser component

Create `gui/src/ui/PluginPresetBrowser.h` and `gui/src/ui/PluginPresetBrowser.cpp`.

**UI Layout:**
```
┌──────────────────────────────────┐
│ Preset: [ComboBox ▼]  [Save] [⨯] │
└──────────────────────────────────┘
```

- A compact component that sits at the top of the plugin editor area in PluginBrowserComponent
- ComboBox lists all presets for the current plugin
- Selecting a preset loads it immediately
- "Save" button opens a text input dialog to name and save current state
- Delete button (⨯) next to selected preset removes it (with confirmation)

### 3. Integrate with PluginBrowserComponent

In `gui/src/ui/PluginBrowserComponent.cpp`:
- When a plugin is selected and its parameter panel is shown, add the PluginPresetBrowser above the parameter controls
- Pass the current `te::Plugin*` to the preset browser
- When the user navigates to a different plugin, update the preset browser

### 4. Add save/load preset commands

In `engine/src/CommandHandler.cpp`, add two new commands:

**`save_plugin_preset`:**
- Params: `track_index`, `plugin_index`, `preset_name`
- Saves the plugin's current state using PluginPresetManager

**`load_plugin_preset`:**
- Params: `track_index`, `plugin_index`, `preset_name`
- Loads and applies the preset

This allows the AI agent to manage presets programmatically.

### 5. Add to CMakeLists.txt

Add new files to `gui/CMakeLists.txt`.

## Files Expected To Change
- `gui/src/tools/PluginPresetManager.h` (create)
- `gui/src/tools/PluginPresetManager.cpp` (create)
- `gui/src/ui/PluginPresetBrowser.h` (create)
- `gui/src/ui/PluginPresetBrowser.cpp` (create)
- `gui/src/ui/PluginBrowserComponent.h` (add preset browser member)
- `gui/src/ui/PluginBrowserComponent.cpp` (integrate preset browser)
- `engine/src/CommandHandler.h` (add preset commands)
- `engine/src/CommandHandler.cpp` (implement preset commands)
- `gui/CMakeLists.txt` (add new files)

## Validation

```bash
# Build must succeed
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))

# All tests must still pass
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- Presets can be saved with a custom name
- Presets persist to disk (~/.config/Waive/presets/)
- Presets can be loaded from the dropdown
- Presets can be deleted
- Preset commands available for AI agent
- All existing tests still pass
