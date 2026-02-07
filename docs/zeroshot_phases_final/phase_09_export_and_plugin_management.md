# Phase 09: Export, Plugin Management & Bounce

## Objective
Add audio export (mixdown + per-track stems), plugin management commands (remove, bypass, list parameters), and track bounce (render a track to a single audio file). These are essential DAW operations that the AI needs for completing production workflows end-to-end.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### CommandHandler (engine/src/CommandHandler.h/.cpp)
- Dispatches JSON commands via if/else chain in `handleCommand()`.
- Has `getTrackById(int)` helper.
- Has `makeOk()` and `makeError(msg)` response helpers.
- Has `allowedMediaDirectories` for file path validation (used by insert_audio_clip).
- Works directly with `te::Edit&`.

### Tracktion Engine APIs for this phase

**Rendering/Export:**
- `te::Renderer::renderToFile()` — renders an Edit to an audio file.
- `te::Renderer::Parameters` — configuration struct for render: edit, output file, range, format, etc.
- The render operation is synchronous and CPU-intensive — it must run on a background thread.
- Output format: WAV (default), can also do FLAC, OGG. Use `juce::WavAudioFormat` for WAV.
- For stems: solo each track, render, unsolo. Or use `te::Renderer` with track isolation.

**Plugin management:**
- `track->pluginList` — the plugin chain on a track.
- `track->pluginList[index]` — access plugin by index.
- `plugin->setEnabled(bool)` — bypass/unbypass a plugin.
- `plugin->isEnabled()` — check bypass state.
- `track->pluginList.removePlugin(plugin)` — remove a plugin.
- `plugin->getAutomatableParameters()` — list all automatable parameters.
- `param->getParameterName()`, `param->getCurrentValue()`, `param->paramID` — parameter info.

**Track bounce:**
- Render a single track to audio by soloing it and rendering the edit, or by using `te::Renderer` with track-specific options.

### Path validation pattern
From existing `handleInsertAudioClip`: validate output paths against `allowedMediaDirectories` to prevent writing to arbitrary locations. Apply the same pattern for export operations.

## Implementation Tasks

### 1. Modify `engine/src/CommandHandler.h` — Add new handler declarations

```cpp
juce::var handleExportMixdown (const juce::var& params);
juce::var handleExportStems (const juce::var& params);
juce::var handleBounceTrack (const juce::var& params);
juce::var handleRemovePlugin (const juce::var& params);
juce::var handleBypassPlugin (const juce::var& params);
juce::var handleGetPluginParameters (const juce::var& params);
```

### 2. Modify `engine/src/CommandHandler.cpp` — Add dispatch routing

```cpp
else if (action == "export_mixdown")        result = handleExportMixdown (parsed);
else if (action == "export_stems")          result = handleExportStems (parsed);
else if (action == "bounce_track")          result = handleBounceTrack (parsed);
else if (action == "remove_plugin")         result = handleRemovePlugin (parsed);
else if (action == "bypass_plugin")         result = handleBypassPlugin (parsed);
else if (action == "get_plugin_parameters") result = handleGetPluginParameters (parsed);
```

### 3. Modify `engine/src/CommandHandler.cpp` — Implement export_mixdown

This renders the entire edit to a WAV file. The render uses Tracktion Engine's `Renderer`.

```cpp
juce::var CommandHandler::handleExportMixdown (const juce::var& params)
{
    if (! params.hasProperty ("file_path"))
        return makeError ("Missing required parameter: file_path");

    auto filePath = params["file_path"].toString();
    juce::File outputFile (filePath);

    // Validate output path
    auto canonicalFile = outputFile.getLinkedTarget();
    bool isAllowed = false;
    for (const auto& allowedDir : allowedMediaDirectories)
    {
        if (canonicalFile.isAChildOf (allowedDir) || canonicalFile.getParentDirectory() == allowedDir)
        {
            isAllowed = true;
            break;
        }
    }
    if (! isAllowed)
        return makeError ("Output path is outside allowed directories: " + filePath);

    // Ensure parent directory exists
    outputFile.getParentDirectory().createDirectory();

    // Determine render range
    double startSec = params.hasProperty ("start") ? (double) params["start"] : 0.0;
    double endSec = params.hasProperty ("end") ? (double) params["end"] : edit.getLength().inSeconds();

    if (endSec <= startSec)
        return makeError ("End time must be greater than start time");

    // Sample rate and bit depth
    double sampleRate = params.hasProperty ("sample_rate") ? (double) params["sample_rate"] : 44100.0;
    int bitDepth = params.hasProperty ("bit_depth") ? (int) params["bit_depth"] : 24;

    // Use Tracktion's Renderer to export
    // The exact API may vary — adapt to the version of Tracktion Engine available.
    // Typical approach:
    juce::BigInteger tracksMask;
    auto audioTracks = te::getAudioTracks (edit);
    for (int i = 0; i < audioTracks.size(); ++i)
        tracksMask.setBit (audioTracks[i]->getIndexInEditTrackList());

    auto renderResult = te::Renderer::renderToFile (
        "Export Mixdown",
        outputFile,
        edit,
        { te::TimePosition::fromSeconds (startSec), te::TimePosition::fromSeconds (endSec) },
        tracksMask,
        true,     // usePlugins
        {},       // allowedClips (empty = all)
        true);    // useThread

    if (! renderResult)
        return makeError ("Export failed");

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("file_path", outputFile.getFullPathName());
        obj->setProperty ("duration", endSec - startSec);
    }
    return result;
}
```

Note: The exact `te::Renderer::renderToFile()` signature may differ. Check the Tracktion Engine headers for the correct overload. The key parameters are: output file, edit, time range, track mask, and whether to use plugins.

### 4. Modify `engine/src/CommandHandler.cpp` — Implement export_stems

Exports each track as a separate WAV file into a directory:

```cpp
juce::var CommandHandler::handleExportStems (const juce::var& params)
{
    if (! params.hasProperty ("output_dir"))
        return makeError ("Missing required parameter: output_dir");

    auto outputDirPath = params["output_dir"].toString();
    juce::File outputDir (outputDirPath);

    // Validate path
    bool isAllowed = false;
    for (const auto& allowedDir : allowedMediaDirectories)
    {
        if (outputDir.isAChildOf (allowedDir) || outputDir == allowedDir)
        {
            isAllowed = true;
            break;
        }
    }
    if (! isAllowed)
        return makeError ("Output directory is outside allowed directories");

    outputDir.createDirectory();

    double startSec = params.hasProperty ("start") ? (double) params["start"] : 0.0;
    double endSec = params.hasProperty ("end") ? (double) params["end"] : edit.getLength().inSeconds();

    auto audioTracks = te::getAudioTracks (edit);
    juce::Array<juce::var> exportedFiles;

    for (int i = 0; i < audioTracks.size(); ++i)
    {
        auto* track = audioTracks[i];
        if (track->getClips().isEmpty())
            continue;

        // Sanitize track name for filename
        auto safeName = track->getName().replaceCharacters (" /\\:*?\"<>|", "__________");
        auto stemFile = outputDir.getChildFile (juce::String::formatted ("%02d_", i) + safeName + ".wav");

        // Create mask for just this track
        juce::BigInteger trackMask;
        trackMask.setBit (track->getIndexInEditTrackList());

        auto ok = te::Renderer::renderToFile (
            "Export Stem: " + track->getName(),
            stemFile,
            edit,
            { te::TimePosition::fromSeconds (startSec), te::TimePosition::fromSeconds (endSec) },
            trackMask,
            true,   // usePlugins
            {},
            true);  // useThread

        if (ok)
        {
            auto* fileInfo = new juce::DynamicObject();
            fileInfo->setProperty ("track_id", i);
            fileInfo->setProperty ("track_name", track->getName());
            fileInfo->setProperty ("file_path", stemFile.getFullPathName());
            exportedFiles.add (juce::var (fileInfo));
        }
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("output_dir", outputDir.getFullPathName());
        obj->setProperty ("stems", exportedFiles);
        obj->setProperty ("count", exportedFiles.size());
    }
    return result;
}
```

### 5. Modify `engine/src/CommandHandler.cpp` — Implement bounce_track

Renders a single track to audio and replaces its contents with the bounced file:

```cpp
juce::var CommandHandler::handleBounceTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id"))
        return makeError ("Missing required parameter: track_id");

    int trackId = params["track_id"];
    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    if (track->getClips().isEmpty())
        return makeError ("Track has no clips to bounce");

    // Determine render range from track's clip extents
    double startSec = std::numeric_limits<double>::max();
    double endSec = 0.0;
    for (auto* clip : track->getClips())
    {
        auto pos = clip->getPosition();
        startSec = std::min (startSec, pos.getStart().inSeconds());
        endSec = std::max (endSec, pos.getEnd().inSeconds());
    }

    // Create bounce file in temp directory next to project or in app data
    auto bounceDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("waive_bounces");
    bounceDir.createDirectory();

    auto safeName = track->getName().replaceCharacters (" /\\:*?\"<>|", "__________");
    auto bounceFile = bounceDir.getChildFile (safeName + "_bounced.wav");

    juce::BigInteger trackMask;
    trackMask.setBit (track->getIndexInEditTrackList());

    auto ok = te::Renderer::renderToFile (
        "Bounce: " + track->getName(),
        bounceFile,
        edit,
        { te::TimePosition::fromSeconds (startSec), te::TimePosition::fromSeconds (endSec) },
        trackMask,
        true, {}, true);

    if (! ok || ! bounceFile.existsAsFile())
        return makeError ("Bounce render failed");

    // Remove all existing clips from the track
    for (auto* clip : track->getClips())
        clip->removeFromParent();

    // Insert the bounced audio as a single clip
    te::AudioFile audioFile (edit.engine, bounceFile);
    track->insertWaveClip (
        track->getName() + " (bounced)",
        bounceFile,
        { { te::TimePosition::fromSeconds (startSec),
            te::TimePosition::fromSeconds (endSec) },
          te::TimeDuration() },
        false);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("bounce_file", bounceFile.getFullPathName());
        obj->setProperty ("start", startSec);
        obj->setProperty ("end", endSec);
    }
    return result;
}
```

### 6. Modify `engine/src/CommandHandler.cpp` — Implement remove_plugin

```cpp
juce::var CommandHandler::handleRemovePlugin (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("plugin_index"))
        return makeError ("Missing required parameters: track_id, plugin_index");

    int trackId = params["track_id"];
    int pluginIdx = params["plugin_index"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Only count user plugins (skip built-in VolumeAndPan, LevelMeter)
    juce::Array<te::Plugin*> userPlugins;
    for (auto* p : track->pluginList)
    {
        if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
            userPlugins.add (p);
    }

    if (pluginIdx < 0 || pluginIdx >= userPlugins.size())
        return makeError ("Plugin index out of range. Track has " + juce::String (userPlugins.size()) + " user plugins.");

    auto* plugin = userPlugins[pluginIdx];
    auto pluginName = plugin->getName();
    track->pluginList.removePlugin (plugin);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("removed_plugin", pluginName);
    }
    return result;
}
```

### 7. Modify `engine/src/CommandHandler.cpp` — Implement bypass_plugin

```cpp
juce::var CommandHandler::handleBypassPlugin (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("plugin_index") || ! params.hasProperty ("bypassed"))
        return makeError ("Missing required parameters: track_id, plugin_index, bypassed");

    int trackId = params["track_id"];
    int pluginIdx = params["plugin_index"];
    bool bypassed = params["bypassed"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    juce::Array<te::Plugin*> userPlugins;
    for (auto* p : track->pluginList)
    {
        if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
            userPlugins.add (p);
    }

    if (pluginIdx < 0 || pluginIdx >= userPlugins.size())
        return makeError ("Plugin index out of range");

    auto* plugin = userPlugins[pluginIdx];
    plugin->setEnabled (! bypassed);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("plugin_name", plugin->getName());
        obj->setProperty ("bypassed", bypassed);
    }
    return result;
}
```

### 8. Modify `engine/src/CommandHandler.cpp` — Implement get_plugin_parameters

```cpp
juce::var CommandHandler::handleGetPluginParameters (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("plugin_index"))
        return makeError ("Missing required parameters: track_id, plugin_index");

    int trackId = params["track_id"];
    int pluginIdx = params["plugin_index"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    juce::Array<te::Plugin*> userPlugins;
    for (auto* p : track->pluginList)
    {
        if (dynamic_cast<te::ExternalPlugin*> (p) != nullptr)
            userPlugins.add (p);
    }

    if (pluginIdx < 0 || pluginIdx >= userPlugins.size())
        return makeError ("Plugin index out of range");

    auto* plugin = userPlugins[pluginIdx];

    juce::Array<juce::var> paramList;
    for (auto* param : plugin->getAutomatableParameters())
    {
        auto* pObj = new juce::DynamicObject();
        pObj->setProperty ("param_id", param->paramID);
        pObj->setProperty ("name", param->getParameterName());
        pObj->setProperty ("value", (double) param->getCurrentValue());
        pObj->setProperty ("default_value", (double) param->getDefaultValue());

        // Try to get value as text
        pObj->setProperty ("value_text", param->getCurrentValueAsString());

        paramList.add (juce::var (pObj));
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("plugin_name", plugin->getName());
        obj->setProperty ("plugin_index", pluginIdx);
        obj->setProperty ("enabled", plugin->isEnabled());
        obj->setProperty ("parameters", paramList);
        obj->setProperty ("parameter_count", paramList.size());
    }
    return result;
}
```

### 9. Modify `gui/src/ai/AiToolSchema.cpp` — Register all new commands

In `generateCommandDefinitions()`, add before `return defs;`:

```cpp
// export_mixdown
defs.push_back ({ "cmd_export_mixdown",
                  "Export the full mix as a WAV audio file.",
                  makeSchema ("object",
                              { { "file_path", prop ("string", "Output file path (must be in allowed directories)") },
                                { "start", prop ("number", "Start time in seconds (default: 0)") },
                                { "end", prop ("number", "End time in seconds (default: edit length)") },
                                { "sample_rate", prop ("number", "Sample rate (default: 44100)") },
                                { "bit_depth", prop ("integer", "Bit depth: 16 or 24 (default: 24)") } },
                              { "file_path" }) });

// export_stems
defs.push_back ({ "cmd_export_stems",
                  "Export each track as a separate WAV file into a directory.",
                  makeSchema ("object",
                              { { "output_dir", prop ("string", "Output directory path") },
                                { "start", prop ("number", "Start time in seconds (default: 0)") },
                                { "end", prop ("number", "End time in seconds (default: edit length)") } },
                              { "output_dir" }) });

// bounce_track
defs.push_back ({ "cmd_bounce_track",
                  "Render a track to a single audio file, replacing all clips with the bounced result.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index to bounce") } },
                              { "track_id" }) });

// remove_plugin
defs.push_back ({ "cmd_remove_plugin",
                  "Remove a plugin from a track by its index (0-based among user/external plugins only).",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "plugin_index", prop ("integer", "0-based index among user plugins on the track") } },
                              { "track_id", "plugin_index" }) });

// bypass_plugin
defs.push_back ({ "cmd_bypass_plugin",
                  "Bypass or unbypass a plugin on a track.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "plugin_index", prop ("integer", "0-based index among user plugins on the track") },
                                { "bypassed", prop ("boolean", "true to bypass, false to enable") } },
                              { "track_id", "plugin_index", "bypassed" }) });

// get_plugin_parameters
defs.push_back ({ "cmd_get_plugin_parameters",
                  "List all automatable parameters of a plugin on a track, with current values.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "plugin_index", prop ("integer", "0-based index among user plugins on the track") } },
                              { "track_id", "plugin_index" }) });
```

## Files Expected To Change
- `engine/src/CommandHandler.h`
- `engine/src/CommandHandler.cpp`
- `gui/src/ai/AiToolSchema.cpp`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- `export_mixdown` renders the full mix to a WAV file.
- `export_stems` exports each track as a separate WAV.
- `bounce_track` renders a track and replaces clips with the bounced file.
- `remove_plugin`, `bypass_plugin`, `get_plugin_parameters` work for user/external plugins.
- All new commands registered in AiToolSchema.
- Path validation applied to export operations.
- Build compiles with no errors.
