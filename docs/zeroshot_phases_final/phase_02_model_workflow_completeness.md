# Phase 02: Model Workflow Completeness

## Objective
Make model-backed tools fully operable from product UI without test-only hooks.

## Consolidated Inputs
- `phase_02_model_workflow_completeness.md`

## Scope
- Model install/uninstall/pin management in Tool Sidebar.
- Clear preflight gating for model-required tools.
- Persisted model settings and resilient UX messaging.

## Implementation Tasks

1. Add user-facing model manager section to Tool Sidebar.
- Show available models, installed versions, pinned version, install size, storage usage.
- Add controls for install/uninstall/pin/unpin/refresh.
- Expose quota and storage-directory controls (or at least status + edit actions).

2. Gate tool planning on model availability with actionable UX.
- If required model/version missing, disable `Plan`.
- Show clear remediation guidance and direct install action.

3. Improve status/error surfacing for model-backed plan failures.
- Preserve specific prepare-plan error text.
- Keep remediation hint visible until user changes context.

4. Persist and restore model preferences.
- Use `ModelManager` settings for pinned versions/quota/storage location.
- Rehydrate sidebar model state on startup.

5. Add regression coverage.
- UI tests for install/pin/uninstall flows via sidebar controls.
- Tests for plan enable/disable behavior by model availability.
- Persistence tests for manager settings.

## Files Expected To Change
- `gui/src/ui/ToolSidebarComponent.h`
- `gui/src/ui/ToolSidebarComponent.cpp`
- `gui/src/tools/ModelManager.h`
- `gui/src/tools/ModelManager.cpp`
- `tests/WaiveUiTests.cpp`
- `tests/WaiveCoreTests.cpp` (if settings persistence tested there)

## Validation

```bash
cmake --build build -j
ctest --test-dir build -R WaiveUiTests --output-on-failure
ctest --test-dir build -R WaiveCoreTests --output-on-failure
```

## Exit Criteria
- End users can manage models from UI.
- Missing-model failures are preempted or clearly recoverable.
- Model-backed tools run end-to-end in normal UI workflow.
- Settings persist correctly between launches.

