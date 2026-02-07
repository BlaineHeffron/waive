# Phase 04: Keyboard Navigation and Accessibility

## Objective
Complete keyboard navigation, fix tab order, add screen reader announcements, and finalize test coverage for all audit findings.

## Scope
- Fix broken tab order (was commented out due to segfault).
- Implement arrow key navigation in MixerComponent, PluginBrowserComponent, LibraryComponent.
- Add screen reader announcements for dynamic state changes.
- Add SchemaFormComponent accessibility labels.
- Expand test coverage for all correctness and performance fixes from phases 1-3.
- Validate CI stability.

## Implementation Tasks

1. Fix tab order in SessionComponent.
- In `gui/src/ui/SessionComponent.cpp`, the explicit focus order was commented out due to segfault. The issue was likely calling `setExplicitFocusOrder()` on child components of a `Viewport` before the Viewport had positioned them.
- Fix approach: Call `setExplicitFocusOrder()` only in `resized()` after components are laid out, not in the constructor.
- Set order: transport buttons (1-6) → tempo controls (7-9) → snap controls (10-11) → timeline (12) → mixer (13) → tool sidebar (14).
- Verify the segfault was in test context by wrapping in a null check:
  ```cpp
  if (timeline != nullptr)
      timeline->setExplicitFocusOrder (12);
  ```

2. Implement arrow key navigation in MixerComponent.
- In `gui/src/ui/MixerComponent.cpp`, implement `keyPressed()`:
  - Left arrow: Focus previous channel strip.
  - Right arrow: Focus next channel strip.
  - Up arrow: Increase focused strip's volume by 1dB.
  - Down arrow: Decrease focused strip's volume by 1dB.
  - Track the focused strip index. When focus changes, call `strips[index]->grabKeyboardFocus()`.
  - Add visual feedback: the focused strip should show a highlight border.

3. Implement arrow key navigation in PluginBrowserComponent.
- In `gui/src/ui/PluginBrowserComponent.cpp`, ensure `keyPressed()` handles:
  - Up/Down arrows in the plugin list (ListBox already handles this if focusable).
  - Enter key to add selected plugin to chain.
  - Delete key to remove selected plugin from chain.
  - Tab to switch focus between browser list and chain list.
- Verify `setWantsKeyboardFocus(true)` on both ListBox components.

4. Implement arrow key navigation in LibraryComponent.
- In `gui/src/ui/LibraryComponent.cpp`:
  - Up/Down arrows to navigate the file list.
  - Enter to open/select the highlighted file.
  - Backspace to navigate to parent directory.
  - Verify the `DirectoryContentsList`'s `ListBox` is keyboard-focusable.

5. Add accessibility labels to SchemaFormComponent dynamic fields.
- In `gui/src/ui/SchemaFormComponent.cpp`, when creating form controls from tool schemas:
  - For each generated Slider: `slider->setTitle (fieldName); slider->setDescription (description);`
  - For each generated ToggleButton: `toggle->setTitle (fieldName); toggle->setDescription (description);`
  - For each generated ComboBox: `combo->setTitle (fieldName); combo->setDescription (description);`
  - For each generated TextEditor: `editor->setTitle (fieldName); editor->setDescription (description);`

6. Add screen reader announcements for dynamic state changes.
- Create a helper function in `gui/src/util/AccessibilityHelpers.h`:
  ```cpp
  #pragma once
  #include <JuceHeader.h>

  namespace waive {
  inline void announce (juce::Component& comp, const juce::String& text)
  {
      if (auto* handler = comp.getAccessibilityHandler())
          handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
  }
  }
  ```
- Call this when:
  - Tempo changes (`SessionComponent.cpp` after `applyTempo()`).
  - Transport state changes (play/stop/record).
  - Tool plan completes (`ToolSidebarComponent.cpp` after plan/apply completion).

7. Expand test coverage for phases 1-3 audit fixes.
- In `tests/WaiveCoreTests.cpp`:
  - Test AudioAnalysisCache: insert N+1 entries into cache of size N, verify oldest evicted.
  - Test AudioAnalysisCache: verify get() promotes entry to front (doesn't get evicted next).
  - Test GridLine struct: verify minor/major flag storage.
  - Test PathSanitizer: verify case-sensitivity handling (if updated in phase 1).
- In `tests/WaiveUiTests.cpp`:
  - Test that tooltips are present on transport buttons (check `button.getTooltip().isNotEmpty()`).
  - Test that empty state text is rendered (check `timelineComponent.getNumChildComponents() == 0` → empty state label visible).
  - Test track color determinism: same trackIndex always produces same color.
  - Test that WaiveSpacing constants are correct values (xxs=2, xs=4, sm=8, md=12, lg=16, xl=24).

8. Validate CI from clean build.
```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

## Files Expected To Change
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/ui/MixerComponent.h`
- `gui/src/ui/MixerComponent.cpp`
- `gui/src/ui/PluginBrowserComponent.cpp`
- `gui/src/ui/LibraryComponent.cpp`
- `gui/src/ui/SchemaFormComponent.cpp`
- `gui/src/ui/ToolSidebarComponent.cpp`
- `gui/src/util/AccessibilityHelpers.h` (NEW)
- `tests/WaiveUiTests.cpp`
- `tests/WaiveCoreTests.cpp`

## Validation

```bash
cmake --build build --target Waive WaiveUiTests WaiveCoreTests -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

Manual checks:
- Tab through UI — focus indicator moves through all major sections in order.
- Arrow keys navigate mixer strips, plugin lists, library files.
- Screen reader (if available) announces tempo changes, transport state.
- All new tests pass.

## Exit Criteria
- Tab order works without segfault, moves through all major UI sections.
- Arrow key navigation works in MixerComponent, PluginBrowserComponent, LibraryComponent.
- SchemaFormComponent dynamic fields have accessibility titles and descriptions.
- Test suite expanded to cover all audit findings.
- Clean build from scratch passes all tests.
