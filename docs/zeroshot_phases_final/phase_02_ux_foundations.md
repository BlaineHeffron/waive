# Phase 02: UX Foundations

## Objective
Add the foundational UX layer that makes the application discoverable and responsive to user interaction. Focus on tooltips, empty states, hover/focus feedback, and selection status.

## Scope
- Tooltips on all interactive controls.
- Empty state messages for all major panels.
- Stronger hover and focus visual states.
- Selection count status display.
- Minimum window size enforcement.
- Spacing constants for consistent padding.

## Implementation Tasks

1. Add tooltips to all interactive controls.
- Transport buttons (play, stop, record, go-to-start, loop, punch-in, punch-out): set tooltip text describing function and keyboard shortcut.
- Mixer controls: fader ("Track Volume (dB)"), pan ("Pan L/R"), solo ("Solo (S)"), mute ("Mute (M)").
- Timeline: snap toggle ("Snap to Grid"), zoom controls, add-track button.
- Tool sidebar: tool selector, run/apply/cancel buttons, model install/uninstall.
- Tempo slider, time signature dropdowns, position display.
- Use `setTooltip()` on each component.

2. Add empty state messages.
- `TimelineComponent`: When no tracks exist, draw centered text "Click '+ Track' to add your first track" in `textMuted` color.
- `LibraryComponent`: When no folders added, show "Click '+ Folder' to add a media directory".
- `PluginBrowserComponent`: When plugin list is empty, show "Click 'Scan' to find installed plugins".
- `ToolSidebarComponent`: When no tool selected, show "Select a tool from the dropdown above".
- `MixerComponent`: When no tracks, show "Add tracks to see the mixer".
- Each empty state should use `waive::Fonts::body()` and `pal->textMuted`.

3. Strengthen hover and pressed states in LookAndFeel.
- In `WaiveLookAndFeel::drawButtonBackground()`, change:
  - Hover: from `brighter(0.08f)` to `brighter(0.15f)`.
  - Pressed: from `darker(0.05f)` to `darker(0.15f)`.
- Add hover highlight to `ClipComponent`: draw a subtle bright border on `mouseEnter`, remove on `mouseExit`. Add `isHovered` flag.
- Add hover highlight to `TrackLaneComponent` header area: slightly brighten background on hover.

4. Add keyboard focus indicators.
- In `WaiveLookAndFeel`, override `drawFocusOutline()` or add a 2px `pal->primary` border around focused components.
- Set `setWantsKeyboardFocus(true)` on transport buttons, mixer solo/mute buttons, tool sidebar buttons.
- Ensure Tab key cycles through focusable controls within the active panel.

5. Add selection count status.
- In `SessionComponent`, add a small status label in the transport toolbar area.
- Update it whenever `SelectionManager` fires `selectionChanged()`:
  - 0 selected: show nothing or "Ready"
  - 1 clip: show clip name
  - N clips: show "N clips selected"
- Use `waive::Fonts::caption()` and `pal->textMuted`.

6. Enforce minimum window size.
- In `Main.cpp` where the window is created, add `setResizeLimits(1024, 600, 4096, 2160)`.

7. Define spacing constants.
- Add to `gui/src/theme/WaiveSpacing.h`:
  ```cpp
  namespace waive { namespace Spacing {
      constexpr int xxs = 2, xs = 4, sm = 8, md = 12, lg = 16, xl = 24;
  }}
  ```
- Use these constants in at least the transport toolbar layout in SessionComponent (replace magic numbers like `reduced(2)`, `reduced(4)`, etc.).

## Files Expected To Change
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/ui/TimelineComponent.cpp`
- `gui/src/ui/MixerComponent.cpp`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/ClipComponent.h`
- `gui/src/ui/ClipComponent.cpp`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/ui/LibraryComponent.cpp`
- `gui/src/ui/PluginBrowserComponent.cpp`
- `gui/src/ui/ToolSidebarComponent.cpp`
- `gui/src/theme/WaiveLookAndFeel.cpp`
- `gui/src/theme/WaiveSpacing.h` (NEW)
- `gui/src/Main.cpp`
- `tests/WaiveUiTests.cpp`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

Manual checks:
- Hover over every button — tooltip should appear within 500ms.
- Open new project — empty state messages visible in timeline, mixer, library.
- Tab through controls — focus indicator visible on each.
- Select 3 clips — status shows "3 clips selected".
- Resize window to minimum — controls should not overlap.

## Exit Criteria
- Every interactive control has a tooltip.
- All empty panels show guidance text.
- Hover/focus states are visually distinct.
- Selection count is displayed.
- Window has enforced minimum size.
