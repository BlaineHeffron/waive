# Phase 01: Hardcoded Colors → Theme Palette

## Objective
Replace all hardcoded color values (hex literals, `juce::Colours::` constants) with palette-based colors from `waive::getWaivePalette()`. This is the single biggest visual consistency problem in the codebase.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH). JUCE module .cpp files fail with `#error "Incorrect use of JUCE cpp file"` when PCH includes JuceHeader.h.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- Do NOT use `juce::Graphics::fillTriangle()` — it does not exist. Use `juce::Path::addTriangle()` + `g.fillPath()`.
- Do NOT use `juce::ScrollBar::setTooltip()` — ScrollBar does not inherit from SettableTooltipClient.
- Do NOT reference `waive::Fonts::small()` — it does not exist. Available fonts: `header()`, `subheader()`, `body()`, `label()`, `caption()`, `mono()`, `meter()`.
- The `waive::ColourPalette` struct has NO member named `error`. Use `danger` for error colors.

## Scope
Only these files need changes. Do NOT modify other files.

## Implementation Tasks

### 1. ClipComponent.cpp — Replace 8 hardcoded colors
File: `gui/src/ui/ClipComponent.cpp`

Every color value in this file should use palette colors. The pattern is:
```cpp
if (auto* pal = waive::getWaivePalette (*this))
    g.setColour (pal->clipDefault);  // use appropriate palette color
```

Replacements:
- `juce::Colours::grey` → `pal->textMuted`
- `0xff4477aa` → `pal->clipSelected`
- `0xff3a5a3a` → `pal->clipDefault.darker(0.2f)`
- `juce::Colours::white.withAlpha(0.8f)` → `pal->textOnPrimary.withAlpha(0.8f)`
- Any other hardcoded hex colors → appropriate palette member

Keep the existing `if (auto* pal = ...)` / `else` fallback pattern but change fallback values to reasonable defaults from `juce::Colours::` only as absolute last resort.

### 2. MixerChannelStrip.cpp — Replace 8 hardcoded colors
File: `gui/src/ui/MixerChannelStrip.cpp`

Replacements:
- `0xff2a2a2a` → `pal->panelBg`
- `0xff3a3a3a` → `pal->surfaceBg`
- `juce::Colours::red` → `pal->meterClip`
- `juce::Colours::limegreen` → `pal->meterNormal`
- `juce::Colours::yellow` → `pal->meterWarning`
- `juce::Colours::white` → `pal->textPrimary`

The meter gradient should use palette colors: `pal->meterNormal` → `pal->meterWarning` → `pal->meterClip`.

### 3. ConsoleComponent.cpp — Replace 4 hardcoded colors
File: `gui/src/ui/ConsoleComponent.cpp`

Replacements:
- `juce::Colours::grey` → `pal->textMuted`
- `juce::Colours::white` → `pal->textPrimary`
- `juce::Colours::red` → `pal->danger` (already fixed in one place, check for others)

### 4. TrackLaneComponent.cpp — Replace hardcoded colors
File: `gui/src/ui/TrackLaneComponent.cpp`

Check for any remaining hardcoded hex colors or `juce::Colours::` and replace with palette.

### 5. TimeRulerComponent.cpp — Replace hardcoded colors
File: `gui/src/ui/TimeRulerComponent.cpp`

### 6. PlayheadComponent.cpp — Replace hardcoded colors
File: `gui/src/ui/PlayheadComponent.cpp`

### 7. LibraryComponent.cpp — Replace hardcoded colors
File: `gui/src/ui/LibraryComponent.cpp`

### 8. MixerComponent.cpp — Replace hardcoded colors
File: `gui/src/ui/MixerComponent.cpp`

## Files Expected To Change
- `gui/src/ui/ClipComponent.cpp`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/ConsoleComponent.cpp`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/ui/TimeRulerComponent.cpp`
- `gui/src/ui/PlayheadComponent.cpp`
- `gui/src/ui/LibraryComponent.cpp`
- `gui/src/ui/MixerComponent.cpp`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- Zero hardcoded hex color values in the UI component files listed above.
- Zero `juce::Colours::` constants in paint/drawing code (except as fallback in `else` branches).
- Build compiles with no errors.
- Visual appearance unchanged (same colors, just sourced from palette).
