# Phase 01: Critical Correctness And Safety

## Objective
Eliminate crash/data-corruption risks and resolve the highest-impact correctness gaps before adding new functionality.

## Consolidated Inputs
- `phase_01_correctness_and_safety.md`
- `phase_06_critical_safety_fixes.md`

## Scope
- Memory safety in selection and async UI callbacks.
- Edit transaction integrity.
- Tool preview correctness.
- Sidebar resizing correctness.
- Job worker exception safety.
- Defensive bounds in grid generation.
- Deprecated API cleanup.

## Implementation Tasks

1. Replace raw clip-pointer selection storage with ID-based storage.
- Update `SelectionManager` to store `te::EditItemID`, not `te::Clip*`.
- Resolve IDs to live clips on demand via `te::findClipForID`.
- Drop stale IDs silently.

2. Separate tool preview highlighting from real user selection.
- Stop using real selection mutation for preview flows.
- Add preview-only highlight channels for timeline and mixer.
- Ensure reject/cancel/apply clears preview-only state without destroying user selection.

3. Fix async use-after-free in `ClipComponent` context menu callbacks.
- Replace raw `this` capture in async menu callbacks with `juce::Component::SafePointer`.
- Capture clip ID and resolve clip at callback execution time.

4. Enforce edit rollback on exceptions.
- In `EditSession::performEdit`, rollback the current transaction on any exception path.
- Ensure partial edits cannot survive after failed mutation lambdas.

5. Add guardrails to grid-line generation loops.
- Bound maximum generated lines per pass to prevent pathological/hung loops under corrupt tempo data.

6. Make the tool sidebar resizer actually functional.
- Remove fixed-width-only behavior.
- Wire layout through a real horizontal stretch/resizer model.
- Keep width stable across hide/show in-session.

7. Harden background job failure handling.
- Add `catch (...)` in job workers in addition to `std::exception`.
- Emit `Failed` status and meaningful fallback message.

8. Remove deprecated JUCE writer API usage in Waive-owned code.
- Migrate to the current writer-options path in app and test fixture code.

## Files Expected To Change
- `gui/src/ui/SelectionManager.h`
- `gui/src/ui/SelectionManager.cpp`
- `gui/src/ui/TimelineComponent.h`
- `gui/src/ui/TimelineComponent.cpp`
- `gui/src/ui/SessionComponent.h`
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/ui/ClipComponent.cpp`
- `gui/src/edit/EditSession.cpp`
- `gui/src/tools/JobQueue.cpp`
- `gui/src/tools/StemSeparationTool.cpp`
- `tests/WaiveUiTests.cpp`
- `tests/WaiveCoreTests.cpp`

## Validation

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- No raw `Clip*` selection state persists across delete/rebuild cycles.
- Preview/reject/cancel/apply no longer destroy user selection.
- No async menu callback can dereference destroyed component state.
- Failed edits are rolled back atomically.
- Grid-line computation cannot hang.
- Sidebar width is user-resizable.
- Job failures are contained and reported.
- No deprecated writer calls remain in Waive-owned code.

