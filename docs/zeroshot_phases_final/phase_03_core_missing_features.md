# Phase 03: UI/UX Polish

## Objective
Bring UI polish to professional DAW standards: complete tooltip coverage, replace all magic number spacing with WaiveSpacing constants, add error feedback for silent failures, fix empty state messages, and add focus indicators on all interactive controls.

## Scope
- Add tooltips to all remaining interactive controls (~60% of controls missing).
- Replace magic number pixel values with WaiveSpacing constants.
- Add error feedback for silent failure paths.
- Fix LibraryComponent empty state message.
- Add keyboard focus indicators to sliders, buttons, and list items.
- Add shift+wheel horizontal scroll to timeline.

## Implementation Tasks

1. Add missing tooltips to all interactive controls.
- **ConsoleComponent** (`gui/src/ui/ConsoleComponent.cpp`): Add tooltips to:
  - `sendButton.setTooltip ("Send command (Enter)");`
  - `clearButton.setTooltip ("Clear response");`
  - `requestEditor.setTooltip ("Enter JSON command here");`
- **LibraryComponent** (`gui/src/ui/LibraryComponent.cpp`): Add tooltips to:
  - `goUpButton.setTooltip ("Navigate to parent directory");`
  - `addFavButton.setTooltip ("Add current directory to favorites");`
  - `favoritesCombo.setTooltip ("Jump to a favorite directory");`
- **SchemaFormComponent** (`gui/src/ui/SchemaFormComponent.cpp`): When creating dynamic form fields from tool schemas, use the `description` field from the schema as the tooltip:
  - For each slider/toggle/combo/text editor created, call `component->setTooltip (description)` where `description` comes from the schema field's description property.
- **TimelineComponent** (`gui/src/ui/TimelineComponent.cpp`): Add:
  - `horizontalScrollbar.setTooltip ("Scroll timeline horizontally");`
- **TrackLaneComponent** (`gui/src/ui/TrackLaneComponent.cpp`): Add:
  - `automationParamCombo.setTooltip ("Select automation parameter to display");`
- **SessionComponent** (`gui/src/ui/SessionComponent.cpp`): Ensure ALL tooltips include keyboard shortcuts where applicable. Check that:
  - Loop button: "Loop On/Off (L)"
  - Punch button: "Punch In/Out"
  - Snap toggle: "Snap to Grid"
  - All tempo/time sig controls have tooltips

2. Replace magic number spacing with WaiveSpacing constants.
- Import `WaiveSpacing.h` in all UI .cpp files that use hardcoded pixel values.
- Systematic replacements (search and replace with context):
  - `reduced (2)` → `reduced (waive::Spacing::xxs)` (where semantically a tiny inset)
  - `reduced (4)` → `reduced (waive::Spacing::xs)`
  - `reduced (8)` → `reduced (waive::Spacing::sm)`
  - `reduced (12)` → `reduced (waive::Spacing::md)`
  - `reduced (16)` → `reduced (waive::Spacing::lg)`
  - `reduced (24)` → `reduced (waive::Spacing::xl)`
- Key files to update:
  - `MixerChannelStrip.cpp`: lines with `reduced (2)`, hardcoded 18, 20, 36 pixels
  - `PluginBrowserComponent.cpp`: `reduced (8)`
  - `LibraryComponent.cpp`: `reduced (8)`
  - `ConsoleComponent.cpp`: `reduced (12)`
  - `ToolLogComponent.cpp`: `reduced (12)`
  - `ClipComponent.cpp`: `reduced (4.0f, 1.0f)` → `reduced ((float) waive::Spacing::xs, 1.0f)`
  - `SchemaFormComponent.cpp`: hardcoded 16, 22, 12

3. Add error feedback for silent failures.
- **LibraryComponent** (`gui/src/ui/LibraryComponent.cpp`): In `fileDoubleClicked()`, when early-returning due to unsupported format or missing file, show an alert:
  ```cpp
  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
      "Cannot Load File", "The selected file could not be loaded.");
  ```
- **TimelineComponent** (`gui/src/ui/TimelineComponent.cpp`): In `itemDropped()`, when early-returning due to invalid drop target or unsupported file, show a status message or alert.
- **ConsoleComponent** (`gui/src/ui/ConsoleComponent.cpp`): When the response contains an error (check for `"error"` key in JSON), color the response text red. Use `responseEditor.setColour (juce::TextEditor::textColourId, errorColour)` temporarily.

4. Fix LibraryComponent empty state message.
- In `gui/src/ui/LibraryComponent.cpp`, the empty state says "Click '+ Folder' to add a media directory" but there is no "+ Folder" button.
- Change the message to match the actual UI: "Click '+' to add a favorite directory" or adjust to reference the actual `addFavButton`.

5. Add keyboard focus indicators to sliders and buttons.
- In `gui/src/theme/WaiveLookAndFeel.cpp`:
  - In `drawButtonBackground()`: Add a check `if (button.hasKeyboardFocus(true))` and draw a 2px primary-color border even when not hovered:
    ```cpp
    if (button.hasKeyboardFocus (true))
    {
        g.setColour (pal ? pal->primary : juce::Colour (0xff4488cc));
        g.drawRoundedRectangle (area.reduced (1.0f), cornerSize, 2.0f);
    }
    ```
  - In `drawLinearSlider()` and `drawRotarySlider()`: Add similar focus ring when `slider.hasKeyboardFocus(true)`.

6. Add shift+wheel horizontal scroll to timeline.
- In `gui/src/ui/TimelineComponent.cpp`, in `mouseWheelMove()`:
  - Currently Cmd+wheel = zoom, plain wheel = vertical scroll.
  - Add: `if (e.mods.isShiftDown())` → scroll horizontally by adjusting `scrollOffsetSeconds`.
  - Amount: `scrollOffsetSeconds -= wheel.deltaY * 2.0 / pixelsPerSecond` (scroll proportional to zoom).

7. Add ConsoleComponent empty state.
- In `gui/src/ui/ConsoleComponent.cpp`, set initial text for the response editor:
  ```cpp
  responseEditor.setText ("Enter a JSON command above and click Send.\nSee docs/command_schema.json for available commands.");
  responseEditor.setColour (juce::TextEditor::textColourId, pal ? pal->textMuted : juce::Colours::grey);
  ```
- Clear this placeholder when the first real response arrives.

## Files Expected To Change
- `gui/src/ui/ConsoleComponent.cpp`
- `gui/src/ui/LibraryComponent.cpp`
- `gui/src/ui/SchemaFormComponent.cpp`
- `gui/src/ui/TimelineComponent.cpp`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/PluginBrowserComponent.cpp`
- `gui/src/ui/ToolLogComponent.cpp`
- `gui/src/ui/ClipComponent.cpp`
- `gui/src/theme/WaiveLookAndFeel.cpp`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

Manual checks:
- Hover over every control — tooltip should appear for all interactive elements.
- Tab through controls — blue focus ring should be visible on focused control.
- Shift+scroll wheel should scroll timeline horizontally.
- Try to load an unsupported file from library — error dialog should appear.

## Exit Criteria
- 100% tooltip coverage on interactive controls.
- No magic number pixel values in layout code (all use WaiveSpacing).
- Silent failure paths show user-visible error feedback.
- LibraryComponent empty state references correct button.
- Focus ring visible on focused buttons, sliders, and list items.
- Shift+wheel horizontal scrolling works.
- Build clean, all tests pass.
