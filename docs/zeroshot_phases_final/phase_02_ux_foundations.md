# Phase 02: Performance Optimization

## Objective
Reduce CPU usage by consolidating timers, optimizing paint methods, caching expensive lookups, and adding precompiled headers for faster builds.

## Scope
- Consolidate 8 concurrent timers down to ~4.
- Optimize pixel-by-pixel meter gradient rendering.
- Cache track index lookups in ClipComponent paint.
- Use static WaiveFonts instead of inline FontOptions.
- Replace Path allocations with direct Graphics primitives for fade triangles.
- Add precompiled headers for build speed.
- Only poll visible mixer strips.

## Implementation Tasks

1. Remove WaiveApplication timer — use event-driven title updates.
- In `gui/src/Main.cpp`, the `WaiveApplication` class runs a 4Hz timer just to poll `projectManager->isDirty()` for window title updates. This is wasteful.
- Remove `private juce::Timer` inheritance and `timerCallback()` override from `WaiveApplication`.
- Remove `startTimerHz(4)` from `initialise()` and `stopTimer()` from `shutdown()`.
- Instead, call `updateWindowTitle()` directly from:
  - `editChanged()` (already does this).
  - Add a `ProjectManager::Listener` interface with `projectDirtyChanged()` callback, and implement it in `WaiveApplication` to call `updateWindowTitle()`.
  - Or simpler: In `ProjectManager`, after any `setDirty()` call, use `juce::MessageManager::callAsync` to notify.

2. Consolidate TrackLaneComponent timers into TimelineComponent.
- Currently each `TrackLaneComponent` has its own 5Hz timer. With N tracks, this creates N timers.
- In `gui/src/ui/TrackLaneComponent.h`, remove `juce::Timer` inheritance.
- In `gui/src/ui/TrackLaneComponent.cpp`, remove `startTimerHz()` and `timerCallback()`.
- Add a public `void pollState()` method to `TrackLaneComponent` that contains the old timer logic.
- In `gui/src/ui/TimelineComponent.cpp`, in its existing timer callback (~5Hz), iterate all track lanes and call `lane->pollState()`.

3. Optimize MixerComponent to only poll visible strips.
- In `gui/src/ui/MixerComponent.cpp` timerCallback, currently iterates ALL strips calling `pollState()`.
- Use the viewport's visible area to determine which strips are visible:
  ```cpp
  auto visibleArea = stripViewport.getViewArea();
  for (auto& strip : strips)
      if (strip != nullptr && strip->getBounds().intersects(visibleArea))
          strip->pollState();
  ```
- This reduces work from N strips to ~8-12 visible strips.

4. Pre-render meter gradients instead of pixel-by-pixel drawing.
- In `gui/src/ui/MixerChannelStrip.cpp`, the meter paint currently does a `for (int i = 0; i < barH; ++i)` loop with individual `fillRect(x, y, 6, 1)` calls — hundreds per strip.
- Replace with `juce::ColourGradient`:
  ```cpp
  juce::ColourGradient gradient (pal->meterClip, 0, topOfMeter,
                                  pal->meterNormal, 0, bottomOfMeter, false);
  gradient.addColour (0.45, pal->meterWarning); // -12dB position
  gradient.addColour (0.05, pal->meterClip);    // -3dB position
  g.setGradientFill (gradient);
  g.fillRect (meterRect.removeFromBottom (barH));
  ```

5. Cache track index in ClipComponent.
- In `gui/src/ui/ClipComponent.cpp`, the `paint()` method does a linear search through all tracks to find the track index for color blending — on every repaint.
- Add an `int cachedTrackIndex = 0;` member to `ClipComponent.h`.
- Set it once in the constructor or in a new `updateTrackIndex()` method.
- Call `updateTrackIndex()` from the constructor and whenever the clip is reparented.
- In `paint()`, use `cachedTrackIndex` directly instead of the loop.

6. Use WaiveFonts instead of inline FontOptions in paint methods.
- In `ClipComponent.cpp` line 123: Replace `g.setFont (juce::FontOptions (11.0f))` with `g.setFont (waive::Fonts::caption())`.
- In `MixerChannelStrip.cpp` line 48: Replace `juce::FontOptions (11.0f)` with `waive::Fonts::caption()`.
- Search for other `juce::FontOptions` usages in paint paths and replace with the appropriate `waive::Fonts::` static method.

7. Replace Path allocations with Graphics primitives for fade triangles.
- In `ClipComponent.cpp` lines 98-118, replace `juce::Path fadeInPath; fadeInPath.addTriangle(...)` with direct `g.fillTriangle(...)` calls.
- `juce::Graphics::fillTriangle` takes 6 float coordinates and avoids the `juce::Path` heap allocation.

8. Add precompiled headers.
- In `gui/CMakeLists.txt`, after the `target_link_libraries` call, add:
  ```cmake
  target_precompile_headers(Waive PRIVATE
      <JuceHeader.h>
      <tracktion_engine/tracktion_engine.h>
  )
  ```
- This should reduce incremental build times by 30-50%.

## Files Expected To Change
- `gui/src/Main.cpp`
- `gui/src/ui/TrackLaneComponent.h`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/ui/TimelineComponent.cpp`
- `gui/src/ui/MixerComponent.cpp`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/ClipComponent.h`
- `gui/src/ui/ClipComponent.cpp`
- `gui/CMakeLists.txt`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- Timer count reduced from 8 to ~4 concurrent timers.
- Meter gradient renders via ColourGradient, not pixel loop.
- ClipComponent paint uses cached track index (no linear search).
- No inline FontOptions in paint methods.
- Fade triangles use fillTriangle, not Path objects.
- PCH configured in CMakeLists.txt.
- Build clean, all tests pass.
