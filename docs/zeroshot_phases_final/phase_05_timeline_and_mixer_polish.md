# Phase 05: Timeline and Mixer Polish

## Objective
Bring the timeline and mixer up to professional DAW visual quality with improved meters, horizontal scrollbar, track colors, and responsive layout.

## Scope
- Professional mixer meters with scale markings and peak hold.
- Horizontal timeline scrollbar.
- Deterministic track color assignments.
- Adjustable track lane heights.
- Responsive transport toolbar layout.
- Stronger clip drag/drop visual feedback.

## Implementation Tasks

1. Improve mixer meter visualization.
- In `MixerChannelStrip::paint()`, replace the current 3px-wide simple bars with proper meters:
  - Width: 6px per channel (12px total for stereo).
  - Scale markings at -60, -40, -20, -10, -6, -3, 0, +6 dB (drawn as tick marks on the right edge).
  - Peak hold indicator: horizontal line that holds at peak for 2 seconds, then decays.
  - Color gradient: green below -12dB, yellow -12 to -3dB, red above -3dB.
  - Use `pal->meterNormal`, `pal->meterWarning`, `pal->meterClip` colors from palette.
- Add `peakHoldL`, `peakHoldR`, `peakHoldDecayCounter` members.

2. Add horizontal timeline scrollbar.
- Add a `juce::ScrollBar` at the bottom of the `TimelineComponent` area.
- Set range based on edit length (with some padding beyond last clip).
- Sync scrollbar position with `timeline.scrollOffsetSeconds`.
- On scrollbar change, update timeline scroll offset.
- On timeline scroll (wheel/drag), update scrollbar position.
- Use `pal->scrollbar` / `pal->scrollbarThumb` colors if available, otherwise use LookAndFeel defaults.

3. Add deterministic track colors.
- Define 12 track colors in `WaiveColours.h` as `trackColor1` through `trackColor12`.
- Assign color based on `trackIndex % 12`.
- Draw a 4px color strip on the left edge of each `TrackLaneComponent`.
- Use the same color for the corresponding `MixerChannelStrip` header background.
- Use the track color as clip background tint (blend 20% track color with `pal->clipDefault`).

4. Add adjustable track lane heights.
- Add Cmd+Scroll (or Ctrl+Scroll) on the track area to zoom track heights.
- Define min (60px), default (108px), and max (300px) track heights.
- Store current height in `TimelineComponent` and apply to all lanes uniformly.
- Add "Zoom Tracks to Fit" command (Cmd+Shift+F) that calculates height to fit all tracks in viewport.

5. Make transport toolbar responsive.
- Replace hardcoded `removeFromLeft(56)` / `removeFromLeft(180)` with `juce::FlexBox` layout.
- Primary row (always visible): play, stop, record, position display, tempo.
- Secondary row (visible when width > 900px): loop toggle, punch in/out, add markers, snap controls.
- When width < 900px, hide secondary controls or wrap to second row.

6. Add clip drag/drop visual feedback.
- When dragging a clip (`ClipComponent::mouseDrag()`), draw a semi-transparent ghost outline at the target position.
- When hovering a drag over the timeline from the library, show a preview rectangle at the drop position.
- Use `pal->primary` at 30% opacity for ghost outline.
- Show a vertical snap line when clip snaps to grid.

7. Add tests.
- Track color assignment consistency.
- Scrollbar range calculation.
- Track height zoom bounds.
- Responsive layout breakpoint behavior.

## Files Expected To Change
- `gui/src/ui/MixerChannelStrip.h`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/TimelineComponent.h`
- `gui/src/ui/TimelineComponent.cpp`
- `gui/src/ui/TrackLaneComponent.h`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/ui/ClipComponent.cpp`
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/theme/WaiveColours.h`
- `gui/src/theme/WaiveColours.cpp`
- `tests/WaiveUiTests.cpp`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

Manual checks:
- Mixer meters show green/yellow/red gradient with scale markings.
- Peak hold indicators visible and decay after 2 seconds.
- Horizontal scrollbar syncs with timeline pan.
- Each track has a distinct color stripe.
- Cmd+Scroll zooms track heights.
- Narrow window hides secondary transport controls gracefully.
- Dragging a clip shows ghost outline.

## Exit Criteria
- Mixer meters are professional quality with peak hold.
- Timeline has a functional horizontal scrollbar.
- Tracks have deterministic color coding.
- Track heights are zoomable.
- Transport toolbar adapts to window width.
- Drag operations show visual previews.
