# Waive Test Framework

Waive has two native C++ regression test executables integrated with CTest.

## Test Targets

- `WaiveCoreTests`
  - Scope: edit-layer and clip-edit behavior without full UI composition.
  - File: `tests/WaiveCoreTests.cpp`
  - Current coverage:
    - coalesced undo transaction behavior
    - clip duplication preserving MIDI data
    - `EditSession::performEdit` exception handling

- `WaiveUiTests`
  - Scope: no-user UI automation by instantiating real components and invoking commands programmatically.
  - File: `tests/WaiveUiTests.cpp`
  - Current coverage:
    - `MainComponent` command routing for `Duplicate`, `Split`, `Delete`, `Undo`, `Redo`
    - timeline selection + command execution without manual input
    - Phase 1/2 library + plugins/routing coverage:
      - library double-click audio import at current transport position, with undo/redo
      - plugin chain operations via `PluginBrowserComponent` no-user helpers:
        - built-in plugin insert/remove/reorder
        - bypass toggle and plugin editor open/close calls
      - input workflow on selected track:
        - assign first available audio input, arm, monitor-on, clear input
        - transport record start/stop state validation under assigned+armed input
      - routing workflow:
        - per-track aux send gain creation/update
        - master aux-return + reverb return creation (idempotent on repeated apply)
    - project lifecycle flow without dialogs:
      - `openProject(file)` load + edit swap refresh
      - command-routed `Save` and `New`
      - dirty-state clearing on save
      - reopen persistence and recent-files updates
    - Phase 3 automation/time/transport coverage:
      - tempo and time-signature control updates + marker insertion at playhead
      - bars/beats grid snap behavior via `SessionComponent`/`TimelineComponent` test helpers (including non-`x/4` bar snap like `6/8`)
      - automation point add/move on track plugin parameter curves, with undo/redo validation
      - loop range + loop enable and punch-in/out state toggles
    - Phase 4 tool framework coverage:
      - `ToolSidebarComponent` no-user workflow for built-in `normalize_selected_clips`:
        - async `plan` generation with structured preview diff
        - reject path (no mutation)
        - apply path (undo/redo safe clip gain mutation)
      - preview highlighting of affected timeline clips and mixer tracks
      - plan artifact persistence to a project-local cache location
      - cancellable long-running tool-plan job leaving session state unchanged
    - Phase 5 built-in tools coverage:
      - `rename_tracks_from_clips`: selected-clip-driven rename apply + undo/redo
      - `gain_stage_selected_tracks`: track fader adjustment from selected-clip peak analysis + undo/redo
      - `detect_silence_and_cut_regions`: leading/trailing silence trim apply + undo/redo
      - `align_clips_by_transient`: multi-clip transient alignment apply + undo/redo
      - phase-5 plan artifact generation for built-in tools
    - Phase 5B model-backed tools coverage:
      - `ModelManager` lifecycle:
        - explicit storage-directory selection in tests (isolated per run)
        - quota enforcement (install fails when quota is too low)
        - install + pin + uninstall flows for model versions
      - `stem_separation`:
        - fails when required model is not installed
        - plan/apply produces stem tracks + wave clips from generated artifacts
        - undo/redo restores/removes generated stem tracks deterministically
      - `auto_mix_suggestions`:
        - requires installed model version
        - plan/apply produces track volume/pan suggestions
        - undo/redo restores suggested mix changes deterministically

## Phase Validation Matrix

`WaiveUiTests` currently validates phased plan milestones end-to-end:

- Phase 0/1/2 foundations and DAW workflow basics:
  - command routing, timeline edit actions, library import, plugin/routing/input workflow coverage, and session lifecycle persistence
- Phase 3:
  - tempo/time-signature/grid snap, automation add/move with undo/redo, loop/punch state
- Phase 4:
  - Tool API plan/apply/reject/cancel behavior, preview highlighting, artifact persistence
- Phase 5A:
  - deterministic built-in assistant tools (rename/gain-stage/silence-cut/transient-align)
- Phase 5B:
  - optional model manager + model-backed tools (stem separation, auto-mix suggestions)
- Phase 6:
  - Safety architecture regression: ID-based selection persistence across edit swaps, async callback safety (no dangling pointers), transaction rollback on exception
  - Performance regression: ClipTrackIndexMap scaling to 100+ clips, AudioAnalysisCache hit rate and eviction behavior

## Running Tests

From repo root:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run one suite:

```bash
ctest --test-dir build -R WaiveCoreTests --output-on-failure
ctest --test-dir build -R WaiveUiTests --output-on-failure
```

Run test binaries directly:

```bash
./build/tests/WaiveCoreTests_artefacts/Release/WaiveCoreTests
./build/tests/WaiveUiTests_artefacts/Release/WaiveUiTests
```

## CI

Tests run automatically on every push and PR via GitHub Actions (`.github/workflows/ci.yml`). The CI pipeline uses `xvfb-run` to provide a virtual display for JUCE's `ScopedJuceInitialiser_GUI`. See `docs/architecture.md` for pipeline details.

## Writing New Tests

- Prefer deterministic tests that create/modify an in-memory `Edit`.
- For UI tests, use command invocation and direct model assertions rather than screenshot/image checks.
- Prefer `SessionComponent`/`TimelineComponent` deterministic helper methods when asserting UI-driven state transitions (tempo/time-signature, snap, automation, loop/punch).
- For plugin/routing/record-input workflow coverage, use `PluginBrowserComponent` no-user helpers:
  `selectTrackForTesting`, `insertBuiltInPluginForTesting`, `selectChainRowForTesting`,
  `moveSelectedChainPluginForTesting`, `removeSelectedChainPluginForTesting`,
  `toggleSelectedChainPluginBypassForTesting`, `openSelectedChainPluginEditorForTesting`,
  `closeSelectedChainPluginEditorForTesting`, `selectFirstAvailableInputForTesting`,
  `setArmEnabledForTesting`, `setMonitorEnabledForTesting`, `setSendLevelDbForTesting`,
  `ensureReverbReturnOnMasterForTesting`.
- For tool framework tests, use `ToolSidebarComponent` test helpers (`runPlanForTesting`, `waitForIdleForTesting`, `applyPlanForTesting`, `cancelPlanForTesting`) to validate plan/apply/cancel flows without manual input.
- **Testing async-callback safety**: Prefer `EditItemID`-based selection over raw pointers in test assertions. Verify that edit swaps (new project, open project) clear selection and don't cause crashes. Test job cancellation paths explicitly to ensure no dangling callbacks fire after cancellation.
- Keep tests message-thread safe; use `juce::ScopedJuceInitialiser_GUI` for UI-oriented tests.
- Avoid requiring interactive dialogs or manual file selection.
