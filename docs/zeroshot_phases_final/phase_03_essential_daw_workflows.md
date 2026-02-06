# Phase 03: Essential DAW Workflows

## Objective
Add missing baseline DAW workflows required for practical daily use.

## Consolidated Inputs
- `phase_07_essential_daw_workflows.md`
- Relevant command ergonomics item from `phase_03_uiux_and_design_polish.md`

## Scope
- Core transport shortcuts.
- Solo/mute and track rename workflows.
- Audio device settings access.
- Render/export workflow.
- Metronome toggle.

## Implementation Tasks

1. Add transport command set and menu coverage.
- `Play/Stop` on `Space`.
- Record toggle command.
- Go-to-start command.
- Add a dedicated Transport menu.
- Rebind split away from bare `s` to avoid accidental triggers and text-entry conflicts.

2. Add solo/mute controls to mixer strips.
- Add per-track solo and mute toggle buttons.
- Sync visual/control state with engine state.
- Use undo-safe edit mutations.

3. Add inline track rename UX.
- Enable double-click rename for mixer strip name label.
- Enable double-click rename for timeline track header label.
- Apply via undo-safe transaction.

4. Add audio settings access from UI.
- Add command and menu item to open device settings dialog.
- Ensure changes apply through engine/device manager correctly.

5. Add render/export-to-file command.
- Add File menu command for render.
- Provide save-target selection.
- Execute render with progress/status reporting and success/failure surface.

6. Add metronome toggle in session transport controls.
- Add click toggle control wired to edit click/metronome state.

7. Add automated coverage.
- Command routing tests for new transport commands.
- Mixer solo/mute tests.
- Rename workflow tests.
- Metronome state tests.
- Render command test where deterministic/non-interactive execution is feasible.

## Files Expected To Change
- `gui/src/MainComponent.h`
- `gui/src/MainComponent.cpp`
- `gui/src/ui/SessionComponent.h`
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/ui/MixerChannelStrip.h`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/TrackLaneComponent.cpp`
- `tests/WaiveUiTests.cpp`

## Validation

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- Transport shortcuts are functional and conflict-safe.
- Solo/mute and rename are fully usable in UI.
- Audio settings are accessible.
- Render/export produces valid output.
- Metronome toggle works reliably.
- Tests pass.

