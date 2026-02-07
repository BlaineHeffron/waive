# Phase 07: Clip Editing Commands

## Objective
Expose fundamental clip editing operations through CommandHandler so the AI agent (and future automation) can split, delete, move, duplicate, trim, rename clips, and adjust per-clip gain. Also enrich the `get_tracks` response with clip indices and gain info so the AI can address clips precisely.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH). JUCE module .cpp files fail with `#error "Incorrect use of JUCE cpp file"` when PCH includes JuceHeader.h.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### CommandHandler (engine/src/CommandHandler.h/.cpp)
- Dispatches JSON commands like `{"action":"add_track"}` → calls handler methods.
- `handleCommand()` parses JSON, routes to `handleXxx()` methods via if/else chain.
- Returns JSON response with `"status":"ok"` or `"status":"error"`.
- Has `getTrackById(int)` helper — returns `te::AudioTrack*` by 0-based index.
- Has `makeOk()` and `makeError(msg)` helpers for building responses.
- Works directly with `te::Edit&` reference.

### Clip addressing
- Clips live on `te::AudioTrack` (actually `te::ClipTrack`).
- `track->getClips()` returns all clips on a track.
- Clips are addressed by `track_id` (0-based track index) + `clip_index` (0-based clip index within that track's clip list).
- `te::Clip` has: `getName()`, `setName()`, `getPosition()` (returns `ClipPosition` with `getStart()`, `getEnd()`), `setStart()`, `setEnd()`, `removeFromParent()`.
- `te::WaveAudioClip` (subclass) has gain: access via `clip.state["gainDb"]` or use `clip.getGainDB()` / `clip.setGainDB(float)` if available, or set via `clip.state.setProperty("gainDb", value, nullptr)`.
- Split: `te::ClipTrack::splitClip(clip, timePosition)`.

### AiToolSchema (gui/src/ai/AiToolSchema.cpp)
- `generateCommandDefinitions()` returns `vector<AiToolDefinition>` with `name`, `description`, `inputSchema`.
- Uses `makeSchema()` and `prop()` helpers.
- Command names prefixed with `cmd_`.

### Existing get_tracks response format
Currently returns per clip: `name`, `start` (seconds), `end` (seconds). This phase enriches it.

## Implementation Tasks

### 1. Modify `engine/src/CommandHandler.h` — Add new handler declarations

Add these private handler methods after the existing `handleRecordFromMic()`:
```cpp
juce::var handleSplitClip (const juce::var& params);
juce::var handleDeleteClip (const juce::var& params);
juce::var handleMoveClip (const juce::var& params);
juce::var handleDuplicateClip (const juce::var& params);
juce::var handleTrimClip (const juce::var& params);
juce::var handleSetClipGain (const juce::var& params);
juce::var handleRenameClip (const juce::var& params);
```

Add a private helper after `getTrackById`:
```cpp
te::Clip* getClipByIndex (int trackIndex, int clipIndex);
```

### 2. Modify `engine/src/CommandHandler.cpp` — Add dispatch routing

In `handleCommand()`, add these routes in the if/else chain before the `else` (unknown action) fallback:
```cpp
else if (action == "split_clip")       result = handleSplitClip (parsed);
else if (action == "delete_clip")      result = handleDeleteClip (parsed);
else if (action == "move_clip")        result = handleMoveClip (parsed);
else if (action == "duplicate_clip")   result = handleDuplicateClip (parsed);
else if (action == "trim_clip")        result = handleTrimClip (parsed);
else if (action == "set_clip_gain")    result = handleSetClipGain (parsed);
else if (action == "rename_clip")      result = handleRenameClip (parsed);
```

### 3. Modify `engine/src/CommandHandler.cpp` — Implement getClipByIndex helper

```cpp
te::Clip* CommandHandler::getClipByIndex (int trackIndex, int clipIndex)
{
    auto* track = getTrackById (trackIndex);
    if (track == nullptr)
        return nullptr;

    auto clips = track->getClips();
    if (clipIndex >= 0 && clipIndex < clips.size())
        return clips[clipIndex];
    return nullptr;
}
```

### 4. Modify `engine/src/CommandHandler.cpp` — Enrich handleGetTracks

In the existing `handleGetTracks()`, add `clip_index`, `length`, and `gain_db` to each clip object. The updated clip loop should be:
```cpp
int clipIdx = 0;
for (auto* clip : track->getClips())
{
    auto* clipObj = new juce::DynamicObject();
    auto pos = clip->getPosition();
    clipObj->setProperty ("clip_index", clipIdx);
    clipObj->setProperty ("name", clip->getName());
    clipObj->setProperty ("start", pos.getStart().inSeconds());
    clipObj->setProperty ("end",   pos.getEnd().inSeconds());
    clipObj->setProperty ("length", (pos.getEnd() - pos.getStart()).inSeconds());

    // Clip gain (wave clips only)
    if (auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip))
    {
        auto gainProp = waveClip->state["gainDb"];
        clipObj->setProperty ("gain_db", gainProp.isVoid() ? 0.0 : (double) gainProp);
    }

    clipList.add (juce::var (clipObj));
    ++clipIdx;
}
```

### 5. Modify `engine/src/CommandHandler.cpp` — Implement split_clip

```cpp
juce::var CommandHandler::handleSplitClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index") || ! params.hasProperty ("position"))
        return makeError ("Missing required parameters: track_id, clip_index, position");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];
    double position = params["position"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    auto pos = clip->getPosition();
    if (position <= pos.getStart().inSeconds() || position >= pos.getEnd().inSeconds())
        return makeError ("Split position must be within clip bounds ("
                          + juce::String (pos.getStart().inSeconds()) + " - "
                          + juce::String (pos.getEnd().inSeconds()) + ")");

    auto* clipTrack = dynamic_cast<te::ClipTrack*> (clip->getTrack());
    if (clipTrack == nullptr)
        return makeError ("Clip is not on a clip track");

    clipTrack->splitClip (*clip, te::TimePosition::fromSeconds (position));

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("split_position", position);
    }
    return result;
}
```

### 6. Modify `engine/src/CommandHandler.cpp` — Implement delete_clip

```cpp
juce::var CommandHandler::handleDeleteClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index"))
        return makeError ("Missing required parameters: track_id, clip_index");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    auto clipName = clip->getName();
    clip->removeFromParent();

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("deleted_clip", clipName);
    }
    return result;
}
```

### 7. Modify `engine/src/CommandHandler.cpp` — Implement move_clip

```cpp
juce::var CommandHandler::handleMoveClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index") || ! params.hasProperty ("new_start"))
        return makeError ("Missing required parameters: track_id, clip_index, new_start");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];
    double newStart = params["new_start"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    if (newStart < 0.0)
        return makeError ("new_start must be >= 0");

    clip->setStart (te::TimePosition::fromSeconds (newStart), true, false);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("clip_index", clipIdx);
        obj->setProperty ("new_start", newStart);
    }
    return result;
}
```

### 8. Modify `engine/src/CommandHandler.cpp` — Implement duplicate_clip

```cpp
juce::var CommandHandler::handleDuplicateClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index"))
        return makeError ("Missing required parameters: track_id, clip_index");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    auto* clipTrack = dynamic_cast<te::ClipTrack*> (clip->getTrack());
    if (clipTrack == nullptr)
        return makeError ("Clip is not on a clip track");

    auto pos = clip->getPosition();
    auto endTime = pos.getEnd();

    auto duplicatedState = clip->state.createCopy();
    edit.createNewItemID().writeID (duplicatedState, nullptr);
    te::assignNewIDsToAutomationCurveModifiers (edit, duplicatedState);

    juce::String newClipName;
    if (auto* newClip = clipTrack->insertClipWithState (duplicatedState))
    {
        newClip->setName (clip->getName() + " copy");
        newClip->setStart (endTime, true, false);
        newClipName = newClip->getName();
    }
    else
    {
        return makeError ("Failed to duplicate clip");
    }

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("original_clip_index", clipIdx);
        obj->setProperty ("new_clip_name", newClipName);
    }
    return result;
}
```

### 9. Modify `engine/src/CommandHandler.cpp` — Implement trim_clip

Accepts optional `new_start` and/or `new_end` — at least one must be provided.

```cpp
juce::var CommandHandler::handleTrimClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index"))
        return makeError ("Missing required parameters: track_id, clip_index");

    bool hasNewStart = params.hasProperty ("new_start");
    bool hasNewEnd = params.hasProperty ("new_end");
    if (! hasNewStart && ! hasNewEnd)
        return makeError ("At least one of new_start or new_end must be provided");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    if (hasNewStart)
    {
        double newStart = params["new_start"];
        clip->setStart (te::TimePosition::fromSeconds (newStart), false, true);
    }

    if (hasNewEnd)
    {
        double newEnd = params["new_end"];
        clip->setEnd (te::TimePosition::fromSeconds (newEnd), true);
    }

    auto pos = clip->getPosition();
    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("clip_index", clipIdx);
        obj->setProperty ("start", pos.getStart().inSeconds());
        obj->setProperty ("end", pos.getEnd().inSeconds());
    }
    return result;
}
```

### 10. Modify `engine/src/CommandHandler.cpp` — Implement set_clip_gain

```cpp
juce::var CommandHandler::handleSetClipGain (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index") || ! params.hasProperty ("gain_db"))
        return makeError ("Missing required parameters: track_id, clip_index, gain_db");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];
    double gainDb = params["gain_db"];

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip);
    if (waveClip == nullptr)
        return makeError ("Clip is not an audio clip (gain only applies to wave clips)");

    waveClip->state.setProperty ("gainDb", gainDb, &edit.getUndoManager());

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("clip_index", clipIdx);
        obj->setProperty ("gain_db", gainDb);
    }
    return result;
}
```

### 11. Modify `engine/src/CommandHandler.cpp` — Implement rename_clip

```cpp
juce::var CommandHandler::handleRenameClip (const juce::var& params)
{
    if (! params.hasProperty ("track_id") || ! params.hasProperty ("clip_index") || ! params.hasProperty ("name"))
        return makeError ("Missing required parameters: track_id, clip_index, name");

    int trackId = params["track_id"];
    int clipIdx = params["clip_index"];
    auto newName = params["name"].toString();

    auto* clip = getClipByIndex (trackId, clipIdx);
    if (clip == nullptr)
        return makeError ("Clip not found: track " + juce::String (trackId) + " clip " + juce::String (clipIdx));

    clip->setName (newName);

    auto result = makeOk();
    if (auto* obj = result.getDynamicObject())
    {
        obj->setProperty ("track_id", trackId);
        obj->setProperty ("clip_index", clipIdx);
        obj->setProperty ("name", newName);
    }
    return result;
}
```

### 12. Modify `gui/src/ai/AiToolSchema.cpp` — Register all new commands

In `generateCommandDefinitions()`, add before `return defs;`:

```cpp
// split_clip
defs.push_back ({ "cmd_split_clip",
                  "Split a clip at a specific time position, creating two clips.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "clip_index", prop ("integer", "0-based clip index within the track") },
                                { "position", prop ("number", "Time position in seconds to split at (must be within clip bounds)") } },
                              { "track_id", "clip_index", "position" }) });

// delete_clip
defs.push_back ({ "cmd_delete_clip",
                  "Delete a clip from a track.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "clip_index", prop ("integer", "0-based clip index within the track") } },
                              { "track_id", "clip_index" }) });

// move_clip
defs.push_back ({ "cmd_move_clip",
                  "Move a clip to a new start position in seconds.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "clip_index", prop ("integer", "0-based clip index within the track") },
                                { "new_start", prop ("number", "New start position in seconds") } },
                              { "track_id", "clip_index", "new_start" }) });

// duplicate_clip
defs.push_back ({ "cmd_duplicate_clip",
                  "Duplicate a clip, placing the copy immediately after the original.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "clip_index", prop ("integer", "0-based clip index within the track") } },
                              { "track_id", "clip_index" }) });

// trim_clip
defs.push_back ({ "cmd_trim_clip",
                  "Trim a clip by adjusting its start and/or end time. Provide at least one of new_start or new_end.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "clip_index", prop ("integer", "0-based clip index within the track") },
                                { "new_start", prop ("number", "New start time in seconds (trims from left)") },
                                { "new_end", prop ("number", "New end time in seconds (trims from right)") } },
                              { "track_id", "clip_index" }) });

// set_clip_gain
defs.push_back ({ "cmd_set_clip_gain",
                  "Set the gain of an audio clip in dB. Only works on wave/audio clips.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "clip_index", prop ("integer", "0-based clip index within the track") },
                                { "gain_db", prop ("number", "Gain in decibels (0 = unity, negative = quieter)") } },
                              { "track_id", "clip_index", "gain_db" }) });

// rename_clip
defs.push_back ({ "cmd_rename_clip",
                  "Rename a clip.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "clip_index", prop ("integer", "0-based clip index within the track") },
                                { "name", prop ("string", "New name for the clip") } },
                              { "track_id", "clip_index", "name" }) });
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
- `get_tracks` response includes `clip_index`, `length`, and `gain_db` for each clip.
- `split_clip`, `delete_clip`, `move_clip`, `duplicate_clip`, `trim_clip`, `set_clip_gain`, `rename_clip` commands all work through CommandHandler.
- All new commands registered as `cmd_*` in AiToolSchema.
- AI agent can call all new commands.
- Build compiles with no errors.
