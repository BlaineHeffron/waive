# Phase 03: UI Polish and Focus Indicators

## Objective
Complete focus indicator coverage on all interactive elements, add empty state messages, and standardize border radius/thickness for a professional visual appearance.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH). JUCE module .cpp files fail when PCH includes JuceHeader.h.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- Do NOT use `juce::Graphics::fillTriangle()` — it does not exist.
- Do NOT use `juce::ScrollBar::setTooltip()` — ScrollBar is not SettableTooltipClient.
- Do NOT reference `waive::Fonts::small()` — it does not exist. Available fonts: `header()`, `subheader()`, `body()`, `label()`, `caption()`, `mono()`, `meter()`.
- The `waive::ColourPalette` struct has NO member named `error`. Use `danger` for error colors.
- Do NOT call `setExplicitFocusOrder()` — it causes segfaults in tests. Instead, use `setWantsKeyboardFocus(true)` and manual focus management.

## Implementation Tasks

### 1. WaiveLookAndFeel — Standardize focus indicators
File: `gui/src/theme/WaiveLookAndFeel.cpp`

Currently buttons show a 2px focus ring, sliders show a 2px ring, text editors show a 1px ring. Standardize ALL interactive elements to use a consistent 2px rounded focus ring using `palette.borderFocused` color.

Add custom drawing for `drawToggleButton` override to show focus indicator on toggle/checkbox controls:
```cpp
void WaiveLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool shouldDrawAsHighlighted, bool shouldDrawAsDown)
{
    // Draw the toggle content
    // ...
    // Draw focus ring if keyboard-focused
    if (button.hasKeyboardFocus (true))
    {
        g.setColour (palette.borderFocused);
        g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (1.0f), 4.0f, 2.0f);
    }
}
```

Also add `WaiveLookAndFeel.h` declaration for `drawToggleButton`.

### 2. WaiveLookAndFeel — Standardize border radii
File: `gui/src/theme/WaiveLookAndFeel.cpp`

Currently mixed: 4.0f, 3.0f, 2.0f corner radii across components. Standardize to:
- Buttons: 4.0f
- Sliders (track): 4.0f
- ComboBoxes: 4.0f
- TextEditors: 4.0f
- Progress bars: 4.0f

Search for all `drawRoundedRectangle` and `fillRoundedRectangle` calls and ensure consistent 4.0f radius.

### 3. Empty states for PluginBrowserComponent
File: `gui/src/ui/PluginBrowserComponent.cpp`

When no track is selected or no plugins are available, show a centered help message:
```cpp
// In paint() or resized(), if no track selected:
g.setColour (pal->textMuted);
g.setFont (waive::Fonts::body());
g.drawText ("Select a track to manage plugins", getLocalBounds(), juce::Justification::centred);
```

### 4. Empty state for ToolSidebarComponent
File: `gui/src/ui/ToolSidebarComponent.cpp`

When no tool is selected or sidebar is empty, show a help message:
```cpp
g.drawText ("Select a tool from the menu", bounds, juce::Justification::centred);
```

### 5. Tool error feedback visibility
File: `gui/src/ui/ToolLogComponent.cpp`

When a tool job fails, ensure the error message is visually distinct:
- Use `pal->danger` color for failed job text
- Show the error reason text prominently

## Files Expected To Change
- `gui/src/theme/WaiveLookAndFeel.h`
- `gui/src/theme/WaiveLookAndFeel.cpp`
- `gui/src/ui/PluginBrowserComponent.cpp`
- `gui/src/ui/ToolSidebarComponent.cpp`
- `gui/src/ui/ToolLogComponent.cpp`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- All interactive controls (buttons, toggles, sliders, combos, text editors) have 2px focus rings.
- Consistent 4.0f corner radius on all rounded elements.
- Empty states with helpful text in PluginBrowser and ToolSidebar.
- Tool errors visually distinct in ToolLog.
- Build compiles with no errors.
