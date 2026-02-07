# Phase 05: Enable AI Agent to Execute tool_* Commands

## Objective
Replace the `tool_*` error stub in AiAgent with real execution that runs the full Plan/Apply workflow for any registered tool. Also update the system prompt to tell the LLM about the new capabilities.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH). JUCE module .cpp files fail with `#error "Incorrect use of JUCE cpp file"` when PCH includes JuceHeader.h.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- `unique_ptr<T>` with incomplete T in header needs explicit destructor defined in .cpp where T is complete.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.

## Architecture Context

### AiAgent (gui/src/ai/AiAgent.h/.cpp)
- Owns: `AiSettings& settings`, `UndoableCommandHandler& commandHandler`, `ToolRegistry& toolRegistry`, `JobQueue& jobQueue`
- `executeToolCall()` dispatches:
  - `cmd_*` → `executeCommand()` (runs on message thread via `callAsync` + `WaitableEvent`)
  - `tool_*` → currently returns error stub: `"High-level tool execution is not yet supported from AI chat."`
- The conversation loop runs on a background thread (via JobQueue)
- `executeCommand()` pattern: build command JSON, dispatch on message thread with `callAsync`, wait with `WaitableEvent`

### Tool Interface (gui/src/tools/Tool.h)
- `Tool::preparePlan(context, params, outTask)` → sets up `ToolPlanTask` with a `run` lambda
- `ToolPlanTask::run(reporter)` → returns `ToolPlan` (runs on background thread)
- `Tool::apply(context, plan)` → applies the plan to the edit (must run on message thread)
- `ToolExecutionContext` needs: `EditSession&`, `ProjectManager&`, `SessionComponent&`, `ModelManager&`, `juce::File projectCacheDirectory`

### MainComponent (gui/src/MainComponent.h/.cpp)
- Owns: `modelManager` (unique_ptr<ModelManager>), `sessionComponent`, etc.
- Has `getSessionComponentForTesting()` accessor
- Does NOT currently expose `modelManager` — we need to add an accessor

### Main.cpp (gui/src/Main.cpp)
- `WaiveApplication` creates aiAgent with: `*aiSettings, *undoableHandler, *toolRegistry, *jobQueue`
- Creates mainWindow which owns MainComponent
- Has access to `editSession`, `projectManager`

### ProgressReporter
- Defined in `gui/src/tools/JobQueue.h`
- Has `setProgress(float)`, `setStatus(String)`, `isCancelled()` methods
- Used by tool tasks to report progress

## Implementation Tasks

### 1. Modify `gui/src/MainComponent.h` — Add ModelManager accessor

Add a public method:
```cpp
waive::ModelManager* getModelManager() { return modelManager.get(); }
```

### 2. Modify `gui/src/ai/AiAgent.h` — Add tool context provider

Add includes/forward declarations at the top:
```cpp
#include "Tool.h"  // for ToolExecutionContext
```

Add a public type alias and setter method:
```cpp
using ToolContextProvider = std::function<waive::ToolExecutionContext()>;

/** Set a callback that provides the execution context for tool_* calls. */
void setToolContextProvider (ToolContextProvider provider);
```

Add private members:
```cpp
ToolContextProvider toolContextProvider;
juce::String executeTool (const juce::String& toolName, const juce::var& args);
```

### 3. Modify `gui/src/ai/AiAgent.cpp` — Implement tool execution

Add include:
```cpp
#include "Tool.h"
```

Implement the setter:
```cpp
void AiAgent::setToolContextProvider (ToolContextProvider provider)
{
    toolContextProvider = std::move (provider);
}
```

Replace the `tool_*` branch in `executeToolCall()`:
```cpp
else if (call.name.startsWith ("tool_"))
{
    auto toolName = call.name.substring (5);  // strip "tool_" prefix
    return executeTool (toolName, call.arguments);
}
```

Implement `executeTool()`:
```cpp
juce::String AiAgent::executeTool (const juce::String& toolName, const juce::var& args)
{
    if (! toolContextProvider)
        return "{\"status\":\"error\",\"message\":\"Tool execution context not configured.\"}";

    // Find the tool
    auto* tool = toolRegistry.findTool (toolName);
    if (tool == nullptr)
        return "{\"status\":\"error\",\"message\":\"Unknown tool: " + toolName + "\"}";

    // Step 1: preparePlan on message thread (needs UI/edit access)
    waive::ToolPlanTask task;
    juce::Result prepareResult = juce::Result::fail ("Not executed");

    {
        juce::WaitableEvent done;
        juce::MessageManager::callAsync ([&]
        {
            auto context = toolContextProvider();
            prepareResult = tool->preparePlan (context, args, task);
            done.signal();
        });
        done.wait (30000);  // 30s timeout for plan preparation
    }

    if (prepareResult.failed())
        return "{\"status\":\"error\",\"message\":\"Plan preparation failed: "
               + prepareResult.getErrorMessage().replace ("\"", "\\\"") + "\"}";

    // Step 2: run task on current thread (already background)
    // Create a simple reporter
    class SimpleReporter : public waive::ProgressReporter
    {
    public:
        void setProgress (float) override {}
        void setStatus (const juce::String&) override {}
        bool isCancelled() const override { return false; }
    };

    SimpleReporter reporter;
    auto plan = task.run (reporter);

    // Step 3: apply on message thread
    juce::Result applyResult = juce::Result::fail ("Not executed");

    {
        juce::WaitableEvent done;
        juce::MessageManager::callAsync ([&]
        {
            auto context = toolContextProvider();
            applyResult = tool->apply (context, plan);
            done.signal();
        });
        done.wait (30000);  // 30s timeout for apply
    }

    if (applyResult.failed())
        return "{\"status\":\"error\",\"message\":\"Apply failed: "
               + applyResult.getErrorMessage().replace ("\"", "\\\"") + "\"}";

    // Build success response
    auto* result = new juce::DynamicObject();
    result->setProperty ("status", "ok");
    result->setProperty ("summary", plan.summary);
    result->setProperty ("changes", plan.changes.size());
    return juce::JSON::toString (juce::var (result));
}
```

**Important notes on the SimpleReporter:**
- Check if `ProgressReporter` is a class or interface in `JobQueue.h`. If it has pure virtual methods, the `SimpleReporter` must implement all of them.
- If `ProgressReporter` is not suitable for subclassing like this, use a different approach — e.g., create a dummy reporter or pass through the one from the outer job.

**Alternative approach if ProgressReporter isn't easily subclassable:**
- The outer `runConversationLoop()` is already running inside a JobQueue task with a `ProgressReporter&`
- Capture that reporter and pass it to `task.run()` instead of creating a new one

### 4. Modify `gui/src/Main.cpp` — Wire the tool context provider

After creating `mainWindow` (at the end of `initialise()`), add:
```cpp
aiAgent->setToolContextProvider ([this]() -> waive::ToolExecutionContext
{
    auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
    jassert (mc != nullptr);

    auto cacheDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("Waive").getChildFile ("cache");
    cacheDir.createDirectory();

    return { *editSession, *projectManager, mc->getSessionComponentForTesting(),
             *mc->getModelManager(), cacheDir };
});
```

Add `#include "Tool.h"` if not already included (needed for `ToolExecutionContext`).

### 5. Modify `gui/src/ai/AiToolSchema.cpp` — Update system prompt

In `generateSystemPrompt()`, update the returned string to include information about tool_* capabilities. Add to the guidelines section:

```
"- tool_* tools run a Plan/Apply workflow: they analyze the current selection, preview changes, then apply them.\n"
"- Available tool_* tools include audio processing (normalize, stem separation, gain staging, etc.).\n"
"- If external tools are installed, additional tool_* commands may be available (e.g., timbre transfer, music generation).\n"
"- When using tool_* commands, the tool operates on the currently selected clips/tracks.\n"
```

Add these lines after the existing `"- Do not invent file paths..."` line and before the closing `;\n`.

## Files Expected To Change
- `gui/src/MainComponent.h`
- `gui/src/ai/AiAgent.h`
- `gui/src/ai/AiAgent.cpp`
- `gui/src/Main.cpp`
- `gui/src/ai/AiToolSchema.cpp`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- `tool_*` calls in AI chat now execute the full Plan/Apply workflow instead of returning an error.
- MainComponent exposes `getModelManager()` accessor.
- Tool context provider is wired in Main.cpp after window creation.
- System prompt informs the LLM about tool_* capabilities.
- The execution runs preparePlan on message thread, task.run on background thread, apply on message thread.
- Errors during any phase are properly reported back to the LLM as JSON.
- Build compiles with no errors.
