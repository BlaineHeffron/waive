# Waive Development Conventions

## Safety Rules

**CRITICAL**: These rules are enforced to prevent crashes and data corruption in async tool workflows.

- **Never persist raw `te::Clip*` in member variables across async boundaries**. Edit swaps (new project, open project) invalidate all clip pointers. Use `te::EditItemID` for persistent selection state and resolve to clip pointers just-in-time via `edit.findClipForID()`.
- **Use `te::EditItemID` for persistent selection state**. Timeline `SelectionManager` and tool preview highlighting must use IDs, not pointers. IDs survive edit swaps; pointers do not.
- **Validate clip existence before dereferencing stored IDs**. After resolving `editItemID` to a clip pointer, check for `nullptr` before dereferencing. Clips can be deleted by undo/redo or other tools while a job is running.
- **No bare `this` capture in async lambdas**. Use `juce::Component::SafePointer<T>` or explicit validity flags when scheduling callbacks from background threads. Component destruction mid-job would cause a dangling pointer dereference.
- **All mutations must be exception-safe**. Use RAII transaction guards (`EditSession::performEdit()` handles this automatically). Never leave edit state partially mutated if a lambda throws.
- **Repaint requests must be throttled**. Use `juce::Timer` with 30–60 ms interval to batch repaints. Direct `repaint()` calls in tight loops or rapid parameter changes will stall the message thread. See `TimelineComponent::timerCallback()` and `MixerChannelStrip::timerCallback()` for reference patterns.

## Threading Rules

- **Message thread only** — All UI components, `EditSession`, `UndoableCommandHandler`,
  and `CommandHandler` are message-thread only.  Never call them from background threads.
- **Background work** — Use `waive::JobQueue::submit()`.  The job function runs on a pool
  thread; the `onComplete` callback and `JobQueue::Listener::jobEvent` are delivered on the
  message thread.
- **Progress coalescing** — `JobQueue` coalesces progress updates at ~10 Hz so the message
  thread is never flooded.

## Component Conventions

| Directory        | Contents                                              |
|------------------|-------------------------------------------------------|
| `gui/src/ui/`    | JUCE `Component` subclasses (UI panels, views)       |
| `gui/src/edit/`  | Edit-layer classes (`EditSession`, `UndoableCommandHandler`) |
| `gui/src/tools/` | Tool runtime/framework (`Tool*`, `JobQueue`, `ModelManager`) |
| `gui/src/util/`  | Shared helpers (`CommandHelpers`)                     |
| `gui/src/theme/` | Centralized theme (`WaiveColours`, `WaiveFonts`, `WaiveLookAndFeel`) |

## Naming

- Classes: `PascalCase`
- Files: match class name (`SessionComponent.h/.cpp`)
- Namespace helpers: `waive::` namespace for free functions
- Member variables: `camelCase`, no prefix

## Theming

- **Never use hardcoded colours** in component paint methods. Use `getWaivePalette(component)` to access the semantic palette (defined in `WaiveColours.h`).
- Use palette roles semantically: `clipDefault` for clip fills, `waveform` for waveform strokes, `primary` for active/accent elements, etc.
- Always provide a fallback colour with `getWaivePalette()` since it may return `nullptr` in test contexts without a global LookAndFeel.
- Use `waive::Fonts` (from `WaiveFonts.h`) for consistent font sizing across components.
- Custom draw overrides live in `WaiveLookAndFeel.cpp` — prefer extending the LnF over per-component paint hacks.

## Edit Layer Rules

- **All mutations** go through `UndoableCommandHandler`, never call `CommandHandler` directly
  from UI code.
- **Slider coalescing** — Use `runCommandCoalesced()` for continuous controls (volume, pan)
  so a full drag produces a single undo entry.
- **Read-only commands** (ping, get_tracks, get_edit_state, list_plugins, transport_*)
  pass through without undo wrapping.
- **engine/src/ is shared** — `CommandHandler.h/.cpp` must not depend on GUI-layer types.
  The `UndoableCommandHandler` wrapper is the GUI-only layer that adds undo.

### Shared Tool Utilities

- **AudioAnalysis functions** live in `gui/src/tools/AudioAnalysis.h`. Use `waive::analyseAudioFile()` for peak/RMS/transient detection. Pass an `AudioAnalysisCache*` pointer to deduplicate repeated analyses of the same file with the same parameters.
- **ClipTrackIndexMap utility** in `gui/src/tools/ClipTrackIndexMap.h`. Use `waive::buildClipTrackIndexMap(edit)` to precompute O(1) clip-to-track index lookups before multi-clip iteration. Avoids nested track/clip loops.
- **Always use `AudioAnalysisCache` when calling `analyseAudioFile()` repeatedly**. Tools like `normalize_selected_clips`, `gain_stage_selected_tracks`, and `detect_silence_and_cut_regions` analyze multiple clips. Cache hits avoid redundant file I/O and DSP.

## CMake

- All new source files must be listed in `gui/CMakeLists.txt` under `target_sources`.
- Theme files live under `gui/src/theme/` and are included in the build via `gui/CMakeLists.txt`.
- Include directories for subdirectories are set via `target_include_directories` so
  `#include "Foo.h"` works without path prefixes.

## Testing

- C++ regression tests are integrated via CTest under `tests/`.
- UI tests must be runnable without human interaction.
- See `docs/testing.md` for test targets, run commands, and authoring guidelines.
