# Phase 01: Correctness and Safety Fixes

## Objective
Fix all critical and high-severity correctness issues found in the post-Phase-6 audit: thread safety bugs, memory safety issues, logic errors, and data structure problems.

## Scope
- Thread safety in JobQueue, ProjectManager, ModelManager
- Memory safety in MixerChannelStrip destructor
- Grid line cache bit-packing overflow
- AudioAnalysisCache O(n) list removal
- Audio format reader validation
- Test debug file leak cleanup
- EditSession null check

## Implementation Tasks

1. Fix JobQueue timerCallback race condition.
- In `gui/src/tools/JobQueue.cpp`, the `timerCallback()` method takes a snapshot of the jobs vector, releases the lock, then iterates and potentially erases jobs. Between taking the snapshot and erasing, other threads could modify the vector.
- Replace the current pattern with a "completed jobs" list approach:
  - While holding `jobsMutex`, move completed jobs from `jobs` to a local `completedJobs` vector using `std::remove_if` + `erase`.
  - Release the lock.
  - Then iterate `completedJobs` to fire callbacks on the message thread.
  - This ensures no race between snapshot and erase.
- Also change `nextJobId` from `int` to `int64_t` in `gui/src/tools/JobQueue.h` to prevent overflow after 2B jobs.

2. Fix MixerChannelStrip dangling pointer in destructor.
- In `gui/src/ui/MixerChannelStrip.cpp`, the destructor accesses `track` and `masterEdit` pointers that could be dangling if the track/edit was deleted first.
- Fix: In the constructor, store a `juce::Component::SafePointer` or a `te::EditItemID` for the track. In the destructor, look up the track by ID from the edit instead of using a raw pointer.
- Alternative simpler fix: Use `juce::ReferenceCountedObjectPtr` or check if the edit still contains the track before dereferencing.
- The safest approach: Cache a pointer to the `te::LevelMeterPlugin::MeasurerClient` registration and wrap the destructor in a try/catch, or use a flag that gets cleared when the track is about to be deleted.

3. Fix grid line cache bit-packing integer overflow.
- In `gui/src/ui/TrackLaneComponent.cpp` lines ~100-103, grid lines use `| 0x80000000` to mark minor lines. This breaks for negative X values and very large X values.
- Replace with a `struct GridLine { int x; bool isMinor; };` and change `cachedGridLines` from `std::vector<int>` to `std::vector<GridLine>`.
- Update `TrackLaneComponent.h` to declare the new struct and vector type.
- Update the paint code (~line 110-120) to read `gridLine.x` and `gridLine.isMinor` instead of bit masking.

4. Fix AudioAnalysisCache O(n) list removal.
- In `gui/src/tools/AudioAnalysisCache.cpp`, `accessOrder.remove(key)` is O(n) on every cache hit.
- Add a `std::unordered_map<CacheKey, std::list<CacheKey>::iterator> iterMap` member to `AudioAnalysisCache.h`.
- In `get()`: Use `iterMap[key]` to get the iterator, then `accessOrder.erase(it)` and `accessOrder.push_front(key)`, then update `iterMap[key]`.
- In `put()`: Same pattern for existing key updates. On eviction, erase `iterMap[lru]`. On insert, store new iterator.
- In `clear()`: Also clear `iterMap`.

5. Add audio format reader validation.
- In `gui/src/tools/AudioAnalysis.cpp`, after creating the `AudioFormatReader`, add a check:
  ```cpp
  if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
      return summary;
  ```
- This prevents division by zero in downstream code that divides by sampleRate.

6. Remove debug file write from tests.
- In `tests/WaiveUiTests.cpp`, find and remove or guard any writes to `/tmp/gainstage_debug.txt` or similar debug files.
- Use `#ifdef WAIVE_DEBUG_TESTS` guard if the debug output is useful during development.

7. Add null check in EditSession::performEdit.
- In `gui/src/edit/EditSession.cpp`, at the start of `performEdit()`, add:
  ```cpp
  if (edit == nullptr) return false;
  ```

8. Add tests for the fixes.
- In `tests/WaiveCoreTests.cpp`:
  - Test that `AudioAnalysisCache` get/put/eviction works correctly with the new iterator map.
  - Test that `AudioAnalysisSummary` returns empty for zero-sample-rate files.
- In `tests/WaiveUiTests.cpp`:
  - Test that `GridLine` struct correctly stores minor/major flags.

## Files Expected To Change
- `gui/src/tools/JobQueue.h`
- `gui/src/tools/JobQueue.cpp`
- `gui/src/ui/MixerChannelStrip.h`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/TrackLaneComponent.h`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/tools/AudioAnalysisCache.h`
- `gui/src/tools/AudioAnalysisCache.cpp`
- `gui/src/tools/AudioAnalysis.cpp`
- `gui/src/edit/EditSession.cpp`
- `tests/WaiveUiTests.cpp`
- `tests/WaiveCoreTests.cpp`

## Validation

```bash
cmake --build build --target Waive WaiveUiTests WaiveCoreTests -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- No race conditions in JobQueue timerCallback.
- MixerChannelStrip destructor safe against dangling track pointers.
- Grid line cache uses struct instead of bit-packing.
- AudioAnalysisCache uses O(1) removal via iterator map.
- All new tests pass.
- Build clean with no warnings.
