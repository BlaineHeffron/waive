# Phase 01: Security Hardening

## Objective
Eliminate path traversal, input validation, and authentication vulnerabilities identified in security audit.

## Scope
- Sanitize all user-controlled path components in ModelManager and tool artifact creation.
- Add authentication token to CommandServer.
- Validate file paths in insert_audio_clip and related commands.
- Pin Tracktion Engine to a specific commit hash instead of `develop` branch.
- Remove unused CURL dependency.

## Implementation Tasks

1. Add path sanitization utility.
- Create `gui/src/util/PathSanitizer.h` with:
  - `sanitizePathComponent(const juce::String&)` → rejects strings containing `..`, `/`, `\`, or null bytes.
  - `isWithinDirectory(const juce::File& candidate, const juce::File& allowedBase)` → resolves canonical paths and checks containment.
- Unit test both functions with adversarial inputs.

2. Sanitize ModelManager file paths.
- In `ModelManager::installModel()`, sanitize `modelID` and `selectedVersion` through `sanitizePathComponent()` before using as path components.
- In `ModelManager::uninstallModel()`, validate the resolved directory is within the expected storage directory before calling `deleteRecursively()`.
- Reject model IDs that don't match `[a-zA-Z0-9_-]+` pattern.

3. Sanitize tool artifact paths.
- In `StemSeparationTool.cpp` (and any other tool that creates artifact directories), sanitize `toolName` and `plan.planID` through `sanitizePathComponent()` before constructing artifact directory paths.

4. Add file path validation to CommandHandler.
- In `handleInsertAudioClip()`, after checking file existence, resolve the canonical path and reject files outside a configurable allowed-directories list.
- Add an `allowedMediaDirectories` configuration (default: user home + project directory).
- Apply the same validation to any other command that accepts file paths.

5. Add authentication token to CommandServer.
- On startup, generate a random 32-byte hex token and write it to a temp file with mode 0600.
- Require all incoming connections to send the token as the first message.
- Reject connections that don't authenticate within 5 seconds.
- Print the token file path to stdout on startup for client integration.

6. Pin Tracktion Engine version.
- In root `CMakeLists.txt`, change `GIT_TAG develop` to a specific commit hash from the current working version.
- Document the pinned version and update process in a comment.

7. Remove unused CURL dependency.
- Remove `find_package(CURL)` and `target_link_libraries(... CURL::libcurl)` from `gui/CMakeLists.txt` if CURL is not used in any source file.

## Files Expected To Change
- `gui/src/util/PathSanitizer.h` (NEW)
- `gui/src/tools/ModelManager.cpp`
- `gui/src/tools/StemSeparationTool.cpp`
- `engine/src/CommandHandler.cpp`
- `engine/src/CommandServer.h`
- `engine/src/CommandServer.cpp`
- `CMakeLists.txt`
- `gui/CMakeLists.txt`
- `tests/WaiveCoreTests.cpp`

## Validation

```bash
cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

Manual checks:
- Attempt path traversal via console command with `../../etc` as model ID — should be rejected.
- Attempt to insert audio clip with path outside allowed directories — should be rejected.
- Connect to command server without token — should be disconnected.

## Exit Criteria
- All path-controlled file operations validate inputs.
- Command server requires authentication.
- Tracktion Engine pinned to specific version.
- No regressions in existing tests.
