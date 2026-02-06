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
    - project lifecycle flow without dialogs:
      - `openProject(file)` load + edit swap refresh
      - command-routed `Save` and `New`
      - dirty-state clearing on save
      - reopen persistence and recent-files updates
    - Phase 3 automation/time/transport coverage:
      - tempo and time-signature control updates + marker insertion at playhead
      - bars/beats grid snap behavior via `SessionComponent`/`TimelineComponent` test helpers
      - automation point add/move on track plugin parameter curves
      - loop range + loop enable and punch-in/out state toggles

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

## Writing New Tests

- Prefer deterministic tests that create/modify an in-memory `Edit`.
- For UI tests, use command invocation and direct model assertions rather than screenshot/image checks.
- Prefer `SessionComponent`/`TimelineComponent` deterministic helper methods when asserting UI-driven state transitions (tempo/time-signature, snap, automation, loop/punch).
- Keep tests message-thread safe; use `juce::ScopedJuceInitialiser_GUI` for UI-oriented tests.
- Avoid requiring interactive dialogs or manual file selection.

## Existing Python Tests

The `ai/tests/` Python suite is still available for the legacy AI tooling path and can be run separately with `pytest`.
