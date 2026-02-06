# Phase 05: Performance And Scalability

## Objective
Reduce avoidable CPU work and improve responsiveness for large sessions and repeated tool runs.

## Consolidated Inputs
- `phase_04_performance_and_scalability.md`
- `phase_09_performance_optimization.md`

## Scope
- Tool planning complexity reduction.
- Repaint/layout throttling and targeting.
- Timer strategy improvements.
- Optional analysis caching and instrumentation.

## Implementation Tasks

1. Consolidate clip-to-track lookup logic.
- Introduce shared clip-to-track index map utility.
- Remove duplicate per-tool O(tracks * clips) scan helpers.
- Update all tool planning paths to use the shared map.

2. Optimize mixer strip repaints.
- Repaint only meter area when levels change above threshold.
- Skip redundant control-value pushes if no meaningful change.

3. Optimize playhead repaint strategy.
- Repaint only old/new playhead line regions.
- Avoid full overlay repaint every timer tick.

4. Gate track-lane layout and repaint by dirty state.
- Rebuild/layout/repaint only on structural or viewport changes.
- Track scroll/zoom deltas explicitly.

5. Tune timers and update cadence.
- Reduce non-critical timer frequencies where safe.
- Avoid timer proliferation causing unnecessary UI churn.

6. Add analysis-result caching (project-aware).
- Cache results keyed by file identity + params.
- Reuse across repeated plan operations in same project.

7. Add profiling instrumentation for debug builds.
- Log per-tool plan timing and analyzed item counts.
- Keep instrumentation lightweight and removable/toggleable.

8. Add regression/perf validation.
- Ensure tool outputs and undo/redo semantics remain unchanged.
- Confirm measurable improvement on larger sessions.

## Files Expected To Change
- `gui/src/tools/AudioAnalysis.h`
- `gui/src/tools/AudioAnalysis.cpp`
- `gui/src/tools/NormalizeSelectedClipsTool.cpp`
- `gui/src/tools/RenameTracksFromClipsTool.cpp`
- `gui/src/tools/GainStageSelectedTracksTool.cpp`
- `gui/src/tools/DetectSilenceAndCutRegionsTool.cpp`
- `gui/src/tools/AlignClipsByTransientTool.cpp`
- `gui/src/tools/StemSeparationTool.cpp`
- `gui/src/tools/AutoMixSuggestionsTool.cpp`
- `gui/src/ui/MixerChannelStrip.h`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/PlayheadComponent.h`
- `gui/src/ui/PlayheadComponent.cpp`
- `gui/src/ui/TrackLaneComponent.h`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/ui/SessionComponent.cpp`
- `tests/WaiveUiTests.cpp`

## Validation

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Profiling checks:
- Idle CPU in larger session (8+ tracks, multiple clips).
- Tool plan latency before/after.
- UI smoothness during playback and editing.

## Exit Criteria
- Duplicate lookup paths removed.
- High-frequency repaint hotspots are bounded.
- Tool behavior remains functionally identical.
- Measurable CPU/latency improvements are observed.

