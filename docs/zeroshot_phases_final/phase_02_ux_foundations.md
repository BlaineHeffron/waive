# Phase 02: Performance Optimization

## Objective
Reduce UI thread pressure by consolidating timers, caching expensive paint operations, and adding change detection before UI updates. Target: reduce timer callbacks from ~75/sec to ~30/sec and eliminate heap allocations in paint().

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH). JUCE module .cpp files fail when PCH includes JuceHeader.h.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- Do NOT use `juce::Graphics::fillTriangle()` — it does not exist.
- Do NOT use `juce::ScrollBar::setTooltip()` — ScrollBar is not SettableTooltipClient.
- Do NOT reference `waive::Fonts::small()` — it does not exist. Available fonts: `header()`, `subheader()`, `body()`, `label()`, `caption()`, `mono()`, `meter()`.
- The `waive::ColourPalette` struct has NO member named `error`. Use `danger` for error colors.

## Implementation Tasks

### 1. SessionComponent — Change detection before control updates
File: `gui/src/ui/SessionComponent.cpp`

In `timerCallback()`, the code currently updates 8 slider/combobox controls unconditionally every tick (10 Hz = 80 redundant updates/sec).

Fix: Cache last-known values as member variables and only call `setValue()`/`setSelectedId()` when the value actually changes:

```cpp
// Add members to SessionComponent.h:
double lastTempo = -1.0;
int lastNumerator = -1, lastDenominator = -1;
bool lastLoopState = false, lastPunchState = false;
bool lastClickState = false, lastSnapState = false;

// In timerCallback(), wrap each update:
auto newTempo = seq.getTempo(0)->getBpm();
if (newTempo != lastTempo)
{
    tempoSlider.setValue (newTempo, juce::dontSendNotification);
    lastTempo = newTempo;
}
// Same pattern for all other controls
```

Also optimize the position display string formatting — currently creates 3 temporary juce::String allocations per tick. Use `juce::String::formatted()` or a single `snprintf` into a stack buffer.

### 2. MixerChannelStrip — Cache meter gradient
File: `gui/src/ui/MixerChannelStrip.cpp`

In `paint()`, a `juce::ColourGradient` with 3 color stops is created every paint call during meter updates.

Fix: Cache the gradient as a member variable. Rebuild only when the meter bounds change (in `resized()` or when `lastMeterBounds` changes):

```cpp
// In MixerChannelStrip.h, add:
juce::ColourGradient meterGradient;
juce::Rectangle<int> cachedMeterGradientBounds;

// In paint(), at gradient creation point:
if (meterBounds != cachedMeterGradientBounds)
{
    cachedMeterGradientBounds = meterBounds;
    meterGradient = juce::ColourGradient (...); // build once
    meterGradient.addColour (...);
}
g.setGradientFill (meterGradient);
```

### 3. PlayheadComponent — Skip redundant repaints
File: `gui/src/ui/PlayheadComponent.cpp`

The 30 Hz timer calls `repaint()` every tick even when the playhead hasn't moved.

Fix: Cache `lastPlayheadX` and only call `repaint()` when the pixel position changes:

```cpp
int newX = timeToX (transport.getPosition().inSeconds());
if (newX != lastPlayheadX)
{
    lastPlayheadX = newX;
    repaint();
}
```

### 4. TrackLaneComponent — Batch grid line drawing
File: `gui/src/ui/TrackLaneComponent.cpp`

Currently calls `g.drawVerticalLine()` individually for each grid line (~50 calls per paint).

Fix: Build a `juce::Path` with all major lines, another with all minor lines, then stroke each path once:

```cpp
juce::Path majorPath, minorPath;
for (const auto& gl : cachedGridLines)
{
    float xf = (float) gl.x;
    if (gl.isMinor)
        minorPath.addLineSegment ({ xf, top, xf, bottom }, 1.0f);
    else
        majorPath.addLineSegment ({ xf, top, xf, bottom }, 1.0f);
}
g.setColour (pal->gridMinor);
g.fillPath (minorPath);
g.setColour (pal->gridMajor);
g.fillPath (majorPath);
```

### 5. JobQueue — HashMap for O(1) cancel
File: `gui/src/tools/JobQueue.h` and `gui/src/tools/JobQueue.cpp`

`cancelJob()` does O(N) linear scan. Add a `std::unordered_map<int, size_t>` index for O(1) lookup. Or simpler: use `std::unordered_map<int64_t, std::shared_ptr<JobInfo>>` as the primary storage.

## Files Expected To Change
- `gui/src/ui/SessionComponent.h`
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/ui/MixerChannelStrip.h`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/PlayheadComponent.h`
- `gui/src/ui/PlayheadComponent.cpp`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/tools/JobQueue.h`
- `gui/src/tools/JobQueue.cpp`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- SessionComponent timerCallback only updates controls when values change.
- MixerChannelStrip meter gradient cached, not recreated per paint.
- PlayheadComponent skips repaint when position unchanged.
- Grid lines drawn via Path batching instead of individual calls.
- JobQueue cancel is O(1).
- Build compiles with no errors.
