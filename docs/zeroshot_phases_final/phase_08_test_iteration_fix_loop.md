# Phase 08: Test Iteration & Fix Loop

## Objective
Run ALL tests (C++ and Python), identify every failure, fix the root causes, and iterate until the full suite is green. This is the final quality gate — no new features, only fixes to make existing code pass its tests. The phase is complete when `ctest` and `pytest` both report 0 failures.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt` or `tests/CMakeLists.txt`.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Context

### Test Suites To Pass

**C++ tests (via ctest):**
1. `WaiveCoreTests` — EditSession, ClipEditActions, ModelManager, PathSanitizer, AudioAnalysisCache
2. `WaiveUiTests` — All UI components, tool framework, plugin routing, transport
3. `WaiveToolTests` — Audio analysis engine + built-in tool logic

**Python tests (via pytest):**
4. `tools/tests/test_beat_detection.py`
5. `tools/tests/test_key_detection.py`
6. `tools/tests/test_chord_detection.py`
7. `tools/tests/test_audio_to_midi.py`
8. `tools/tests/test_music_generation.py`
9. `tools/tests/test_timbre_transfer.py`
10. `tools/tests/test_phase12_tools.py`

### Known Issues Going In
- **New features from phases 01-07** may have introduced build or test regressions
- **New UI components** (PianoRollComponent, RenderDialog, PluginPresetBrowser) may not compile cleanly with existing test infrastructure
- **New commands** (folder tracks, presets, collect/save) may need test coverage additions
- **WaiveUiTests** uses RUN_TEST_SAFELY macro — any new UI test functions need to be registered in main()

## Implementation Tasks

### 1. Build ALL test targets

```bash
cmake -B build
cmake --build build -j$(($(nproc)/2))
```

**Fix any build/linker errors first.** Common issues:
- Missing source files in `target_sources()` — add them
- Missing `target_include_directories()` — add the directory
- Undefined symbols from new headers — add the `.cpp` file to the test target sources
- Forward declaration issues with new types

### 2. Run C++ tests and collect failures

```bash
ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/ctest_results.txt
```

For each failing test:
1. Read the error message
2. Identify root cause (crash, assertion, wrong value, missing feature)
3. Fix the source code
4. Rebuild and re-run only that test:
   ```bash
   cmake --build build --target <TestTarget> -j$(($(nproc)/2))
   ./build/tests/<TestTarget>_artefacts/<TestTarget>
   ```

### 3. Run Python tests and collect failures

```bash
pip install -r tools/tests/requirements.txt 2>/dev/null || true
for req in tools/*/requirements.txt; do pip install -r "$req" 2>/dev/null || true; done
python3 -m pytest tools/tests/ -v --tb=short 2>&1 | tee /tmp/pytest_results.txt
```

### 4. Fix common issues

**Build errors from new phases:**
- New files added to `gui/CMakeLists.txt` but not to test targets that include GUI headers
- New command handler methods need to be available in tests that link CommandHandler

**WaiveUiTests additions:**
- If phases 01-07 added new UI features, the existing WaiveUiTests should still pass
- If new test functions were added, ensure they're registered in main() with RUN_TEST_SAFELY

**Python test regressions:**
- Tool scripts may have been modified — re-run tests to catch regressions
- New tools added in earlier phases may need test stubs

### 5. Iterate until green

Repeat steps 2-4 until:
```bash
ctest --test-dir build --output-on-failure
# → 100% tests passed, 0 tests failed

python3 -m pytest tools/tests/ -v --tb=short
# → all passed (or all non-skipped passed)
```

### 6. Final verification

```bash
# Full rebuild from clean
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))

# All C++ tests
ctest --test-dir build --output-on-failure

# All Python tests
python3 -m pytest tools/tests/ -v --tb=short

# Verify the app binary exists
ls -la build/gui/Waive_artefacts/Release/Waive
```

## Fixing Guidelines

### DO:
- Fix the root cause, not the symptom
- Adjust test tolerances when the algorithm is fundamentally correct but precision differs
- Add try/catch around crashing C++ tests to isolate failures
- Fix off-by-one errors in sample/frame indexing
- Fix field name mismatches between test expectations and actual output

### DO NOT:
- Delete or skip tests without a documented reason
- Change test thresholds to unreasonably loose values just to pass
- Add new features — this phase is fixes only
- Remove any existing source files from CMakeLists.txt
- Use `-j$(nproc)` for builds

## Validation

```bash
cmake -B build \
  && cmake --build build --target Waive -j$(($(nproc)/2)) \
  && ctest --test-dir build --output-on-failure \
  && python3 -m pytest tools/tests/ -v --tb=short
```

## Exit Criteria
- **Zero C++ test failures**: `ctest` reports `100% tests passed`.
- **Zero Python test failures**: `pytest` reports all passed (skips for optional deps are OK).
- **Build clean**: `cmake --build build --target Waive` compiles with no errors.
- **No regressions**: All previously-passing tests still pass.
