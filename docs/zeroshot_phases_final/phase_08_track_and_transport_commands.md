# Phase 08: Track Management & Transport Commands

## Objective
Add essential track management commands (rename, solo, mute, duplicate) and transport state queries so the AI agent has full control over the session. Also add a `get_transport_state` query that returns playback position, tempo, playing/recording status — critical for the AI to make informed decisions.

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
- Has `getTrackById(int)` and `getClipByIndex(int, int)` helpers.
- Has `makeOk()` and `makeError(msg)` response builders.
- Works directly with `te::Edit&`.

### Tracktion Engine APIs for this phase
- **Track name**: `track->getName()`, `track->setName(name)`
- **Solo**: track does not have a direct solo property. Solo is managed via `te::AudioTrack`'s `isSolo()` / solo state. Check `track->isMuted(false)` vs `track->isMuted(true)` (where `true` includes solo). Actually, Tracktion Engine uses `track->setMute(bool)` and `track->setSolo(bool)` — or via the `MuteAndSoloPlugin`. Look for the mute/solo pattern used in MixerChannelStrip.cpp.
- **Mute**: Similar — check MixerChannelStrip.cpp for the exact API pattern. Typically done via the track's mute state.
- **Duplicate track**: `edit.ensureNumberOfAudioTracks(n+1)` to create, then copy clips.
- **Transport state**: `edit.getTransport()` has `isPlaying()`, `isRecording()`, `getPosition()`. Tempo: `edit.tempoSequence.getTempoAt(pos)` or `edit.tempoSequence.getTempo(0)->getBpm()`.

### MixerChannelStrip (gui/src/ui/MixerChannelStrip.cpp) — reference for solo/mute
The existing mixer strip already controls solo/mute. Look at the `setupControls()` and `pollState()` methods to find the exact Tracktion Engine API calls for getting and setting solo/mute state. Use the same API pattern in CommandHandler.

### AiToolSchema (gui/src/ai/AiToolSchema.cpp)
- `generateCommandDefinitions()` returns all cmd_* tool definitions.
- `generateSystemPrompt()` returns the system prompt text.

## Implementation Tasks

### 1. Modify `engine/src/CommandHandler.h` — Add new handler declarations

Add these private handler methods:
```cpp
juce::var handleRenameTrack (const juce::var& params);
juce::var handleSoloTrack (const juce::var& params);
juce::var handleMuteTrack (const juce::var& params);
juce::var handleDuplicateTrack (const juce::var& params);
juce::var handleGetTransportState();
juce::var handleSetTempo (const juce::var& params);
juce::var handleSetLoopRegion (const juce::var& params);
```

### 2. Modify `engine/src/CommandHandler.cpp` — Add dispatch routing

In `handleCommand()`, add these routes:
```cpp
else if (action == "rename_track")        result = handleRenameTrack (parsed);
else if (action == "solo_track")          result = handleSoloTrack (parsed);
else if (action == "mute_track")          result = handleMuteTrack (parsed);
else if (action == "duplicate_track")     result = handleDuplicateTrack (parsed);
else if (action == "get_transport_state") result = handleGetTransportState();
else if (action == "set_tempo")           result = handleSetTempo (parsed);
else if (action == "set_loop_region")     result = handleSetLoopRegion (parsed);
```

### 3. Modify `engine/src/CommandHandler.cpp` — Implement rename_track

```cpp
juce::var CommandHandler::handleRenameTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("name"))
        return makeError ("Missing required parameters: track_id, name");

    int trackId = params["track_id"];
    auto newName = params["name"].toString();

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    track->setName (newName);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("name", newName);
    }
    return result;
}
```

### 4. Modify `engine/src/CommandHandler.cpp` — Implement solo_track

Look at MixerChannelStrip.cpp to find the exact solo API pattern. The typical Tracktion Engine approach is:
- `track->setSolo (bool)` or `track->setSoloed (bool)`
- Or via mute/solo plugin

Use the same approach as MixerChannelStrip. The implementation should be:
```cpp
juce::var CommandHandler::handleSoloTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("solo"))
        return makeError ("Missing required parameters: track_id, solo");

    int trackId = params["track_id"];
    bool solo = params["solo"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Use the same solo mechanism as MixerChannelStrip
    track->setSolo (solo);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("solo", solo);
    }
    return result;
}
```

Note: If `track->setSolo(bool)` is not available, check MixerChannelStrip for the alternative API (it might use `track->isSolo(false)` for querying and a different setter). Adapt accordingly.

### 5. Modify `engine/src/CommandHandler.cpp` — Implement mute_track

```cpp
juce::var CommandHandler::handleMuteTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("mute"))
        return makeError ("Missing required parameters: track_id, mute");

    int trackId = params["track_id"];
    bool mute = params["mute"];

    auto* track = getTrackById (trackId);
    if (track == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Use the same mute mechanism as MixerChannelStrip
    track->setMute (mute);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("mute", mute);
    }
    return result;
}
```

### 6. Modify `engine/src/CommandHandler.cpp` — Implement duplicate_track

Duplicates a track including all its clips and plugin chain:
```cpp
juce::var CommandHandler::handleDuplicateTrack (const juce::var& params)
{
    if (! params.hasProperty ("track_id"))
        return makeError ("Missing required parameter: track_id");

    int trackId = params["track_id"];

    auto* sourceTrack = getTrackById (trackId);
    if (sourceTrack == nullptr)
        return makeError ("Track not found: " + juce::String (trackId));

    // Create a new track
    auto trackCount = te::getAudioTracks (edit).size();
    edit.ensureNumberOfAudioTracks (trackCount + 1);
    auto* newTrack = te::getAudioTracks (edit).getLast();
    if (newTrack == nullptr)
        return makeError ("Failed to create new track");

    newTrack->setName (sourceTrack->getName() + " copy");

    // Copy volume and pan
    auto srcVolPlugins = sourceTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
    auto dstVolPlugins = newTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
    if (! srcVolPlugins.isEmpty() && ! dstVolPlugins.isEmpty())
    {
        dstVolPlugins.getFirst()->volParam->setParameter (
            srcVolPlugins.getFirst()->volParam->getCurrentValue(), juce::dontSendNotification);
        dstVolPlugins.getFirst()->panParam->setParameter (
            srcVolPlugins.getFirst()->panParam->getCurrentValue(), juce::dontSendNotification);
    }

    // Copy all clips from source to new track
    for (auto* clip : sourceTrack->getClips())
    {
        auto clipState = clip->state.createCopy();
        edit.createNewItemID().writeID (clipState, nullptr);
        te::assignNewIDsToAutomationCurveModifiers (edit, clipState);
        newTrack->insertClipWithState (clipState);
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("source_track_id", trackId);
        obj->setProperty ("new_track_index", (int) (trackCount));
        obj->setProperty ("new_track_name", newTrack->getName());
    }
    return result;
}
```

### 7. Modify `engine/src/CommandHandler.cpp` — Implement get_transport_state

```cpp
juce::var CommandHandler::handleGetTransportState()
{
    auto& transport = edit.getTransport();

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("position", transport.getPosition().inSeconds());
        obj->setProperty ("is_playing", transport.isPlaying());
        obj->setProperty ("is_recording", transport.isRecording());

        // Tempo
        auto& tempoSeq = edit.tempoSequence;
        if (tempoSeq.getNumTempos() > 0)
            obj->setProperty ("tempo_bpm", tempoSeq.getTempo (0)->getBpm());
        else
            obj->setProperty ("tempo_bpm", 120.0);

        // Time signature
        if (tempoSeq.getNumTimeSigs() > 0)
        {
            auto* ts = tempoSeq.getTimeSig (0);
            obj->setProperty ("time_sig_numerator", ts->numerator.get());
            obj->setProperty ("time_sig_denominator", ts->denominator.get());
        }

        // Loop region
        auto loopRange = transport.getLoopRange();
        obj->setProperty ("loop_enabled", transport.looping.get());
        obj->setProperty ("loop_start", loopRange.getStart().inSeconds());
        obj->setProperty ("loop_end", loopRange.getEnd().inSeconds());

        // Edit length
        obj->setProperty ("edit_length", edit.getLength().inSeconds());

        // Track count
        obj->setProperty ("track_count", (int) te::getAudioTracks (edit).size());
    }
    return result;
}
```

### 8. Modify `engine/src/CommandHandler.cpp` — Implement set_tempo

```cpp
juce::var CommandHandler::handleSetTempo (const juce::var& params)
{
    if (! params.hasProperty ("bpm"))
        return makeError ("Missing required parameter: bpm");

    double bpm = params["bpm"];
    if (bpm < 20.0 || bpm > 999.0)
        return makeError ("BPM must be between 20 and 999");

    auto& tempoSeq = edit.tempoSequence;
    if (tempoSeq.getNumTempos() > 0)
        tempoSeq.getTempo (0)->setBpm (bpm);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
        obj->setProperty ("bpm", bpm);
    return result;
}
```

### 9. Modify `engine/src/CommandHandler.cpp` — Implement set_loop_region

```cpp
juce::var CommandHandler::handleSetLoopRegion (const juce::var& params)
{
    if (! params.hasProperty ("enabled"))
        return makeError ("Missing required parameter: enabled");

    bool enabled = params["enabled"];
    auto& transport = edit.getTransport();

    transport.looping.setValue (enabled, nullptr);

    if (enabled && params.hasProperty ("start") && params.hasProperty ("end"))
    {
        double startSec = params["start"];
        double endSec = params["end"];
        if (startSec >= endSec)
            return makeError ("Loop start must be less than end");

        transport.setLoopRange ({ te::TimePosition::fromSeconds (startSec),
                                  te::TimePosition::fromSeconds (endSec) });
    }

    auto loopRange = transport.getLoopRange();
    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("enabled", enabled);
        obj->setProperty ("loop_start", loopRange.getStart().inSeconds());
        obj->setProperty ("loop_end", loopRange.getEnd().inSeconds());
    }
    return result;
}
```

### 10. Modify `engine/src/CommandHandler.cpp` — Enrich handleGetTracks with solo/mute

In the existing `handleGetTracks()`, add solo and mute state to each track object:
```cpp
trackObj->setProperty ("solo", track->isSolo (false));
trackObj->setProperty ("mute", track->isMuted (false));
```

Add these lines in the track loop, after the existing properties.

### 11. Modify `gui/src/ai/AiToolSchema.cpp` — Register all new commands

In `generateCommandDefinitions()`, add before `return defs;`:

```cpp
// rename_track
defs.push_back ({ "cmd_rename_track",
                  "Rename an audio track.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "name", prop ("string", "New track name") } },
                              { "track_id", "name" }) });

// solo_track
defs.push_back ({ "cmd_solo_track",
                  "Solo or unsolo a track.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "solo", prop ("boolean", "true to solo, false to unsolo") } },
                              { "track_id", "solo" }) });

// mute_track
defs.push_back ({ "cmd_mute_track",
                  "Mute or unmute a track.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "mute", prop ("boolean", "true to mute, false to unmute") } },
                              { "track_id", "mute" }) });

// duplicate_track
defs.push_back ({ "cmd_duplicate_track",
                  "Duplicate a track including all its clips.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index to duplicate") } },
                              { "track_id" }) });

// get_transport_state
defs.push_back ({ "cmd_get_transport_state",
                  "Get transport state: position, tempo, playing/recording status, loop region, track count.",
                  makeSchema ("object") });

// set_tempo
defs.push_back ({ "cmd_set_tempo",
                  "Set the project tempo in BPM.",
                  makeSchema ("object",
                              { { "bpm", prop ("number", "Tempo in beats per minute (20-999)") } },
                              { "bpm" }) });

// set_loop_region
defs.push_back ({ "cmd_set_loop_region",
                  "Enable/disable looping and optionally set loop start/end points.",
                  makeSchema ("object",
                              { { "enabled", prop ("boolean", "true to enable looping, false to disable") },
                                { "start", prop ("number", "Loop start in seconds (required when enabling)") },
                                { "end", prop ("number", "Loop end in seconds (required when enabling)") } },
                              { "enabled" }) });
```

### 12. Modify `gui/src/ai/AiToolSchema.cpp` — Update system prompt

In `generateSystemPrompt()`, add to the guidelines section:
```
"- Use cmd_get_transport_state to check playback position, tempo, and loop settings.\n"
"- Clips are addressed by track_id + clip_index (both 0-based).\n"
"- Use cmd_get_tracks to see solo/mute state per track.\n"
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
- `rename_track`, `solo_track`, `mute_track`, `duplicate_track` commands work through CommandHandler.
- `get_transport_state` returns position, tempo, time sig, loop region, playing/recording status.
- `set_tempo` and `set_loop_region` modify the session.
- `get_tracks` response now includes `solo` and `mute` per track.
- All new commands registered in AiToolSchema.
- System prompt updated with new guidance.
- Build compiles with no errors.
