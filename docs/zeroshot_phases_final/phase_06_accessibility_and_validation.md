# Phase 06: Accessibility and Validation

## Objective
Add basic accessibility support, expand test coverage for all new features from phases 1-5, update documentation, and validate CI stability.

## Scope
- Accessibility labels on interactive components.
- Keyboard navigation (Tab order, arrow keys in lists).
- Test expansion for security, UX, features, performance, and polish changes.
- Documentation updates reflecting new architecture.
- CI validation with clean build.

## Implementation Tasks

1. Add accessibility labels.
- Set `setTitle()` and `setDescription()` on all transport buttons, mixer controls, and tool sidebar controls.
- Examples:
  - `playButton.setTitle("Play"); playButton.setDescription("Start playback (Space)");`
  - `faderSlider.setTitle("Volume"); faderSlider.setDescription("Track volume in dB");`
  - `soloButton.setTitle("Solo"); soloButton.setDescription("Solo this track (S)");`
- Set accessible names on track lanes, clip components, and timeline.

2. Implement keyboard navigation.
- Set `setWantsKeyboardFocus(true)` on all interactive components.
- Define a Tab order: transport buttons → track lanes → mixer strips → tool sidebar.
- In `MixerComponent`, add arrow key navigation between channel strips.
- In `PluginBrowserComponent`, add arrow key navigation in the plugin list.
- In `LibraryComponent`, add arrow key navigation in the file list.

3. Expand regression tests for phases 1-5.
- Phase 1 (Security): Test path sanitization rejects `../`, `..\\`, null bytes. Test command server rejects unauthenticated connections.
- Phase 2 (UX): Test tooltip presence on transport buttons. Test empty state text rendering.
- Phase 3 (Features): Test track delete + undo. Test clip fade value changes. Test auto-save file creation.
- Phase 4 (Performance): Test AudioAnalysisCache LRU eviction at max capacity. Test previewClipIDs set operations.
- Phase 5 (Polish): Test track color determinism. Test scrollbar range calculation.

4. Update architecture documentation.
- Add security architecture section to `docs/architecture.md`:
  - Path sanitization utility and usage.
  - Command server authentication flow.
  - File path validation in command handler.
- Add new feature architecture:
  - Auto-save manager design.
  - Clip fade handle interaction model.
  - Track color assignment system.

5. Update development conventions.
- Add to `docs/dev_conventions.md`:
  - "All file path components from external input must pass through `sanitizePathComponent()`."
  - "Every interactive component must have a tooltip via `setTooltip()`."
  - "Every component must call `setTitle()` for accessibility."
  - "Use `waive::Spacing` constants instead of magic numbers for padding/margins."
  - "Timer discipline: prefer single consolidated timer per parent container."

6. Validate CI from clean build.
```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

7. Update testing documentation.
- Add new test categories to `docs/testing.md`:
  - Security regression tests.
  - Accessibility label verification tests.
  - Performance boundary tests (cache limits, timer counts).

## Files Expected To Change
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/ui/MixerComponent.cpp`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/PluginBrowserComponent.cpp`
- `gui/src/ui/LibraryComponent.cpp`
- `gui/src/ui/ToolSidebarComponent.cpp`
- `gui/src/ui/TimelineComponent.cpp`
- `gui/src/ui/ClipComponent.cpp`
- `gui/src/ui/TrackLaneComponent.cpp`
- `tests/WaiveUiTests.cpp`
- `tests/WaiveCoreTests.cpp`
- `docs/architecture.md`
- `docs/dev_conventions.md`
- `docs/testing.md`

## Validation

```bash
cmake --build build --target Waive WaiveUiTests WaiveCoreTests -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

Manual checks:
- Tab through UI — focus indicator moves through all interactive controls.
- Screen reader (if available) reads button labels correctly.
- All new tests pass.
- Docs accurately reflect current architecture.

## Exit Criteria
- All interactive components have accessibility titles.
- Tab navigation works through major UI sections.
- Test suite covers all changes from phases 1-5.
- Documentation is current and accurate.
- Clean build from scratch passes all tests.
