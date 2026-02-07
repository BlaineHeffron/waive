# Phase 15: Automation & Clip Fade Commands

## Objective
Expose the existing automation infrastructure (TrackLaneComponent has full automation UI — lanes, curves, parameter selection via ComboBox) and clip fade handles (ClipComponent has fade in/out rendering + drag) to the AI agent via CommandHandler commands. This closes the biggest gap: the AI currently cannot programmatically create or edit automation curves or set clip fades, even though the UI supports both.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- The `waive::ColourPalette` struct has NO member named `error`. Use `danger` for error colors.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### Existing automation infrastructure (GUI layer)
- **TrackLaneComponent** has `automationLaneHeight = 34`, `automationParamCombo` (ComboBox), `automationParams` (ReferenceCountedArray of AutomatableParameter)
- `refreshAutomationParams()` populates the combo box with all automatable parameters for the track
- `findNearbyAutomationPoint()`, `normalisedFromAutomationY()`, `automationYForNormalisedValue()` — full editing support
- Mouse handlers allow adding/dragging/removing automation points on the curve
- Tracktion Engine API: `te::AutomationCurve`, `te::AutomatableParameter`, `curve.addPoint()`, `curve.removePoint()`, `curve.getNumPoints()`

### Existing clip fade infrastructure (GUI layer)
- **ClipComponent** has `isFadeInZone()`, `isFadeOutZone()`, `DragMode::FadeIn`, `DragMode::FadeOut`
- `dragStartFadeIn`, `dragStartFadeOut` for drag state
- Rendering code reads `waveClip->getFadeIn().inSeconds()`, `waveClip->getFadeOut().inSeconds()`
- Tracktion Engine API: `te::WaveAudioClip::setFadeIn()`, `te::WaveAudioClip::setFadeOut()`, `te::TimeDuration::fromSeconds()`
- **ToolDiff** already has `ToolDiffKind::clipFadeChanged`

### CommandHandler (engine/src/)
- JSON-in/JSON-out dispatcher
- Has `getTrackById()` and `getClipByIndex()` helpers
- Currently 31+ commands but NONE for automation or fades

### AiToolSchema (gui/src/ai/)
- Registers command definitions for AI agent
- Uses `makeSchema()` / `prop()` helpers

## Implementation Tasks

### 1. Modify `engine/src/CommandHandler.h` — Add automation and fade handler declarations

Add these private method declarations:

```cpp
// Automation commands
juce::var handleGetAutomationParams (const juce::var& params);
juce::var handleGetAutomationPoints (const juce::var& params);
juce::var handleAddAutomationPoint (const juce::var& params);
juce::var handleRemoveAutomationPoint (const juce::var& params);
juce::var handleClearAutomation (const juce::var& params);

// Clip fade commands
juce::var handleSetClipFade (const juce::var& params);
```

### 2. Modify `engine/src/CommandHandler.cpp` — Add command routing

In `handleCommand()`, add routing for the new commands:

```cpp
else if (action == "get_automation_params")    result = handleGetAutomationParams (parsed);
else if (action == "get_automation_points")    result = handleGetAutomationPoints (parsed);
else if (action == "add_automation_point")     result = handleAddAutomationPoint (parsed);
else if (action == "remove_automation_point")  result = handleRemoveAutomationPoint (parsed);
else if (action == "clear_automation")         result = handleClearAutomation (parsed);
else if (action == "set_clip_fade")            result = handleSetClipFade (parsed);
```

### 3. Modify `engine/src/CommandHandler.cpp` — Implement handleGetAutomationParams

List all automatable parameters for a track:

```cpp
juce::var CommandHandler::handleGetAutomationParams (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    juce::Array<juce::var> paramList;
    auto allParams = track->getAllAutomatableParams();
    for (int i = 0; i < allParams.size(); ++i)
    {
        auto* p = allParams[i];
        auto* pObj = new juce::DynamicObject();
        pObj->setProperty ("index", i);
        pObj->setProperty ("name", p->getParameterName());
        pObj->setProperty ("label", p->getLabel());
        pObj->setProperty ("current_value", p->getCurrentValue());
        pObj->setProperty ("default_value", p->getDefaultValue());
        // Normalised range is always 0-1 in Tracktion
        pObj->setProperty ("range_min", 0.0);
        pObj->setProperty ("range_max", 1.0);
        paramList.add (juce::var (pObj));
    }

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("track_id", trackId);
    result->setProperty ("params", paramList);
    return juce::var (result);
}
```

### 4. Modify `engine/src/CommandHandler.cpp` — Implement handleGetAutomationPoints

Return existing automation curve points for a specific parameter:

```cpp
juce::var CommandHandler::handleGetAutomationPoints (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto paramIndex = (int) params["param_index"];
    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();

    juce::Array<juce::var> points;
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        auto* pt = new juce::DynamicObject();
        pt->setProperty ("index", i);
        pt->setProperty ("time", curve.getPointTime (i).inSeconds());
        pt->setProperty ("value", curve.getPointValue (i));
        // curve type: 0=linear, 1=bezier
        pt->setProperty ("curve_value", curve.getPointCurve (i));
        points.add (juce::var (pt));
    }

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("track_id", trackId);
    result->setProperty ("param_index", paramIndex);
    result->setProperty ("param_name", param->getParameterName());
    result->setProperty ("points", points);
    return juce::var (result);
}
```

### 5. Modify `engine/src/CommandHandler.cpp` — Implement handleAddAutomationPoint

Add a point to an automation curve:

```cpp
juce::var CommandHandler::handleAddAutomationPoint (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto paramIndex = (int) params["param_index"];
    auto timeSec = (double) params["time"];
    auto value = (float) (double) params["value"];
    auto curveVal = params.hasProperty ("curve") ? (float) (double) params["curve"] : 0.0f;

    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    value = juce::jlimit (0.0f, 1.0f, value);

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();
    auto tp = te::TimePosition::fromSeconds (timeSec);
    curve.addPoint (tp, value, curveVal);

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("track_id", trackId);
    result->setProperty ("param_name", param->getParameterName());
    result->setProperty ("time", timeSec);
    result->setProperty ("value", (double) value);
    result->setProperty ("total_points", curve.getNumPoints());
    return juce::var (result);
}
```

### 6. Modify `engine/src/CommandHandler.cpp` — Implement handleRemoveAutomationPoint

Remove a specific automation point by index:

```cpp
juce::var CommandHandler::handleRemoveAutomationPoint (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto paramIndex = (int) params["param_index"];
    auto pointIndex = (int) params["point_index"];

    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();

    if (pointIndex < 0 || pointIndex >= curve.getNumPoints())
        return makeError ("Point index out of range: " + juce::String (pointIndex));

    curve.removePoint (pointIndex);

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("remaining_points", curve.getNumPoints());
    return juce::var (result);
}
```

### 7. Modify `engine/src/CommandHandler.cpp` — Implement handleClearAutomation

Remove all automation points for a parameter:

```cpp
juce::var CommandHandler::handleClearAutomation (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto paramIndex = (int) params["param_index"];

    auto* track = getTrackById (trackId);
    if (! track)
        return makeError ("Track not found: " + juce::String (trackId));

    auto allParams = track->getAllAutomatableParams();
    if (paramIndex < 0 || paramIndex >= allParams.size())
        return makeError ("Parameter index out of range: " + juce::String (paramIndex));

    auto* param = allParams[paramIndex];
    auto& curve = param->getCurve();
    curve.clear();

    return makeOk();
}
```

### 8. Modify `engine/src/CommandHandler.cpp` — Implement handleSetClipFade

Set fade in and/or fade out duration on a wave clip:

```cpp
juce::var CommandHandler::handleSetClipFade (const juce::var& params)
{
    auto trackId = (int) params["track_id"];
    auto clipIndex = (int) params["clip_index"];

    auto* clip = getClipByIndex (trackId, clipIndex);
    if (! clip)
        return makeError ("Clip not found: track " + juce::String (trackId)
                          + " clip " + juce::String (clipIndex));

    auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip);
    if (! waveClip)
        return makeError ("Clip is not an audio clip (MIDI clips do not support fades)");

    bool changed = false;

    if (params.hasProperty ("fade_in"))
    {
        auto fadeInSec = juce::jmax (0.0, (double) params["fade_in"]);
        waveClip->setFadeIn (te::TimeDuration::fromSeconds (fadeInSec));
        changed = true;
    }

    if (params.hasProperty ("fade_out"))
    {
        auto fadeOutSec = juce::jmax (0.0, (double) params["fade_out"]);
        waveClip->setFadeOut (te::TimeDuration::fromSeconds (fadeOutSec));
        changed = true;
    }

    if (! changed)
        return makeError ("Provide fade_in and/or fade_out (in seconds)");

    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("fade_in", waveClip->getFadeIn().inSeconds());
    result->setProperty ("fade_out", waveClip->getFadeOut().inSeconds());
    return juce::var (result);
}
```

### 9. Modify `gui/src/ai/AiToolSchema.cpp` — Register new commands

Add definitions for all 6 new commands in `generateCommandDefinitions()`:

```cpp
// get_automation_params
defs.push_back ({ "cmd_get_automation_params",
                  "Get all automatable parameters for a track (volume, pan, plugin params). Returns parameter indices needed for other automation commands.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") } },
                              { "track_id" }) });

// get_automation_points
defs.push_back ({ "cmd_get_automation_points",
                  "Get all automation points for a specific parameter. Use get_automation_params first to find the param_index.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "param_index", prop ("integer", "Parameter index from get_automation_params") } },
                              { "track_id", "param_index" }) });

// add_automation_point
defs.push_back ({ "cmd_add_automation_point",
                  "Add an automation point at a specific time with a normalised value (0.0-1.0). Use get_automation_params to find the param_index.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "param_index", prop ("integer", "Parameter index from get_automation_params") },
                                { "time", prop ("number", "Time in seconds for the automation point") },
                                { "value", prop ("number", "Normalised value 0.0-1.0") },
                                { "curve", prop ("number", "Curve tension (-1.0 to 1.0, 0=linear). Optional.") } },
                              { "track_id", "param_index", "time", "value" }) });

// remove_automation_point
defs.push_back ({ "cmd_remove_automation_point",
                  "Remove an automation point by its index. Use get_automation_points to see current points.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "param_index", prop ("integer", "Parameter index") },
                                { "point_index", prop ("integer", "0-based index of the point to remove") } },
                              { "track_id", "param_index", "point_index" }) });

// clear_automation
defs.push_back ({ "cmd_clear_automation",
                  "Remove all automation points for a parameter, resetting it to its current static value.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "param_index", prop ("integer", "Parameter index") } },
                              { "track_id", "param_index" }) });

// set_clip_fade
defs.push_back ({ "cmd_set_clip_fade",
                  "Set fade-in and/or fade-out duration on an audio clip. Provide fade_in and/or fade_out in seconds.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "clip_index", prop ("integer", "0-based clip index within the track") },
                                { "fade_in", prop ("number", "Fade-in duration in seconds") },
                                { "fade_out", prop ("number", "Fade-out duration in seconds") } },
                              { "track_id", "clip_index" }) });
```

### 10. Modify `gui/src/edit/UndoableCommandHandler.cpp` — Add read-only bypass

The automation query commands are read-only and should bypass undo transactions. Add them to the read-only action set:

```cpp
// In the read-only check:
if (action == "get_automation_params" || action == "get_automation_points"
    || action == "ping" || action == "get_edit_state" /* ... existing read-only checks */)
```

## Files Expected To Change
- `engine/src/CommandHandler.h`
- `engine/src/CommandHandler.cpp`
- `gui/src/ai/AiToolSchema.cpp`
- `gui/src/edit/UndoableCommandHandler.cpp`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- 6 new commands: `get_automation_params`, `get_automation_points`, `add_automation_point`, `remove_automation_point`, `clear_automation`, `set_clip_fade`.
- AI can query all automatable parameters for any track.
- AI can add/remove/clear automation points on any parameter.
- AI can set fade in/out on audio clips.
- Automation query commands bypass undo transactions.
- All AI tool definitions registered in AiToolSchema.
- Build compiles with no errors.
- Existing tests still pass.
