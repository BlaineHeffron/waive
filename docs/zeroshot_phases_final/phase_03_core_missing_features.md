# Phase 03: Core Missing Features

## Objective
Add the most impactful missing DAW features identified in the feature audit: track deletion, clip fades, auto-save, loop region visualization, and track reordering.

## Scope
- Track delete from UI (command exists, no UI surface).
- Clip fade-in/fade-out handles with visual overlay.
- Auto-save with configurable interval and recovery.
- Loop region visualization on timeline.
- Track context menu with delete/duplicate/reorder.

## Implementation Tasks

1. Add track delete UI.
- Add "Delete Track" to a right-click context menu on `TrackLaneComponent` header.
- Also add "Delete Track" to the Edit menu in `MainComponent`.
- Use `UndoableCommandHandler` to wrap the deletion so it's undoable.
- The underlying `remove_track` command already exists in `CommandHandler`.
- Show a confirmation dialog if the track contains clips.

2. Add clip fade-in and fade-out handles.
- In `ClipComponent`, add two new drag zones (8px each) at top-left and top-right corners for fade-in and fade-out respectively.
- During drag, adjust `clip.setFadeIn()` / `clip.setFadeOut()` via `EditSession::performEdit()`.
- Draw fade curves as semi-transparent overlays in `ClipComponent::paint()`:
  - Fade-in: triangle from 0 opacity at clip start to full at fade-in end.
  - Fade-out: triangle from full at fade-out start to 0 opacity at clip end.
- Cursor should change to diagonal resize when hovering fade zones.
- Coalesce undo transactions during drag (use `runCommandCoalesced` pattern).

3. Implement auto-save.
- Add `AutoSaveManager` class in `gui/src/edit/AutoSaveManager.h/.cpp`.
- Use a `juce::Timer` with configurable interval (default 120 seconds).
- On each tick, if `EditSession::isDirty()`, save to `<project_path>.autosave`.
- On startup, check for `.autosave` files and prompt "Recover unsaved changes?".
- On successful manual save, delete the `.autosave` file.
- Wire into `MainComponent` construction.

4. Add loop region visualization.
- In `TimeRulerComponent::paint()`, when loop is enabled (`transport.looping`), draw a colored bar across the loop region using `pal->primary` at 20% opacity.
- Draw loop start/end markers as small triangles on the ruler.
- Make the loop markers draggable to adjust loop bounds.
- Add loop region to `PlayheadComponent` overlay as a subtle tinted background.

5. Add track context menu.
- Right-click on `TrackLaneComponent` header shows context menu with:
  - "Rename Track" — focuses the name editor.
  - "Duplicate Track" — duplicates track with all clips.
  - "Delete Track" — deletes with confirmation if clips present.
  - "Move Up" / "Move Down" — reorders track in the edit.
- All operations undoable via `EditSession::performEdit()`.

6. Add tests.
- Test track deletion and undo.
- Test clip fade-in/out values after simulated drag.
- Test auto-save file creation and recovery detection.
- Test loop region bounds calculation.

## Files Expected To Change
- `gui/src/ui/ClipComponent.h`
- `gui/src/ui/ClipComponent.cpp`
- `gui/src/ui/TrackLaneComponent.h`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/ui/TimeRulerComponent.cpp`
- `gui/src/ui/PlayheadComponent.cpp`
- `gui/src/MainComponent.h`
- `gui/src/MainComponent.cpp`
- `gui/src/edit/AutoSaveManager.h` (NEW)
- `gui/src/edit/AutoSaveManager.cpp` (NEW)
- `gui/CMakeLists.txt`
- `tests/WaiveUiTests.cpp`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

Manual checks:
- Right-click track header → "Delete Track" → undo restores track.
- Drag fade handle on clip → fade curve renders correctly.
- Wait 2 minutes with unsaved changes → `.autosave` file exists.
- Enable loop → colored region visible on ruler and timeline.
- Right-click track → "Move Down" → track order changes.

## Exit Criteria
- Tracks can be deleted from UI with undo support.
- Clip fades have visual handles and render curves.
- Auto-save creates recovery files on schedule.
- Loop regions are visually represented.
- Track context menu works for delete/duplicate/reorder.
