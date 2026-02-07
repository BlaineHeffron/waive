# Phase 02: Recording Improvements

## Objective
Add per-track record arming with input device selection in the mixer, plus a one-click "Mic Rec" button in the transport toolbar that auto-creates an armed track and starts recording. Also expose new commands to the AI agent.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH). JUCE module .cpp files fail with `#error "Incorrect use of JUCE cpp file"` when PCH includes JuceHeader.h.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- The `waive::ColourPalette` struct has NO member named `error`. Use `danger` for error colors.
- Available fonts: `header()`, `subheader()`, `body()`, `label()`, `caption()`, `mono()`, `meter()`.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### MixerChannelStrip (gui/src/ui/MixerChannelStrip.h/.cpp)
- Has two constructors: one for `te::AudioTrack&` (track strip) and one for `te::Edit&` (master strip)
- Members: `editSession`, `track` (nullptr for master), `masterEdit`, `isMaster` flag
- Layout in `resized()` currently: name label → solo/mute buttons → pan knob → fader + meter
- `pollState()` called from parent MixerComponent timer to sync UI state from engine
- `setupControls()` called from constructors to configure sliders/buttons
- Has `suppressControlCallbacks` pattern to avoid feedback loops
- Strip width: `static constexpr int stripWidth = 80`

### SessionComponent (gui/src/ui/SessionComponent.h/.cpp)
- Transport toolbar has: playButton, stopButton, recordButton, addTrackButton, loopButton, punchButton, etc.
- `record()` method exists — calls `runTransportAction("transport_record")`
- Layout: toolbar at top, then timeline+mixer below with a resizer

### CommandHandler (engine/src/CommandHandler.h/.cpp)
- Dispatches JSON commands like `{"action":"add_track"}` → calls handler methods
- `handleCommand()` parses JSON, routes to `handleXxx()` methods
- Returns JSON response with `"status":"ok"` or error
- Has `getTrackById(int)` helper

### AiToolSchema (gui/src/ai/AiToolSchema.cpp)
- `generateCommandDefinitions()` returns `vector<AiToolDefinition>` — each with name, description, inputSchema
- Uses `makeSchema()` and `prop()` helpers to build JSON schemas
- Command names prefixed with `cmd_` in the definitions

## Implementation Tasks

### 1. Modify `gui/src/ui/MixerChannelStrip.h` — Add arm button and input combo

Add new members after the existing `muteButton`:
```cpp
juce::ToggleButton armButton;
juce::ComboBox inputCombo;
```

### 2. Modify `gui/src/ui/MixerChannelStrip.cpp` — Wire arm + input controls

In `setupControls()`, for non-master strips only (`if (!isMaster)`):

**Arm button:**
- Configure: `armButton.setButtonText ("R")`, make it a toggle
- Style: use `armButton.setColour (juce::ToggleButton::tickColourId, ...)` with a red-ish color from palette (`pal->danger`) when armed
- On click callback: find the track's input device instances and toggle recording enabled state
  - Use `editSession.getEdit().getEditInputDevices()` to access input device management
  - Call `te::InputDeviceInstance::setRecordingEnabled()` on the track's assigned input
- Add as child and make visible

**Input combo:**
- Populate from `editSession.getEdit().engine.getDeviceManager().getWaveInputDevices()`
- Each combo item: device name, ID = device index + 1 (0 = no input)
- On change callback: assign the selected wave input device to this track
  - Use `edit.getEditInputDevices()` to create or get the input device instance for this track
- Add as child and make visible

**For master strips (isMaster == true):**
- Do NOT add arm or input controls

In `pollState()`, for non-master strips:
- Check if any `te::InputDeviceInstance` assigned to this track has recording enabled
- Update `armButton` toggle state (with `suppressControlCallbacks` guard)
- Update `inputCombo` selection based on currently assigned input device

In `resized()`, update the layout to insert arm + input row. New layout order:
```
[Name label]       — top
[S] [M]            — solo/mute row
[R] [Input ▾]      — NEW: arm + input row
[Pan knob]         — pan
[Fader + meter]    — fills remaining space
```

Each row should be approximately 24px high. The arm button takes ~24px width, input combo gets the rest.

### 3. Modify `gui/src/ui/SessionComponent.h` — Add mic record button

Add a new member alongside the existing transport buttons:
```cpp
juce::TextButton recordFromMicButton { "Mic Rec" };
```

Add a new private method:
```cpp
void recordFromMic();
```

### 4. Modify `gui/src/ui/SessionComponent.cpp` — Implement mic record

In the constructor, set up the button:
- `addAndMakeVisible (recordFromMicButton)`
- Set tooltip: `"Record from microphone (auto-creates armed track)"`
- On click: call `recordFromMic()`
- Style with palette colors — e.g. `pal->danger` for the button color to indicate recording

In `resized()`, place `recordFromMicButton` in the transport toolbar primary row, right after the existing `recordButton`. Give it about 60-70px width.

Implement `recordFromMic()`:
1. Get the edit: `auto& edit = editSession.getEdit();`
2. Find the first audio track with no clips, or create a new one:
   ```cpp
   te::AudioTrack* targetTrack = nullptr;
   for (auto* t : te::getAudioTracks (edit))
   {
       if (t->getClips().isEmpty())
       {
           targetTrack = t;
           break;
       }
   }
   if (targetTrack == nullptr)
   {
       edit.ensureNumberOfAudioTracks (te::getAudioTracks (edit).size() + 1);
       targetTrack = te::getAudioTracks (edit).getLast();
   }
   ```
3. Get the first wave input device:
   ```cpp
   auto& dm = edit.engine.getDeviceManager();
   auto waveInputs = dm.getWaveInputDevices();
   if (waveInputs.isEmpty()) return;
   auto* waveIn = waveInputs.getFirst();
   ```
4. Create/get input device instance on the track and arm it:
   ```cpp
   auto& eid = edit.getEditInputDevices();
   // Assign the wave input to the target track
   for (auto* idi : eid.getDevicesForTargetTrack (*targetTrack))
       idi->setRecordingEnabled (true);
   // If no device instance exists yet, we may need to create one
   ```
   Note: The exact Tracktion Engine API for assigning inputs may vary. The key pattern is:
   - Get or create a `te::InputDeviceInstance` for the wave input device on the target track
   - Set `recordingEnabled = true` on it
5. Start recording: `edit.getTransport().record (false);`

### 5. Modify `engine/src/CommandHandler.h` — Declare new handlers

Add two new private handler methods:
```cpp
juce::var handleArmTrack (const juce::var& params);
juce::var handleRecordFromMic();
```

### 6. Modify `engine/src/CommandHandler.cpp` — Implement new commands

In `handleCommand()`, add routing for the two new actions in the action dispatch:
- `"arm_track"` → `handleArmTrack(params)`
- `"record_from_mic"` → `handleRecordFromMic()`

**`handleArmTrack()`:**
- Parse params: `track_id` (required int), `armed` (required bool), `input_device` (optional string)
- Get track via `getTrackById(track_id)`
- If `input_device` specified, find the matching wave input device from `edit.engine.getDeviceManager()`
- Toggle recording enabled on the track's input device instances
- Return `makeOk()` or `makeError()`

**`handleRecordFromMic()`:**
- Same logic as SessionComponent::recordFromMic() but using the edit reference directly
- Find empty track or create one
- Get first wave input, assign to track, arm it
- Start recording via `edit.getTransport().record(false)`
- Return `makeOk()`

### 7. Modify `gui/src/ai/AiToolSchema.cpp` — Register new commands

In `generateCommandDefinitions()`, add before the `return defs;`:

```cpp
// arm_track
defs.push_back ({ "cmd_arm_track",
                  "Arm or disarm a track for recording, optionally assigning an input device.",
                  makeSchema ("object",
                              { { "track_id", prop ("integer", "0-based track index") },
                                { "armed", prop ("boolean", "true to arm, false to disarm") },
                                { "input_device", prop ("string", "Optional: wave input device name to assign") } },
                              { "track_id", "armed" }) });

// record_from_mic
defs.push_back ({ "cmd_record_from_mic",
                  "Quick-record from microphone: creates or finds an empty track, arms it with the default input, and starts recording.",
                  makeSchema ("object") });
```

## Files Expected To Change
- `gui/src/ui/MixerChannelStrip.h`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/SessionComponent.h`
- `gui/src/ui/SessionComponent.cpp`
- `engine/src/CommandHandler.h`
- `engine/src/CommandHandler.cpp`
- `gui/src/ai/AiToolSchema.cpp`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- MixerChannelStrip shows arm toggle ("R") and input device combo on non-master track strips.
- Arm state syncs bidirectionally between UI and engine.
- "Mic Rec" button in transport toolbar auto-creates an armed track and starts recording.
- `arm_track` and `record_from_mic` commands work via CommandHandler.
- AI agent can call `cmd_arm_track` and `cmd_record_from_mic`.
- Build compiles with no errors.
