# Phase 04: Performance Optimization

## Objective
Reduce CPU usage, improve startup time, and bound memory growth by addressing timer proliferation, blocking startup operations, and unbounded caches.

## Scope
- Consolidate per-strip mixer timers into single MixerComponent timer.
- Defer plugin scanning to background thread.
- Add LRU eviction to AudioAnalysisCache.
- Change previewClipIDs to unordered_set for O(1) lookups.
- Cache grid line calculations in TrackLaneComponent.
- Add precompiled header support to CMake.

## Implementation Tasks

1. Consolidate MixerChannelStrip timers.
- Remove `juce::Timer` inheritance from `MixerChannelStrip`.
- In `MixerComponent`, run a single 30Hz timer.
- In `MixerComponent::timerCallback()`, iterate all channel strips and call a new `MixerChannelStrip::pollState()` method that does what `timerCallback()` currently does.
- This reduces N timers (one per strip) to 1 timer regardless of track count.

2. Defer plugin scanning to background thread.
- In `Main.cpp`, remove the blocking `engine->getPluginManager().initialise()` call from startup.
- Instead, after showing the main window, schedule a background scan via `JobQueue` or `juce::Thread`.
- While scanning, show a non-blocking status message in the plugin browser ("Scanning plugins...").
- When scan completes, notify `PluginBrowserComponent` to rebuild its list.

3. Add LRU eviction to AudioAnalysisCache.
- Add a `maxEntries` constructor parameter (default 256).
- Maintain an access-order list alongside the unordered_map.
- On `get()`, move accessed entry to front of list.
- On `put()`, if cache exceeds `maxEntries`, evict the least-recently-used entry.
- Keep the existing `juce::CriticalSection` for thread safety.

4. Change previewClipIDs to unordered_set.
- In `TimelineComponent.h`, change `juce::Array<te::EditItemID> previewClipIDs` to `std::unordered_set<te::EditItemID>`.
- Update all call sites: `add()` → `insert()`, `contains()` → `count()` or `contains()`, `clear()` remains.
- In `ClipComponent::paint()`, replace the linear search loop with a single `contains()` call.

5. Cache grid line calculations.
- In `TrackLaneComponent`, add `cachedGridLines` (vector of x positions) and dirty flags `cachedScrollOffset`, `cachedPixelsPerSecond`.
- Only recalculate grid lines when scroll offset or pixels-per-second changes.
- Use cached values in `paint()` instead of calling `timeline.getGridLineTimes()` every frame.

6. Add precompiled header support.
- In `gui/CMakeLists.txt`, after `juce_add_gui_app(Waive ...)`, add:
  ```cmake
  target_precompile_headers(Waive PRIVATE <JuceHeader.h>)
  ```
- Similarly for test targets.
- Verify build still works and is faster.

7. Use Tracktion Engine listeners instead of polling where possible.
- In `SessionComponent`, investigate replacing the 10Hz timer for transport position with `te::TransportControl::Listener` if the API supports it. If not, document why polling is necessary.

## Files Expected To Change
- `gui/src/ui/MixerComponent.h`
- `gui/src/ui/MixerComponent.cpp`
- `gui/src/ui/MixerChannelStrip.h`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/TimelineComponent.h`
- `gui/src/ui/TimelineComponent.cpp`
- `gui/src/ui/ClipComponent.cpp`
- `gui/src/ui/TrackLaneComponent.h`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/tools/AudioAnalysisCache.h`
- `gui/src/tools/AudioAnalysisCache.cpp`
- `gui/src/Main.cpp`
- `gui/CMakeLists.txt`
- `tests/CMakeLists.txt`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

Profiling checks:
- Open a 10-track project. CPU usage at idle should be measurably lower.
- App should show UI within 1 second of launch (plugin scan in background).
- AudioAnalysisCache should cap at 256 entries.
- Build time should decrease with PCH enabled.

## Exit Criteria
- Only 1 mixer timer regardless of track count.
- Plugin scanning doesn't block startup.
- AudioAnalysisCache has bounded memory usage.
- previewClipIDs uses O(1) lookups.
- Grid lines are cached in TrackLaneComponent.
- PCH enabled for main and test targets.
