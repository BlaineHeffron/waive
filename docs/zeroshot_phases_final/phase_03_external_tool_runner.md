# Phase 03: External Tool Runner (Plugin System)

## Objective
Create a plugin system that loads external tool manifests (`.waive-tool.json` files), executes them as child processes, and integrates them into the existing Tool interface so they appear in the Tool Sidebar and can be used by the AI agent.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH). JUCE module .cpp files fail with `#error "Incorrect use of JUCE cpp file"` when PCH includes JuceHeader.h.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- `unique_ptr<T>` with incomplete T in header needs explicit destructor defined in .cpp where T is complete.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### Tool Interface (gui/src/tools/Tool.h)
```cpp
struct ToolDescription {
    juce::String name, displayName, version, description;
    juce::var inputSchema, defaultParams;
    juce::String modelRequirement;
};

struct ToolExecutionContext {
    EditSession& editSession;
    ProjectManager& projectManager;
    SessionComponent& sessionComponent;
    ModelManager& modelManager;
    juce::File projectCacheDirectory;
};

struct ToolPlanTask {
    juce::String jobName;
    std::function<ToolPlan (ProgressReporter&)> run;
};

class Tool {
public:
    virtual ~Tool() = default;
    virtual ToolDescription describe() const = 0;
    virtual juce::Result preparePlan (const ToolExecutionContext& context,
                                      const juce::var& params,
                                      ToolPlanTask& outTask) = 0;
    virtual juce::Result apply (const ToolExecutionContext& context,
                                const ToolPlan& plan) = 0;
};
```

### ToolDiff (gui/src/tools/ToolDiff.h)
- `ToolDiffEntry` has `type` (enum: setClipGain, renameTrack, setTrackVolume, setTrackPan, insertClip, removeClip, etc.), `trackIndex`, `clipIndex`, and `juce::var value`
- `ToolPlan` has `juce::String summary` and `juce::Array<ToolDiffEntry> changes`

### ToolRegistry (gui/src/tools/ToolRegistry.h/.cpp)
- `registerTool(unique_ptr<Tool>)` adds to the vector
- `findTool(name)` looks up by `describe().name`
- Constructor pre-registers 7 built-in tools

### StemSeparationTool pattern for audio export
The StemSeparationTool (gui/src/tools/StemSeparationTool.cpp) shows the pattern for exporting a selected clip to WAV:
- In `preparePlan()`, get the selected clip from context
- Use `te::Renderer` or the clip's source file to get the audio data
- Write to a temp WAV file for processing
- In the `run` lambda, execute the processing
- In `apply()`, insert output files as new clips

### Main.cpp structure
- `WaiveApplication` owns: `engine`, `editSession`, `jobQueue`, `projectManager`, `commandHandler`, `undoableHandler`, `appProperties`, `aiSettings`, `toolRegistry`, `aiAgent`, `mainWindow`
- All created in `initialise()`, destroyed in `shutdown()` in reverse order

## Implementation Tasks

### 1. Create `gui/src/tools/ExternalToolManifest.h`

```cpp
#pragma once

#include <JuceHeader.h>
#include <vector>
#include <optional>

namespace waive
{

struct ExternalToolManifest
{
    juce::String name;
    juce::String displayName;
    juce::String version;
    juce::String description;
    juce::var inputSchema;
    juce::var defaultParams;
    juce::String executable;       // e.g. "python3"
    juce::StringArray arguments;   // e.g. ["script.py"]
    juce::File baseDirectory;      // directory where the manifest lives
    int timeoutMs = 300000;        // 5 minutes default
    bool acceptsAudioInput = false;
    bool producesAudioOutput = false;
};

/** Parse a single .waive-tool.json manifest file. */
std::optional<ExternalToolManifest> parseManifest (const juce::File& manifestFile);

/** Scan a directory for all *.waive-tool.json files and parse them. */
std::vector<ExternalToolManifest> scanToolDirectory (const juce::File& directory);

} // namespace waive
```

### 2. Create `gui/src/tools/ExternalToolManifest.cpp`

Implement `parseManifest()`:
- Read the file, parse with `juce::JSON::parse()`
- Extract all fields from the JSON object into the struct
- `executable`: string field (required)
- `arguments`: JSON array of strings → `juce::StringArray`
- `inputSchema`: the raw JSON object (keep as `juce::var`)
- `defaultParams`: optional JSON object
- `baseDirectory`: set to `manifestFile.getParentDirectory()`
- `acceptsAudioInput`, `producesAudioOutput`: boolean fields, default false
- `timeoutMs`: integer field, default 300000
- Return `std::nullopt` if the file can't be read, parsed, or is missing required fields (`name`, `executable`)

Implement `scanToolDirectory()`:
- Use `directory.findChildFiles (results, juce::File::findFiles, false, "*.waive-tool.json")`
- Call `parseManifest()` on each, collect successful results into vector

Expected manifest JSON format:
```json
{
  "name": "timbre_transfer",
  "displayName": "Timbre Transfer",
  "version": "1.0.0",
  "description": "Transform audio timbre",
  "inputSchema": { "type": "object", "properties": {...}, "required": [...] },
  "executable": "python3",
  "arguments": ["timbre_transfer.py"],
  "acceptsAudioInput": true,
  "producesAudioOutput": true,
  "timeoutMs": 600000
}
```

### 3. Create `gui/src/tools/ExternalToolRunner.h`

```cpp
#pragma once

#include <JuceHeader.h>
#include <vector>

#include "ExternalToolManifest.h"

namespace waive
{

class ProgressReporter;

struct ExternalToolOutput
{
    bool success = false;
    juce::String message;
    juce::var resultData;          // parsed result.json content
    juce::File outputAudioFile;    // path to output.wav if produced
};

class ExternalToolRunner
{
public:
    ExternalToolRunner();

    /** Run an external tool with given params. Blocks until done. */
    ExternalToolOutput run (const ExternalToolManifest& manifest,
                            const juce::var& params,
                            const juce::File& inputAudioFile,
                            ProgressReporter& reporter);

    /** Get the primary tools directory (app data). */
    juce::File getToolsDirectory() const;

    /** Add an additional directory to scan for tools. */
    void addToolsDirectory (const juce::File& dir);

    /** Get all configured tool directories. */
    const std::vector<juce::File>& getToolsDirectories() const { return toolsDirs; }

private:
    std::vector<juce::File> toolsDirs;
};

} // namespace waive
```

### 4. Create `gui/src/tools/ExternalToolRunner.cpp`

Implement `ExternalToolRunner()`:
- Initialize with the default tools directory: `getToolsDirectory()`

Implement `getToolsDirectory()`:
- Return `juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Waive").getChildFile ("tools")`

Implement `addToolsDirectory()`:
- Push to `toolsDirs` vector

Implement `run()`:
1. Create a unique temp working directory:
   ```cpp
   auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("waive_tool_" + juce::String (juce::Random::getSystemRandom().nextInt64()));
   tempDir.createDirectory();
   auto inputDir = tempDir.getChildFile ("input");
   auto outputDir = tempDir.getChildFile ("output");
   inputDir.createDirectory();
   outputDir.createDirectory();
   ```

2. Write `params.json` to inputDir:
   ```cpp
   inputDir.getChildFile ("params.json")
       .replaceWithText (juce::JSON::toString (params));
   ```

3. If `inputAudioFile.existsAsFile()` and `manifest.acceptsAudioInput`:
   - Copy input audio to `inputDir.getChildFile ("input.wav")`

4. Build command line:
   ```cpp
   juce::StringArray cmdLine;
   cmdLine.add (manifest.executable);
   cmdLine.addArray (manifest.arguments);
   cmdLine.add ("--input-dir");
   cmdLine.add (inputDir.getFullPathName());
   cmdLine.add ("--output-dir");
   cmdLine.add (outputDir.getFullPathName());
   ```

5. Launch `juce::ChildProcess`:
   ```cpp
   juce::ChildProcess process;
   if (! process.start (cmdLine, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr,
                         manifest.baseDirectory))
   {
       // return error
   }
   ```

6. Poll for completion with timeout:
   ```cpp
   auto startTime = juce::Time::getMillisecondCounter();
   while (process.isRunning())
   {
       if (juce::Time::getMillisecondCounter() - startTime > (uint32) manifest.timeoutMs)
       {
           process.kill();
           // return timeout error
       }
       juce::Thread::sleep (500);
   }
   ```

7. Read results:
   - Parse `outputDir.getChildFile ("result.json")` with `juce::JSON::parse()`
   - Check for `outputDir.getChildFile ("output.wav")`
   - Build and return `ExternalToolOutput`

8. Clean up: do NOT delete temp dir immediately — the caller may need the output files. The caller is responsible for cleanup, or files get cleaned up on OS temp cleanup.

### 5. Create `gui/src/tools/ExternalTool.h`

```cpp
#pragma once

#include "Tool.h"
#include "ExternalToolManifest.h"

namespace waive
{

class ExternalToolRunner;

/** Adapts an ExternalToolManifest into the waive::Tool interface. */
class ExternalTool : public Tool
{
public:
    ExternalTool (const ExternalToolManifest& manifest, ExternalToolRunner& runner);

    ToolDescription describe() const override;
    juce::Result preparePlan (const ToolExecutionContext& context,
                              const juce::var& params,
                              ToolPlanTask& outTask) override;
    juce::Result apply (const ToolExecutionContext& context,
                        const ToolPlan& plan) override;

private:
    ExternalToolManifest manifest;
    ExternalToolRunner& runner;
};

} // namespace waive
```

### 6. Create `gui/src/tools/ExternalTool.cpp`

Include: `ExternalTool.h`, `ExternalToolRunner.h`, `ToolDiff.h`, `JobQueue.h` (for `ProgressReporter`)

**`describe()`:**
- Return `ToolDescription` populated from the manifest fields
- Map: `name` → `manifest.name`, `displayName` → `manifest.displayName`, etc.
- `inputSchema` → `manifest.inputSchema`, `defaultParams` → `manifest.defaultParams`
- `modelRequirement` → empty string (external tools don't need models)

**`preparePlan()`:**
- If `manifest.acceptsAudioInput`:
  - Get the selected clip from context (same pattern as StemSeparationTool)
  - Get the clip's source audio file path
  - Store in a local variable for the lambda capture
- Set up `outTask.jobName` = `manifest.displayName`
- Set up `outTask.run` lambda:
  - Call `runner.run (manifest, params, inputAudioFile, reporter)`
  - If failed, return empty `ToolPlan` with error summary
  - Build `ToolPlan`:
    - `summary` = tool output message
    - If `manifest.producesAudioOutput` and output audio exists:
      - Add a `ToolDiffEntry` with type `insertClip`, containing the output file path in `value`
    - Store the output in the plan's metadata so `apply()` can access it
- Return `juce::Result::ok()`

**`apply()`:**
- If `manifest.producesAudioOutput` and plan has insertClip entries:
  - Get the output audio file path from the plan
  - Insert it as a clip on the appropriate track (same pattern as StemSeparationTool's apply)
- Return `juce::Result::ok()`

### 7. Modify `gui/src/tools/ToolRegistry.h`

Add forward declaration and new method:
```cpp
class ExternalToolRunner;
```

Add public method:
```cpp
/** Scan directories and register any external tools found. */
void scanAndRegisterExternalTools (ExternalToolRunner& runner);
```

### 8. Modify `gui/src/tools/ToolRegistry.cpp`

Add includes:
```cpp
#include "ExternalToolManifest.h"
#include "ExternalToolRunner.h"
#include "ExternalTool.h"
```

Implement:
```cpp
void ToolRegistry::scanAndRegisterExternalTools (ExternalToolRunner& runner)
{
    for (auto& dir : runner.getToolsDirectories())
    {
        auto manifests = scanToolDirectory (dir);
        for (auto& manifest : manifests)
        {
            // Don't register duplicates
            if (findTool (manifest.name) == nullptr)
                registerTool (std::make_unique<ExternalTool> (manifest, runner));
        }
    }
}
```

### 9. Modify `gui/src/Main.cpp`

Add includes:
```cpp
#include "ExternalToolRunner.h"
```

Add member to `WaiveApplication`:
```cpp
std::unique_ptr<waive::ExternalToolRunner> externalToolRunner;
```

In `initialise()`, after creating `toolRegistry` and before creating `aiAgent`:
```cpp
externalToolRunner = std::make_unique<waive::ExternalToolRunner>();

// Add built-in tools directory (next to executable)
auto builtInToolsDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                           .getParentDirectory().getParentDirectory().getChildFile ("tools");
if (builtInToolsDir.isDirectory())
    externalToolRunner->addToolsDirectory (builtInToolsDir);

// Add user tools directory
externalToolRunner->addToolsDirectory (externalToolRunner->getToolsDirectory());

// Scan and register external tools
toolRegistry->scanAndRegisterExternalTools (*externalToolRunner);
```

In `shutdown()`, add cleanup (after `aiAgent.reset()`, before `toolRegistry.reset()`):
```cpp
externalToolRunner.reset();
```

### 10. Modify `gui/CMakeLists.txt`

In the `# Tools` section, add after the existing tool files:
```
    src/tools/ExternalToolManifest.h
    src/tools/ExternalToolManifest.cpp
    src/tools/ExternalToolRunner.h
    src/tools/ExternalToolRunner.cpp
    src/tools/ExternalTool.h
    src/tools/ExternalTool.cpp
```

## Files Expected To Change
- `gui/src/tools/ExternalToolManifest.h` (NEW)
- `gui/src/tools/ExternalToolManifest.cpp` (NEW)
- `gui/src/tools/ExternalToolRunner.h` (NEW)
- `gui/src/tools/ExternalToolRunner.cpp` (NEW)
- `gui/src/tools/ExternalTool.h` (NEW)
- `gui/src/tools/ExternalTool.cpp` (NEW)
- `gui/src/tools/ToolRegistry.h`
- `gui/src/tools/ToolRegistry.cpp`
- `gui/src/Main.cpp`
- `gui/CMakeLists.txt`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- ExternalToolManifest correctly parses `.waive-tool.json` files.
- ExternalToolRunner can launch child processes with proper I/O contract.
- ExternalTool adapts manifests to the Tool interface (describe, preparePlan, apply).
- ToolRegistry scans directories and registers external tools without duplicates.
- Main.cpp creates the runner, adds search directories, and scans on startup.
- External tools appear in the Tool Sidebar.
- Build compiles with no errors.
