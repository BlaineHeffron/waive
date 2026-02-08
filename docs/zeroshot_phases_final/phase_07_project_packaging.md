# Phase 07: Project Packaging & Media Management

## Objective
Add "Collect and Save" functionality that copies all referenced audio files into the project folder, converts absolute paths to relative, and optionally packages the project as a zip archive. Also add "Remove Unused Media" to clean up orphaned audio files.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Context

### Current State
- Projects are saved as `.tracktionedit` XML files (Tracktion format)
- Audio files are referenced by absolute paths in the edit XML
- No mechanism to collect scattered audio files
- No way to share a project with all its audio

### Tracktion Engine File References
```cpp
// Audio clips reference source files via:
te::AudioClipBase& clip = ...;
juce::File sourceFile = clip.getSourceFileReference().getFile();

// Tracktion's Edit stores file references that can be resolved:
// te::SourceFileReference holds a path that can be absolute or relative to the edit file
```

## Implementation Tasks

### 1. Create ProjectPackager utility

Create `gui/src/util/ProjectPackager.h` and `gui/src/util/ProjectPackager.cpp`.

**API:**
```cpp
class ProjectPackager
{
public:
    struct CollectResult
    {
        int filesCopied = 0;
        int64 bytesCopied = 0;
        juce::StringArray errors;
    };

    // Collect all referenced audio into project_dir/Audio/
    static CollectResult collectAndSave (te::Edit& edit, const juce::File& projectDir);

    // Find audio files referenced by the edit that are outside the project directory
    static juce::Array<juce::File> findExternalMedia (te::Edit& edit, const juce::File& projectDir);

    // Find audio files in project directory that are NOT referenced by any clip
    static juce::Array<juce::File> findUnusedMedia (te::Edit& edit, const juce::File& projectDir);

    // Remove unused media files
    static int removeUnusedMedia (te::Edit& edit, const juce::File& projectDir);

    // Package project directory as a zip file
    static bool packageAsZip (const juce::File& projectDir, const juce::File& outputZip);
};
```

### 2. Implement collectAndSave

**Logic:**
1. Create `projectDir/Audio/` directory if it doesn't exist
2. Iterate all tracks → all clips → get source file references
3. For each file that lives outside `projectDir`:
   a. Copy it to `projectDir/Audio/<filename>` (handle name collisions by appending numbers)
   b. Update the clip's source file reference to point to the new location
4. Save the edit
5. Return count of files copied and any errors

**Important:** Use `juce::File::copyFileTo()` for copying. Handle large files gracefully.

### 3. Implement findUnusedMedia / removeUnusedMedia

**Logic:**
1. Collect all source file paths referenced by clips in the edit
2. Scan `projectDir/Audio/` for all audio files (.wav, .flac, .ogg, .aif, .aiff, .mp3)
3. Files in Audio/ that are NOT in the referenced set are "unused"
4. Remove moves files to a `projectDir/.trash/` directory (recoverable)

### 4. Implement packageAsZip

Use `juce::ZipFile::Builder` to create a zip containing:
- The `.tracktionedit` project file
- The `Audio/` directory with all collected media
- Any `.autosave` file if present

### 5. Add menu items

In `gui/src/MainComponent.cpp`, add to the File menu:
- "Collect and Save..." → runs collectAndSave, shows result dialog
- "Remove Unused Media..." → shows list of unused files, confirms before removal
- "Package as Zip..." → collectAndSave then packageAsZip, FileChooser for output path

Add command IDs for each:
- `cmdCollectAndSave`
- `cmdRemoveUnusedMedia`
- `cmdPackageAsZip`

### 6. Add commands for AI agent

In `engine/src/CommandHandler.cpp`:

**`collect_and_save`:**
- No params (operates on current edit)
- Returns `{ files_copied, bytes_copied }`

**`remove_unused_media`:**
- No params
- Returns `{ files_removed, bytes_freed }`

### 7. Add to CMakeLists.txt

Add ProjectPackager files to `gui/CMakeLists.txt`.

## Files Expected To Change
- `gui/src/util/ProjectPackager.h` (create)
- `gui/src/util/ProjectPackager.cpp` (create)
- `gui/src/MainComponent.h` (add command IDs)
- `gui/src/MainComponent.cpp` (add menu items, command handlers)
- `engine/src/CommandHandler.cpp` (add collect/cleanup commands)
- `gui/CMakeLists.txt` (add new files)

## Validation

```bash
# Build must succeed
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))

# All tests must still pass
ctest --test-dir build --output-on-failure
```

## Exit Criteria
- "Collect and Save" copies external audio into project/Audio/ and updates references
- "Remove Unused Media" identifies and removes orphaned files
- "Package as Zip" creates a portable zip archive
- Commands available for AI agent
- All existing tests still pass
