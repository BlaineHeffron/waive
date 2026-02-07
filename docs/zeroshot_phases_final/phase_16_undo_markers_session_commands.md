# Phase 16: Undo/Redo, Markers & Session Management Commands

## Objective
Add utility commands that make the AI agent more capable and self-correcting: undo/redo (so the AI can revert its own mistakes), named markers (for navigation and arrangement), track reordering, and time signature changes. These are standard DAW features that are currently only available through the GUI but not via commands.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### Undo/Redo
- **EditSession** (`gui/src/edit/EditSession.h`) has `undo()` and `redo()` methods that wrap `edit.getUndoManager().undo()` / `.redo()` and stop recording + refresh SelectionManagers.
- **UndoableCommandHandler** wraps CommandHandler with undo transactions. Read-only commands pass through, mutating commands get `performEdit()`.
- The AI agent dispatches commands via `AiAgent::executeToolCall()` → `executeCommand()` which calls `commandHandler.handleCommand()`.
- **Problem**: CommandHandler lives in `engine/src/` and has no access to EditSession's undo/redo. The undo/redo commands must be handled at the GUI layer (UndoableCommandHandler or AiAgent).

### Markers
- Tracktion Engine has `te::MarkerTrack` accessible via `edit.getMarkerTrack()`.
- Markers are `te::MarkerClip` objects on the marker track.
- `te::MarkerClip` has `setName()`, position (start/end), `getMarkerID()`.
- SessionComponent has tempo/time-sig marker buttons but NO general-purpose named markers.

### Track Reordering
- Tracktion Engine: `edit.moveTrack()` or manipulating the track list's ValueTree order.
- `te::getAudioTracks(edit)` returns tracks in order; the order is determined by their position in the Edit's ValueTree.

### Time Signature
- SessionComponent has `setTimeSignatureForTesting(numerator, denominator)` and `insertTimeSigMarkerAtPlayheadForTesting()`.
- `edit.tempoSequence` has `insertTimeSig()` and the default time sig.
- `set_tempo` exists in CommandHandler but `set_time_signature` does not.

## Implementation Tasks

### 1. Modify `engine/src/CommandHandler.h` — Add new handler declarations

```cpp
// Session utility commands
juce::var handleSetTimeSignature (const juce::var& params);
juce::var handleAddMarker (const juce::var& params);
juce::var handleRemoveMarker (const juce::var& params);
juce::var handleListMarkers();
juce::var handleReorderTrack (const juce::var& params);
```

### 2. Modify `engine/src/CommandHandler.cpp` — Add command routing

```cpp
else if (action == "set_time_signature")  result = handleSetTimeSignature (parsed);
else if (action == "add_marker")          result = handleAddMarker (parsed);
else if (action == "remove_marker")       result = handleRemoveMarker (parsed);
else if (action == "list_markers")        result = handleListMarkers ();
else if (action == "reorder_track")       result = handleReorderTrack (parsed);
```

Note: `undo` and `redo` are NOT routed through CommandHandler (they need EditSession). They are handled in AiAgent::executeToolCall().

### 3. Modify `engine/src/CommandHandler.cpp` — Implement handleSetTimeSignature

```cpp
juce::var CommandHandler::handleSetTimeSignature (const juce::var& params)
{
    auto numerator = (int) params["numerator"];
    auto denominator = (int) params["denominator"];

    if (numerator < 1 || numerator > 32)
        return makeError ("Numerator must be 1-32");
    if (denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16)
        return makeError ("Denominator must be 2, 4, 8, or 16");

    auto& tempoSeq = edit.tempoSequence;
    // Set the default time signature (first entry)
    if (auto* timeSig = tempoSeq.getTimeSig (0))
    {
        timeSig->numerator = numerator;
        timeSig->denominator = denominator;
    }

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("numerator", numerator);
    result->setProperty ("denominator", denominator);
    return juce::var (result);
}
```

### 4. Modify `engine/src/CommandHandler.cpp` — Implement marker commands

```cpp
juce::var CommandHandler::handleAddMarker (const juce::var& params)
{
    auto timeSec = (double) params["time"];
    auto name = params["name"].toString();

    if (name.isEmpty())
        return makeError ("Marker name is required");
    if (timeSec < 0.0)
        return makeError ("Time must be >= 0");

    auto* markerTrack = edit.getMarkerTrack();
    if (! markerTrack)
        return makeError ("Could not access marker track");

    auto tp = te::TimePosition::fromSeconds (timeSec);
    // Insert a marker clip with a small duration
    auto markerClip = markerTrack->insertMarkerClip (
        name, { te::TimeRange (tp, tp + te::TimeDuration::fromSeconds (0.1)) }, nullptr);

    if (! markerClip)
        return makeError ("Failed to create marker");

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("name", name);
    result->setProperty ("time", timeSec);
    result->setProperty ("marker_id", markerClip->getMarkerID());
    return juce::var (result);
}

juce::var CommandHandler::handleRemoveMarker (const juce::var& params)
{
    auto markerId = (int) params["marker_id"];

    auto* markerTrack = edit.getMarkerTrack();
    if (! markerTrack)
        return makeError ("Could not access marker track");

    for (auto* clip : markerTrack->getClips())
    {
        if (auto* mc = dynamic_cast<te::MarkerClip*> (clip))
        {
            if (mc->getMarkerID() == markerId)
            {
                mc->removeFromParent();
                return makeOk();
            }
        }
    }

    return makeError ("Marker not found with ID: " + juce::String (markerId));
}

juce::var CommandHandler::handleListMarkers()
{
    auto* markerTrack = edit.getMarkerTrack();

    juce::Array<juce::var> markerList;
    if (markerTrack)
    {
        for (auto* clip : markerTrack->getClips())
        {
            if (auto* mc = dynamic_cast<te::MarkerClip*> (clip))
            {
                auto* m = new juce::DynamicObject();
                m->setProperty ("marker_id", mc->getMarkerID());
                m->setProperty ("name", mc->getName());
                m->setProperty ("time", mc->getPosition().getStart().inSeconds());
                markerList.add (juce::var (m));
            }
        }
    }

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("markers", markerList);
    return juce::var (result);
}
```

### 5. Modify `engine/src/CommandHandler.cpp` — Implement handleReorderTrack

```cpp
juce::var CommandHandler::handleReorderTrack (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto newPosition = (int) params["new_position"];

    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    auto audioTracks = te::getAudioTracks (edit);
    if (newPosition < 0 || newPosition >= audioTracks.size())
        return makeError ("new_position out of range (0-" + juce::String (audioTracks.size() - 1) + ")");

    // Tracktion Engine: move track in the ValueTree
    auto& trackList = edit.getTrackList();
    auto trackState = track->state;
    auto parentTree = trackState.getParent();

    // Find target position in the parent ValueTree
    auto* destTrack = audioTracks[newPosition];
    auto destIndex = parentTree.indexOf (destTrack->state);
    parentTree.moveChild (parentTree.indexOf (trackState), destIndex, &edit.getUndoManager());

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("track_id", trackId);
    result->setProperty ("new_position", newPosition);
    return juce::var (result);
}
```

### 6. Modify `gui/src/ai/AiAgent.cpp` — Handle undo/redo in executeToolCall

Undo/redo need access to EditSession, not CommandHandler. Handle them before command dispatch:

```cpp
juce::String AiAgent::executeToolCall (const ChatMessage::ToolCall& call)
{
    // Handle undo/redo specially — these need EditSession, not CommandHandler
    if (call.name == "cmd_undo")
    {
        if (editSession)
        {
            if (editSession->getUndoDescription().isNotEmpty())
            {
                auto desc = editSession->getUndoDescription();
                editSession->undo();
                return "{\"status\":\"ok\",\"undone\":\"" + desc.replace ("\"", "\\\"") + "\"}";
            }
            return "{\"status\":\"ok\",\"message\":\"Nothing to undo\"}";
        }
        return "{\"status\":\"error\",\"message\":\"No edit session available\"}";
    }

    if (call.name == "cmd_redo")
    {
        if (editSession)
        {
            if (editSession->getRedoDescription().isNotEmpty())
            {
                auto desc = editSession->getRedoDescription();
                editSession->redo();
                return "{\"status\":\"ok\",\"redone\":\"" + desc.replace ("\"", "\\\"") + "\"}";
            }
            return "{\"status\":\"ok\",\"message\":\"Nothing to redo\"}";
        }
        return "{\"status\":\"error\",\"message\":\"No edit session available\"}";
    }

    // ... existing routing (cmd_search_tools, cmd_*, tool_*, etc.)
```

**Note**: AiAgent needs access to EditSession. If it doesn't already have a reference, add one. Check the constructor — it likely receives a commandHandler which wraps EditSession. If AiAgent has `UndoableCommandHandler& commandHandler`, it can access EditSession through `commandHandler.getEditSession()`. If no such accessor exists, add one to UndoableCommandHandler:

```cpp
// In UndoableCommandHandler.h:
EditSession& getEditSession() { return editSession; }
```

### 7. Modify `gui/src/ai/AiToolSchema.cpp` — Register all new commands

```cpp
// undo
defs.push_back ({ "cmd_undo",
                  "Undo the last edit operation. Returns the description of what was undone.",
                  makeSchema ("object") });

// redo
defs.push_back ({ "cmd_redo",
                  "Redo the last undone operation. Returns the description of what was redone.",
                  makeSchema ("object") });

// set_time_signature
defs.push_back ({ "cmd_set_time_signature",
                  "Set the project time signature. Numerator 1-32, denominator must be 2, 4, 8, or 16.",
                  makeSchema ("object",
                              { { "numerator", prop ("integer", "Beats per bar (1-32)") },
                                { "denominator", prop ("integer", "Note value per beat (2, 4, 8, or 16)") } },
                              { "numerator", "denominator" }) });

// add_marker
defs.push_back ({ "cmd_add_marker",
                  "Add a named marker at a specific time position. Useful for marking song sections (verse, chorus, bridge, etc.).",
                  makeSchema ("object",
                              { { "name", prop ("string", "Marker name (e.g. 'Verse 1', 'Chorus', 'Bridge')") },
                                { "time", prop ("number", "Time position in seconds") } },
                              { "name", "time" }) });

// remove_marker
defs.push_back ({ "cmd_remove_marker",
                  "Remove a marker by its marker_id. Use list_markers to find marker IDs.",
                  makeSchema ("object",
                              { { "marker_id", prop ("integer", "Marker ID to remove") } },
                              { "marker_id" }) });

// list_markers
defs.push_back ({ "cmd_list_markers",
                  "List all markers in the project with their names, times, and IDs.",
                  makeSchema ("object") });

// reorder_track
defs.push_back ({ "cmd_reorder_track",
                  "Move a track to a new position in the track list. 0-based indices.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based index of track to move") },
                                { "new_position", prop ("integer", "0-based target position") } },
                              { "track_id", "new_position" }) });
```

### 8. Modify `gui/src/edit/UndoableCommandHandler.cpp` — Add read-only bypass

Add `list_markers` to the read-only command set:

```cpp
if (action == "list_markers" || action == "get_automation_params" || action == "get_automation_points"
    || /* existing read-only checks */)
```

Note: `undo` and `redo` are not routed through UndoableCommandHandler at all — they're handled in AiAgent.

## Files Expected To Change
- `engine/src/CommandHandler.h`
- `engine/src/CommandHandler.cpp`
- `gui/src/ai/AiAgent.h` (if EditSession accessor needed)
- `gui/src/ai/AiAgent.cpp`
- `gui/src/ai/AiToolSchema.cpp`
- `gui/src/edit/UndoableCommandHandler.h` (if getEditSession() accessor needed)
- `gui/src/edit/UndoableCommandHandler.cpp`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- `undo` and `redo` commands work from AI agent, correctly using EditSession.
- `set_time_signature` sets numerator/denominator with validation.
- `add_marker`, `remove_marker`, `list_markers` manage named markers on the marker track.
- `reorder_track` moves tracks to new positions in the track list.
- All 7 new commands registered in AiToolSchema.
- Read-only commands (`list_markers`) bypass undo transactions.
- Build compiles with no errors.
- Existing tests still pass.
