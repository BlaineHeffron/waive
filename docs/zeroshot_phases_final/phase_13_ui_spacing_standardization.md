# Phase 13: UI Layout Constants & Spacing Standardization

## Objective
Extend `WaiveSpacing.h` with standard UI dimension constants (button heights, row heights, toolbar heights, sidebar widths) and systematically replace ~200 hardcoded pixel values across all GUI components with named constants. This makes the layout consistent, maintainable, and sets the foundation for visual polish.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### WaiveSpacing.h (gui/src/theme/WaiveSpacing.h)
Current spacing scale — only has general-purpose spacing:
```cpp
namespace waive {
namespace Spacing {
    constexpr int xxs = 2;
    constexpr int xs = 4;
    constexpr int sm = 8;
    constexpr int md = 12;
    constexpr int lg = 16;
    constexpr int xl = 24;
}
}
```

No constants for button heights, row heights, toolbar dimensions, scrollbar thickness, or common widths.

### Current hardcoded values audit
These values appear repeatedly across components:

**Button/row heights:**
- `28` — used everywhere as default button/row height (SessionComponent, ToolLogComponent, LibraryComponent, PluginBrowserComponent, ChatPanelComponent)
- `32` — used for larger buttons/rows (ConsoleComponent, ToolLogComponent)
- `24` — used for small controls (ToolSidebarComponent, MainComponent menu bar)
- `36` — toolbar row height (SessionComponent primary row)
- `26` — action button height (ToolSidebarComponent)
- `18` — label/status row height (MixerChannelStrip name, ToolSidebarComponent status)
- `20` — small button height (MixerChannelStrip solo/mute)

**Toolbar/panel heights:**
- `68` — SessionComponent toolbar (two rows)
- `36` — SessionComponent toolbar (single row)
- `30` — TimelineComponent ruler height
- `14` — TimelineComponent scrollbar height

**Panel widths:**
- `280` — ToolSidebarComponent default width
- `120` — TimelineComponent track header width
- `80` — MixerChannelStrip width
- `360` — PluginBrowserComponent left panel

**Other constants:**
- `4` — resizer bar thickness (SessionComponent)
- `160` — ConsoleComponent request editor height
- `120`–`240` — ToolSidebarComponent model section height range

### Components to update
1. **SessionComponent** — transport toolbar layout (resized, ~lines 380-480)
2. **TimelineComponent** — ruler, scrollbar, track heights (resized, ~line 65-80)
3. **MixerComponent** — strip arrangement (resized, ~line 30-40)
4. **MixerChannelStrip** — meter layout, buttons (resized, ~line 260-340)
5. **ToolSidebarComponent** — model section, buttons, form (resized, ~line 375-420)
6. **SchemaFormComponent** — field height, padding (buildForm, ~line 35-40, resized ~line 260-280)
7. **ConsoleComponent** — editor height, button row (resized, ~line 55-65)
8. **ToolLogComponent** — button widths, row heights (resized, ~line 60-70, paintListBoxItem ~line 200-210)
9. **LibraryComponent** — top row, button size (resized, ~line 80-90)
10. **PluginBrowserComponent** — panel split, button widths, row heights (resized, ~line 320-370)
11. **ChatPanelComponent** — header row, button/combo widths (resized, ~line 110-160)
12. **MainComponent** — menu bar height (resized)

## Implementation Tasks

### 1. Modify `gui/src/theme/WaiveSpacing.h` — Add UI dimension constants

Replace the entire file with:
```cpp
#pragma once

namespace waive {
namespace Spacing {
    // ── General padding / gap scale ─────────────────────────────
    constexpr int xxs = 2;
    constexpr int xs  = 4;
    constexpr int sm  = 8;
    constexpr int md  = 12;
    constexpr int lg  = 16;
    constexpr int xl  = 24;

    // ── Standard UI dimension constants ─────────────────────────
    // Button / control heights
    constexpr int controlHeightSmall  = 20;   // solo/mute, tiny toggles
    constexpr int controlHeightMedium = 24;   // combo boxes, small buttons
    constexpr int controlHeightDefault = 28;  // standard buttons, rows
    constexpr int controlHeightLarge  = 32;   // tall buttons, input rows

    // Label / status heights
    constexpr int labelHeight = 18;           // name labels, status text
    constexpr int captionHeight = 14;         // small captions

    // Toolbar / header heights
    constexpr int toolbarRowHeight = 36;      // single toolbar row
    constexpr int menuBarHeight = 24;         // top menu bar

    // Fixed panel dimensions
    constexpr int rulerHeight = 30;           // timeline ruler
    constexpr int scrollbarThickness = 14;    // scrollbar size
    constexpr int resizerBarThickness = 4;    // drag-to-resize bars
    constexpr int trackHeaderWidth = 120;     // timeline track headers
    constexpr int mixerStripWidth = 80;       // mixer channel strip
    constexpr int sidebarWidth = 280;         // tool sidebar default
}
}
```

### 2. Modify `gui/src/MainComponent.cpp` — Replace menu bar hardcode

In `resized()`, replace the hardcoded `24`:
```cpp
// Before:
menuBar.setBounds (bounds.removeFromTop (24));

// After:
menuBar.setBounds (bounds.removeFromTop (waive::Spacing::menuBarHeight));
```

Add `#include "WaiveSpacing.h"` if not already included.

### 3. Modify `gui/src/ui/SessionComponent.cpp` — Standardize transport toolbar

In `resized()`, replace hardcoded toolbar and row values:

```cpp
void SessionComponent::resized()
{
    auto bounds = getLocalBounds();

    // Transport toolbar with responsive layout
    const int viewportWidth = bounds.getWidth();
    const bool showSecondaryControls = viewportWidth >= 900;

    // Before: (showSecondaryControls ? 68 : 36) and .reduced (8, 4)
    // After:
    const int toolbarH = showSecondaryControls
                            ? (waive::Spacing::controlHeightDefault * 2 + waive::Spacing::md)
                            : waive::Spacing::toolbarRowHeight;
    auto toolbar = bounds.removeFromTop (toolbarH);
    toolbar = toolbar.reduced (waive::Spacing::sm, waive::Spacing::xs);

    // Before: toolbar.removeFromTop (28)
    // After:
    auto topRow = toolbar.removeFromTop (waive::Spacing::controlHeightDefault);
    // ... (FlexBox layout stays the same — button widths are content-driven, leave as-is)

    if (showSecondaryControls)
    {
        // Before: toolbar.removeFromTop (28)
        // After:
        auto bottomRow = toolbar.removeFromTop (waive::Spacing::controlHeightDefault);
        // ...
    }

    // Tool sidebar
    // Before: defaultSidebarWidth = 280
    // After: use waive::Spacing::sidebarWidth
    // ...

    // Resizer bars
    // Before: bounds.removeFromTop (4)  or  bounds.removeFromRight (4)
    // After:  bounds.removeFromTop (waive::Spacing::resizerBarThickness)
    // ...
}
```

**Important:** Leave the FlexItem button widths (56, 70, 78, etc.) as-is. These are content-driven widths based on text label size and are not part of the spacing system — they are correctly sized to fit their label text. Only replace structural layout values (toolbar height, row heights, resizer bars, sidebar width, padding).

### 4. Modify `gui/src/ui/TimelineComponent.cpp` — Replace dimension constants

Replace the static constants and hardcoded values:
```cpp
// Before (at class or file level):
// static const int rulerHeight = 30;
// After:
// Remove the local constant, use waive::Spacing::rulerHeight

// In resized():
// Before: bounds.removeFromTop (30) or rulerHeight
// After:  bounds.removeFromTop (waive::Spacing::rulerHeight)

// Before: bounds.removeFromBottom (14) for scrollbar
// After:  bounds.removeFromBottom (waive::Spacing::scrollbarThickness)

// Before: trackHeaderWidth = 120
// After:  waive::Spacing::trackHeaderWidth
```

If TimelineComponent has `static const int rulerHeight = 30;` or similar as a member/local, remove the local definition and use the Spacing constant instead.

### 5. Modify `gui/src/ui/MixerComponent.cpp` — Use strip width constant

```cpp
// Before: MixerChannelStrip::stripWidth
// This is fine as-is if it matches 80. But also ensure:
// Any hardcoded references to the strip width use the constant.
```

### 6. Modify `gui/src/ui/MixerChannelStrip.cpp` — Standardize layout

In `resized()`:
```cpp
// Before: nameLabel height 18
// After:  waive::Spacing::labelHeight

// Before: solo/mute button height 20
// After:  waive::Spacing::controlHeightSmall

// Before: arm + input combo height 24
// After:  waive::Spacing::controlHeightMedium

// Before: pan knob height 36
// After:  waive::Spacing::toolbarRowHeight (or leave as 36 if it's a deliberate knob size)

// Meter width 20 and text area 20 — these are pixel-specific to the meter drawing.
// Leave meter rendering constants as local to MixerChannelStrip since they're
// component-specific, not part of the spacing system.
```

### 7. Modify `gui/src/ui/ToolSidebarComponent.cpp` — Standardize layout

In `resized()`:
```cpp
// Before: action button height 26
// After:  waive::Spacing::controlHeightMedium (24) — close enough, rounds down

// Before: status label height 18
// After:  waive::Spacing::labelHeight

// Before: defaultSidebarWidth reference if any
// After:  waive::Spacing::sidebarWidth

// Before: `(actionRow.getWidth() - 12) / 4`
// After:  `(actionRow.getWidth() - 3 * waive::Spacing::xs) / 4`
```

### 8. Modify `gui/src/ui/SchemaFormComponent.cpp` — Standardize field layout

```cpp
// Before: padding = 6
// After:  padding = waive::Spacing::sm (8) — slightly more generous

// Before: fieldHeight = 52
// After:  Keep as a local constant, but document it:
//         constexpr int fieldHeight = waive::Spacing::lg + waive::Spacing::controlHeightMedium + waive::Spacing::md;
//         (16 + 24 + 12 = 52)
```

### 9. Modify `gui/src/ui/ConsoleComponent.cpp` — Standardize layout

```cpp
// Before: request editor height 160
// After:  Keep as local constant (content-specific)

// Before: button row height 32
// After:  waive::Spacing::controlHeightLarge
```

### 10. Modify `gui/src/ui/ToolLogComponent.cpp` — Standardize layout

```cpp
// Before: button widths 100, 80
// After:  Keep as-is (content-driven, label-specific)

// Before: active job height 32
// After:  waive::Spacing::controlHeightLarge

// Before: row layout 200, 70, 28
// After:  Keep column widths as-is (content-driven), but height 28 → waive::Spacing::controlHeightDefault
```

### 11. Modify `gui/src/ui/LibraryComponent.cpp` — Standardize layout

```cpp
// Before: top row height 28
// After:  waive::Spacing::controlHeightDefault

// Before: button width 32
// After:  waive::Spacing::controlHeightLarge (32) — square button
```

### 12. Modify `gui/src/ui/PluginBrowserComponent.cpp` — Standardize layout

```cpp
// Before: row height 28
// After:  waive::Spacing::controlHeightDefault

// Before: left panel 360
// After:  Keep as local constant (component-specific panel division)

// Button widths (80, 50, 60, etc.) — keep as-is (label-driven)
```

### 13. Modify `gui/src/ai/ChatPanelComponent.cpp` — Standardize layout

```cpp
// Before: header row 28
// After:  waive::Spacing::controlHeightDefault

// Button/combo widths (70, 110, 180, 100, 50) — keep as-is (content-driven)
```

## Guiding Principle

Replace **structural** layout values (heights of rows, toolbars, panels, resize bars, scrollbars) with `Spacing::` constants. **Do NOT replace** content-driven widths (button widths that fit specific text labels, column widths for specific data). Those are correctly sized for their content and should remain as local values.

## Files Expected To Change
- `gui/src/theme/WaiveSpacing.h`
- `gui/src/MainComponent.cpp`
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/ui/TimelineComponent.cpp`
- `gui/src/ui/MixerComponent.cpp`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/ToolSidebarComponent.cpp`
- `gui/src/ui/SchemaFormComponent.cpp`
- `gui/src/ui/ConsoleComponent.cpp`
- `gui/src/ui/ToolLogComponent.cpp`
- `gui/src/ui/LibraryComponent.cpp`
- `gui/src/ui/PluginBrowserComponent.cpp`
- `gui/src/ai/ChatPanelComponent.cpp`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
```

Also verify the UI still looks correct:
```bash
xvfb-run -a -s "-screen 0 1920x1080x24" build/gui/Waive_artefacts/Release/Waive --screenshot /tmp/waive_spacing_check
ls -la /tmp/waive_spacing_check/
```

## Exit Criteria
- `WaiveSpacing.h` contains both the general padding scale and the new UI dimension constants.
- All 12+ component files use `waive::Spacing::` constants for structural layout values (row heights, toolbar heights, panel heights, resizer bars, scrollbar thickness).
- No functional layout changes — the UI should look identical (values are the same or very close).
- Content-driven widths (button widths for specific labels) are left as local values.
- Build compiles with no errors.
- Existing tests still pass (`ctest --test-dir build --output-on-failure`).
