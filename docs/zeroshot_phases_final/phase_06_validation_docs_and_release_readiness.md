# Phase 06: Validation, Documentation, And Release Readiness

## Objective
Validate integrated behavior across all prior phases and finalize release-quality documentation and CI signal.

## Consolidated Inputs
- `phase_05_validation_and_release_readiness.md`
- `phase_10_validation_and_docs.md`

## Scope
- Regression coverage expansion.
- Documentation accuracy.
- CI reliability and warning hygiene.
- Clean-build and manual smoke readiness.

## Implementation Tasks

1. Expand and verify regression matrix.
- Add/verify coverage for all new safety, workflow, UI, and performance paths.
- Ensure plan/apply/reject/cancel + undo/redo behavior remains deterministic.
- Keep tests non-interactive and CI-friendly.

2. Update architecture documentation.
- Reflect new command/workflow surface area.
- Document safety architecture changes (ID-based selection, async callback safety, rollback behavior).
- Document key performance architecture changes.

3. Update testing documentation.
- Refresh phase validation matrix.
- Document new helpers and regression patterns.

4. Update development conventions.
- Capture enforced rules from implementation:
  - no raw clip pointers in persistent selection state
  - no unsafe `this` capture in deferred callbacks
  - transaction safety expectations
  - repaint/timer discipline
  - shared tool utility usage

5. Validate CI and warning policy.
- Ensure all suites pass in CI from clean checkout.
- Ensure Waive-owned warnings are surfaced and addressed.
- Keep UI tests stable under `xvfb-run`.

6. Perform clean-build verification and smoke checklist.

```bash
rm -rf build
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Manual checklist includes:
- app launch
- transport/record
- mixer operations
- rename flows
- model-backed tools
- render/export
- save/load persistence
- crash-free rapid interaction

## Files Expected To Change
- `tests/WaiveUiTests.cpp`
- `tests/WaiveCoreTests.cpp`
- `docs/architecture.md`
- `docs/testing.md`
- `docs/dev_conventions.md`
- `.github/workflows/ci.yml` (if policy updates required)

## Exit Criteria
- All tests pass from clean build.
- Docs reflect actual behavior and architecture.
- CI is stable and meaningful.
- Manual smoke checks pass.
- Repository is ready for signoff.

