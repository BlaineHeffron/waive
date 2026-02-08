# Phase 06: Track Grouping & VCA Faders

## Objective
Add folder tracks for visual grouping, VCA master faders that control slave track volumes, and group solo/mute. This enables managing large sessions with many tracks.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Context

### Current Track System
- Tracks are flat (no hierarchy) — all displayed at the same level in TimelineComponent and MixerComponent
- Commands: `add_track`, `remove_track`, `reorder_track`, `solo_track`, `mute_track`, `set_track_volume`, `set_track_pan`
- MixerChannelStrip shows fader + pan + solo/mute per track

### Tracktion Engine Support
Tracktion Engine has built-in VCA and folder track support:

```cpp
// Folder tracks
te::FolderTrack* folder = edit.insertNewFolderTrack (
    te::TrackInsertPoint (nullptr, existingTrack), nullptr, false);
folder->setName ("Drums");

// Move a track into a folder
te::moveTrackTo (*audioTrack, *folder);

// VCA — Tracktion's VCA system
// te::VCAPlugin is a built-in plugin that can control volumes of assigned tracks
// Actually, Tracktion uses a folder-based approach where FolderTrack has submix functionality
```

**Note:** Tracktion Engine's folder track system is the primary grouping mechanism. A FolderTrack can contain AudioTracks and acts as a submix. VCA-style control is achieved through the folder track's volume/pan which applies to all child tracks.

### Key Tracktion API
```cpp
// Create folder track
auto* folderTrack = edit.insertNewFolderTrack (insertPoint, nullptr, false);

// Get child tracks of a folder
auto children = folderTrack->getAllSubTracks (false); // direct children only

// Check if a track is in a folder
auto* parent = track->getParentFolderTrack();

// Folder track volume acts as VCA for children
folderTrack->getVolumePlugin()->setVolumeDb (newDb);
```

## Implementation Tasks

### 1. Add folder track commands

In `engine/src/CommandHandler.cpp`, add new commands:

**`add_folder_track`:**
- Params: `name` (string)
- Creates a new FolderTrack in the edit
- Returns the track index

**`move_track_to_folder`:**
- Params: `track_index` (int), `folder_index` (int)
- Moves the specified track into the folder track
- Validates both indices

**`remove_from_folder`:**
- Params: `track_index` (int)
- Moves the track out of its parent folder to the top level

### 2. Update TimelineComponent for folder tracks

In `gui/src/ui/TimelineComponent.cpp`:
- Folder tracks display as a collapsible header row with a disclosure triangle
- Clicking the triangle expands/collapses child tracks
- Folder track header shows: name, solo/mute buttons, volume fader (VCA)
- Child tracks are indented ~20px from the left
- Collapsed folders hide their child track lanes
- Drag-and-drop tracks into/out of folders (or via right-click → "Move to Folder")

### 3. Update MixerComponent for folder tracks

In `gui/src/ui/MixerComponent.cpp` and `MixerChannelStrip.cpp`:
- Folder tracks get a channel strip with a distinct appearance (e.g., different background colour from palette, slightly wider)
- Folder strip shows: name, VCA fader, pan, solo/mute
- Folder solo: solos all child tracks
- Folder mute: mutes all child tracks
- VCA fader: adjusts the folder track's volume which affects all children
- A visual separator between folder groups

### 4. Group solo/mute logic

When a folder track is soloed:
- All child tracks are soloed
- All non-child tracks are implicitly muted (standard solo behaviour)

When a folder track is muted:
- All child tracks are muted regardless of individual mute state

Implement in CommandHandler for `solo_track` and `mute_track`:
- Detect if the target is a FolderTrack
- Apply the action to all children

### 5. Update get_tracks command

The `get_tracks` command should include folder information:
- Add `"parent_folder"` field (index or null) to each track in the response
- Add `"is_folder"` boolean field
- Add `"children"` array (indices) for folder tracks

### 6. Add to command list

Register the new commands in CommandHandler's dispatch map and add their schemas.

## Files Expected To Change
- `engine/src/CommandHandler.h` (add folder commands)
- `engine/src/CommandHandler.cpp` (implement folder commands, update get_tracks, solo/mute group logic)
- `gui/src/ui/TimelineComponent.h` (folder track display, collapse/expand)
- `gui/src/ui/TimelineComponent.cpp` (implement folder rendering)
- `gui/src/ui/MixerComponent.cpp` (folder channel strips)
- `gui/src/ui/MixerChannelStrip.h` (folder strip variant)
- `gui/src/ui/MixerChannelStrip.cpp` (implement folder strip)

## Validation

```bash
# Build must succeed
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))

# All tests must still pass
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- Folder tracks can be created via command
- Tracks can be moved into/out of folders
- Folder tracks are visually distinct in timeline and mixer
- Folder collapse/expand works in timeline
- Folder solo/mute affects all children
- Folder VCA fader controls group volume
- get_tracks returns hierarchy information
- All existing tests still pass
