# Phase 04: UI/UX And Design Polish

## Objective
Improve usability and visual coherence after core workflow completion.

## Consolidated Inputs
- `phase_03_uiux_and_design_polish.md`
- `phase_08_ui_polish_and_design.md`
- Styling consistency items from `phase_06_critical_safety_fixes.md`

## Scope
- Responsive session toolbar layout.
- Position display improvements.
- Theme consistency cleanup.
- Better visual hierarchy and discoverability.
- Timeline/mixer visual clarity features.

## Implementation Tasks

1. Make session transport toolbar responsive.
- Replace rigid width slicing with adaptive layout (`FlexBox` or equivalent).
- Keep core transport actions always visible.
- Move secondary controls to wrapped row and/or overflow affordance on narrow widths.

2. Improve position display modes.
- Support bars/beats/ticks display when bars mode is enabled.
- Use monospaced font for stable numeric alignment.

3. Standardize theme usage in audited UI surfaces.
- Remove hardcoded colors/fonts in touched components.
- Use `getWaivePalette()` and `waive::Fonts` consistently.

4. Improve Tool Sidebar information architecture.
- Clear section grouping for tool, parameters, actions, preview, and model status.
- Improve status visibility (idle/running/success/failure).
- Add empty-state guidance for no-op plans.

5. Add track-level visual identity.
- Add deterministic track colors and apply them consistently in timeline/mixer/clip surfaces.

6. Add clip fade UX affordances.
- Add fade in/out handles and visual overlays.
- Enforce safe drag bounds and undoable edits.

7. Add library search/filter UX.
- Add inline search box and clear action.
- Filter visible file entries with low-latency updates.

8. Add/adjust tests.
- Resize behavior and control accessibility tests.
- Bars/beats display tests.
- Track-color assignment tests.
- Fade-handle boundary tests.

## Files Expected To Change
- `gui/src/ui/SessionComponent.h`
- `gui/src/ui/SessionComponent.cpp`
- `gui/src/ui/ToolSidebarComponent.cpp`
- `gui/src/ui/SchemaFormComponent.cpp`
- `gui/src/ui/TrackLaneComponent.h`
- `gui/src/ui/TrackLaneComponent.cpp`
- `gui/src/ui/MixerChannelStrip.h`
- `gui/src/ui/MixerChannelStrip.cpp`
- `gui/src/ui/ClipComponent.h`
- `gui/src/ui/ClipComponent.cpp`
- `gui/src/ui/LibraryComponent.h`
- `gui/src/ui/LibraryComponent.cpp`
- `tests/WaiveUiTests.cpp`

## Validation

```bash
cmake --build build -j
ctest --test-dir build -R WaiveUiTests --output-on-failure
```

Manual checks:
- Narrow/wide resize behavior.
- Tool Sidebar clarity/readability.
- Bars/beats position mode.
- Track-color/fade-handle visual quality.
- Library search responsiveness.

## Exit Criteria
- UI remains usable at constrained widths.
- Visual hierarchy is clear across key workflows.
- Theme conventions are consistently applied.
- New visual/interaction features are stable and tested.

