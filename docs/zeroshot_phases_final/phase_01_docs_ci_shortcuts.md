# Phase 01: Documentation, CI Hardening & Shortcut Fix

## Objective
Create user-facing documentation (README, LICENSE), fix a critical keyboard shortcut conflict, make Python tests blocking in CI, and add a keyboard shortcut reference to the Help menu.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Tracktion Engine namespace is `namespace te = tracktion;`

## Context

### Current State
- All 3 C++ test suites pass (WaiveCoreTests, WaiveUiTests, WaiveToolTests)
- All Python tests pass (48 passed, 4 skipped for music_generation needing a model)
- Build is clean with no warnings
- No README.md at project root
- No LICENSE file
- `Cmd+S` is mapped to **both** "Save" and "Split at Playhead" (MainComponent.cpp:259,278) — conflict
- Python tests in CI are `continue-on-error: true` (advisory only, don't block merges)

### Keyboard Shortcuts (current)
Defined in `gui/src/MainComponent.cpp` lines 224-318 via `getCommandInfo()`:
| Command | Shortcut |
|---------|----------|
| Undo | Cmd+Z |
| Redo | Cmd+Shift+Z |
| New | Cmd+N |
| Open | Cmd+O |
| Save | Cmd+S |
| Save As | Cmd+Shift+S |
| Delete | Delete |
| Duplicate | Cmd+D |
| Split at Playhead | Cmd+S (CONFLICT!) |
| Delete Track | Cmd+Backspace |
| Toggle Tool Sidebar | Cmd+T |
| AI Chat | Cmd+Shift+C |
| Play/Stop | Space |
| Record | R |
| Go to Start | Home |

## Implementation Tasks

### 1. Create README.md at project root

Create `/home/blaine/projects/waive/README.md` with:
- Project name and one-line description ("AI-assisted DAW built with JUCE and Tracktion Engine")
- Feature highlights (48 commands, 7 built-in tools, 10 Python AI tools, multi-provider AI chat)
- Screenshot placeholder (just a text note, no actual image)
- Build instructions (cmake, dependencies for Ubuntu)
- Run instructions
- Test instructions (ctest + pytest)
- Keyboard shortcuts table
- Architecture overview (link to docs/architecture.md)
- License section

### 2. Add LICENSE file

Create `/home/blaine/projects/waive/LICENSE` with the MIT license. Use "Waive Contributors" as the copyright holder and 2026 as the year.

### 3. Fix Cmd+S shortcut conflict

In `gui/src/MainComponent.cpp`, the `cmdSplit` case (line 276-279) uses:
```cpp
result.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier);
```
This conflicts with Save. Change Split to `Cmd+E` (standard in many DAWs for "Edit at playhead"):
```cpp
result.addDefaultKeypress ('e', juce::ModifierKeys::commandModifier);
```

### 4. Make Python tests blocking in CI

In `.github/workflows/ci.yml`, the `python-tests` job (line 72) has:
```yaml
continue-on-error: true
```
Remove this line so Python test failures block merges.

Also fix the `hashFiles` condition syntax — it should use `hashFiles(...)` properly in the `if` conditions.

### 5. Add keyboard shortcut reference to Help menu

Create a simple dialog that shows all keyboard shortcuts. In `gui/src/MainComponent.cpp`:

1. Add a new command ID `cmdShowShortcuts` to the enum in `MainComponent.h`
2. Register it in `getAllCommands()`
3. In `getCommandInfo()`, set info with `addDefaultKeypress ('/', juce::ModifierKeys::commandModifier)` (Cmd+/ to show shortcuts)
4. In `perform()`, show a `juce::AlertWindow` with a formatted string of all shortcuts
5. Add "Keyboard Shortcuts..." to the Help menu in `getMenuBarNames()` / `getMenuForIndex()`

The dialog content should be a simple multi-line string listing all shortcuts in a readable format.

## Files Expected To Change
- `README.md` (create)
- `LICENSE` (create)
- `gui/src/MainComponent.h` (add cmdShowShortcuts)
- `gui/src/MainComponent.cpp` (fix Split shortcut, add shortcuts dialog, add Help menu)
- `.github/workflows/ci.yml` (remove continue-on-error)

## Validation

```bash
# Build must succeed
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))

# All tests must still pass
ctest --test-dir build --output-on-failure

# Files must exist
test -f README.md && test -f LICENSE

# Verify no duplicate Cmd+S binding (search for 's', commandModifier)
grep -n "addDefaultKeypress.*'s'.*commandModifier" gui/src/MainComponent.cpp
# Should show exactly 2 results: Save and Save As (not 3)
```

## Exit Criteria
- README.md exists with build instructions and feature list
- LICENSE file exists (MIT)
- Split at Playhead uses Cmd+E (no Cmd+S conflict)
- Python tests blocking in CI (no continue-on-error)
- Cmd+/ opens keyboard shortcuts reference
- All existing tests still pass
