# Phase 20: Test Iteration & Fix Loop

## Objective
Run ALL tests (C++ and Python), identify every failure, fix the root causes, and iterate until the full suite is green. This is the final quality gate — no new features, only fixes to make existing code pass its tests. The phase is complete when `ctest` and `pytest` both report 0 failures.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt` or `tests/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Context

### Test Suites To Pass
After phases 17-19, the test landscape is:

**C++ tests (via ctest):**
1. `WaiveCoreTests` — EditSession, ClipEditActions, ModelManager, PathSanitizer, AudioAnalysisCache (11 tests)
2. `WaiveUiTests` — UI components, tool framework, plugin routing, transport (13 suites; previously crashes)
3. `WaiveCommandTests` — CommandHandler commands (added in phase 17)
4. `WaiveToolSchemaTests` — AI tool schema generation (added in phase 17)
5. `WaiveExternalToolTests` — External tool manifest parsing (added in phase 17)
6. `WaiveChatSerializerTests` — Chat history serialization (added in phase 17)
7. `WaiveToolTests` — Audio analysis engine + built-in tool logic (added in phase 19)

**Python tests (via pytest):**
8. `tools/tests/test_tool_manifests.py` — Validates all `.waive-tool.json` manifests (added in phase 17)
9. `tools/tests/test_beat_detection.py` — Beat detection accuracy (added in phase 18)
10. `tools/tests/test_key_detection.py` — Key detection accuracy (added in phase 18)
11. `tools/tests/test_chord_detection.py` — Chord detection accuracy (added in phase 18)
12. `tools/tests/test_audio_to_midi.py` — Audio-to-MIDI transcription (added in phase 18)
13. `tools/tests/test_music_generation.py` — Music generation output validation (added in phase 18)
14. `tools/tests/test_timbre_transfer.py` — Timbre transfer output validation (added in phase 18)
15. `tools/tests/test_phase12_tools.py` — Noise reduction, mastering, auto EQ, pitch correction (added in phase 18)

### Known Issues Going In
- **WaiveUiTests** crashes mid-execution (phase 17 adds try/catch isolation)
- **CI Python tests** silently skip (phase 17 fixes paths)
- **New C++ test executables** may have linker errors from missing transitive dependencies
- **Audio tool tests** may reveal algorithmic accuracy issues that need tuning
- **Phase 12 tools** may not exist yet — their tests should skip gracefully

## Implementation Tasks

### 1. Build ALL test targets

```bash
cmake -B build
cmake --build build --target WaiveCoreTests WaiveUiTests WaiveToolTests -j$(($(nproc)/2))
```

If there are additional test targets (WaiveCommandTests, WaiveToolSchemaTests, WaiveExternalToolTests, WaiveChatSerializerTests), build those too:
```bash
cmake --build build --target WaiveCommandTests WaiveToolSchemaTests WaiveExternalToolTests WaiveChatSerializerTests -j$(($(nproc)/2))
```

**Fix any build/linker errors first.** Common issues:
- Missing source files in `target_sources()` — add them
- Missing `target_include_directories()` — add the directory
- Undefined symbols from transitively included headers — add the `.cpp` file to sources
- Header-only deps (like ToolDiff.h) that include other headers needing their `.cpp`

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
# Install dependencies
pip install -r tools/tests/requirements.txt 2>/dev/null || true
for req in tools/*/requirements.txt; do pip install -r "$req" 2>/dev/null || true; done

# Run all tests
python3 -m pytest tools/tests/ -v --tb=short 2>&1 | tee /tmp/pytest_results.txt
```

For each failing test:
1. Read the error and traceback
2. Common Python test failures and fixes:
   - **ImportError for mir_eval/pyloudnorm** — Add `pytest.mark.skipif` guard if not already present
   - **Tool script not found** — Check TOOL_SCRIPT path is correct relative to test file
   - **Beat detection BPM too far off** — Tune the 5% tolerance or fix the onset detection algorithm
   - **Key detection wrong key** — Check if synthetic chord generates enough harmonic content (extend duration, increase amplitude)
   - **Chord detection doesn't detect changes** — Increase segment duration or decrease hop_seconds
   - **Audio-to-MIDI wrong note** — Check YIN threshold or allow wider MIDI tolerance
   - **Music generation output too short** — Increase tolerance or check fallback synthesis duration calculation
   - **Timbre transfer pitch not preserved** — Widen cents tolerance for fallback scipy mode
   - **result.json missing fields** — Tool may use different field names than expected; align test with actual output
   - **subprocess.TimeoutExpired** — Increase timeout in test
3. Fix the test assertion, tolerance, or tool algorithm as needed
4. Re-run the specific test:
   ```bash
   python3 -m pytest tools/tests/test_beat_detection.py -v --tb=long
   ```

### 4. Fix common C++ test issues

**WaiveUiTests crash:**
- Phase 17 adds try/catch isolation. If it still crashes (segfault), wrap individual test calls in a signal handler or comment out the offending test with a `// FIXME` and issue number.
- Common segfault causes: MessageManager not initialized, accessing destroyed component pointers, timer callbacks during teardown.

**WaiveToolTests linker errors:**
- Tools include headers that reference `SessionComponent`, `TimelineComponent`, `SelectionManager`. Either:
  a. Add those source files to `WaiveToolTests` target_sources, OR
  b. Restructure the test to avoid needing those symbols (test analysis layer only)
- If `ToolRegistry` constructor instantiates tools that reference UI types, you may need to add UI sources or test registration differently.

**WaiveCommandTests linker errors:**
- CommandHandler is in `engine/src/`. Make sure the test includes `CommandHandler.h/cpp` and all its deps.
- CommandHandler methods may reference Tracktion API that requires certain engine state.

### 5. Fix algorithmic accuracy issues

If audio tools produce wrong results (not just tolerance issues), fix the algorithm:

**Beat detection (onset method):**
- If BPM doubles/halves, the `while bpm < 60: bpm *= 2` loop may need adjustment
- If beat positions are offset, check `hop_length` and peak picking `min_interval`

**Key detection (Krumhansl-Schmuckler):**
- If wrong mode (major vs minor), check profile correlation tie-breaking
- If wrong root, check frequency → chroma bin mapping in `compute_chroma_summary()`

**Audio-to-MIDI (YIN):**
- If notes are wrong, check YIN threshold (0.15) — may need to be 0.1 or 0.2
- If notes are missing, check RMS threshold and `min_note_frames`

### 6. Iterate until green

Repeat steps 2-5 until:
```bash
ctest --test-dir build --output-on-failure
# → 100% tests passed, 0 tests failed

python3 -m pytest tools/tests/ -v --tb=short
# → all passed (or all non-skipped passed)
```

### 7. Final verification

Run the complete validation sequence:
```bash
# Full rebuild
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
- Adjust test tolerances when the algorithm is fundamentally correct but precision differs from expectations
- Add `pytest.mark.skipif` guards for optional dependencies (mir_eval, pyloudnorm)
- Add try/catch around crashing C++ tests to isolate failures
- Fix off-by-one errors in sample/frame indexing
- Fix field name mismatches between test expectations and actual tool output JSON

### DO NOT:
- Delete or skip tests without a documented reason
- Change test thresholds to unreasonably loose values just to pass
- Modify the tool's core algorithm to pass a single edge case if it breaks normal cases
- Add new features — this phase is fixes only
- Remove any existing source files from CMakeLists.txt
- Use `-j$(nproc)` for builds

## Files Expected To Change
- `tests/WaiveToolTests.cpp` (fixes for linker/compilation issues)
- `tests/CMakeLists.txt` (add missing source files to targets)
- `tests/WaiveCoreTests.cpp` (if any existing tests need isolation)
- `tests/WaiveUiTests.cpp` (crash fixes, try/catch isolation)
- `tools/tests/*.py` (tolerance adjustments, field name fixes, import guards)
- `tools/tests/requirements.txt` (if missing deps)
- `tools/*/` Python tool scripts (algorithmic fixes if tests reveal real bugs)
- `gui/src/tools/*.cpp` (fixes if C++ tool tests reveal real bugs)
- `engine/src/CommandHandler.cpp` (fixes if command tests reveal real bugs)
- `.github/workflows/ci.yml` (ensure all test targets are built and run)

## Validation

```bash
# The single command that must succeed:
cmake -B build \
  && cmake --build build --target Waive -j$(($(nproc)/2)) \
  && ctest --test-dir build --output-on-failure \
  && python3 -m pytest tools/tests/ -v --tb=short
```

## Exit Criteria
- **Zero C++ test failures**: `ctest --test-dir build --output-on-failure` reports `100% tests passed`.
- **Zero Python test failures**: `pytest tools/tests/ -v` reports all passed (skips for optional deps are OK).
- **Build clean**: `cmake --build build --target Waive` compiles with no errors.
- **No regressions**: All previously-passing tests still pass.
- **CI compatible**: Tests can run in the CI environment (Ubuntu 22.04, xvfb-run, Python 3.x).
