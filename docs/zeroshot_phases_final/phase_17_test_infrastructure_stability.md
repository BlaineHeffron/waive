# Phase 17: Test Infrastructure & Stability

## Objective
Fix the crashing/incomplete WaiveUiTests, add targeted unit tests for untested high-value modules (AI agent, tool implementations, ExternalToolRunner, CommandHandler), and create a Python test scaffold so CI stops silently skipping external tool tests. The goal is a green CI pipeline with meaningful coverage of the most critical paths.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### Current test files
- **`tests/WaiveCoreTests.cpp`** (~421 lines, 11 tests) — Tests EditSession, ClipEditActions, ModelManager, PathSanitizer, AudioAnalysisCache. All PASS.
- **`tests/WaiveUiTests.cpp`** (~2059 lines, 13 test suites) — Tests UI components, tool framework, plugin routing, transport, etc. FAILS — output stops mid-execution, likely segfault or unhandled exception.
- **`tests/CMakeLists.txt`** — Two test executables: `WaiveCoreTests` and `WaiveUiTests`.

### Untested modules (high-value gaps)
1. **CommandHandler** — 31+ commands, zero direct unit tests (only tested indirectly through UI tests)
2. **AiAgent** — Chat loop, tool call dispatch, conversation management — no tests at all
3. **AiToolSchema** — Tool definition generation — no tests
4. **ExternalToolRunner** — Subprocess execution — no tests
5. **ExternalToolManifest** — Manifest parsing/validation — no tests
6. **Built-in tools** (7 tools) — No unit tests for any tool's preparePlan()/apply()
7. **ProjectManager** — Project lifecycle — no tests
8. **ChatHistorySerializer** — Serialization — no tests

### CI pipeline (`.github/workflows/ci.yml`)
- Python test job checks for `ai/requirements.txt` and `ai/tests/` directories, but NEITHER EXISTS — tests silently skipped
- Python tools are in `tools/*/` (e.g., `tools/timbre_transfer/`, `tools/music_generation/`)

### Test patterns used
- Custom `expect(bool, string)` function (not a test framework)
- Tests structured as functions called from `main()`
- Return codes: 0 = pass, 1 = fail
- Tests run under `xvfb-run` in CI for headless GUI testing

## Implementation Tasks

### 1. Debug and fix WaiveUiTests crash

Read `tests/WaiveUiTests.cpp` and identify the failing test. The output stops after "runPhase6SafetyArchitectureRegression: PASS" — the next test is likely the one that crashes.

**Approach:**
1. Add try/catch blocks around each test function call in `main()` to isolate which test crashes
2. Add `std::cout.flush()` / `std::cerr.flush()` after each test to ensure output isn't lost
3. If a test segfaults, wrap the crashing test in a signal handler or guard
4. Common crash causes in JUCE UI tests:
   - Accessing destroyed components
   - Timer callbacks firing during teardown
   - Plugin scanning with no audio device
   - JUCE MessageManager not initialized

In the `main()` function, wrap each test call:

```cpp
int main()
{
    // ... existing setup ...

    int failures = 0;
    auto runTest = [&] (const char* name, std::function<void()> fn)
    {
        std::cout << "Running " << name << "..." << std::flush;
        try
        {
            fn();
            std::cout << " PASS" << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << " FAIL: " << e.what() << std::endl;
            ++failures;
        }
        catch (...)
        {
            std::cerr << " FAIL: unknown exception" << std::endl;
            ++failures;
        }
        std::cout.flush();
        std::cerr.flush();
    };

    runTest ("Phase5PerformanceOptimization", [&] { runPhase5PerformanceOptimizationTests (...); });
    runTest ("Phase6SafetyArchitecture", [&] { runPhase6SafetyArchitectureRegression (...); });
    // ... all other tests ...

    std::cout << "\n" << (failures == 0 ? "All tests passed" : std::to_string(failures) + " test(s) failed") << std::endl;
    return failures > 0 ? 1 : 0;
}
```

### 2. Create `tests/WaiveCommandTests.cpp` — Unit tests for CommandHandler

Test all CommandHandler commands directly (no UI, no undo layer):

```cpp
#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "CommandHandler.h"

namespace te = tracktion;

namespace
{
void expect (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

juce::var parseResponse (const juce::String& json)
{
    return juce::JSON::parse (json);
}

juce::String makeCommand (const juce::String& action, const juce::var& params = {})
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("action", action);
    if (params.isObject())
        for (auto& p : *params.getDynamicObject())
            obj->setProperty (p.name, p.value);
    return juce::JSON::toString (juce::var (obj));
}

} // namespace

void runCommandHandlerTests (te::Edit& edit)
{
    CommandHandler handler (edit);
    handler.setAllowedMediaDirectories ({ juce::File::getSpecialLocation (juce::File::tempDirectory) });

    // Test: ping
    {
        auto resp = parseResponse (handler.handleCommand (makeCommand ("ping")));
        expect (resp["status"].toString() == "ok", "ping should return ok");
    }

    // Test: add_track
    {
        auto resp = parseResponse (handler.handleCommand (makeCommand ("add_track")));
        expect (resp["status"].toString() == "ok", "add_track should return ok");
    }

    // Test: get_tracks returns the new track
    {
        auto resp = parseResponse (handler.handleCommand (makeCommand ("get_tracks")));
        expect (resp["status"].toString() == "ok", "get_tracks should return ok");
        auto tracks = resp["tracks"];
        expect (tracks.size() > 0, "Should have at least one track");
    }

    // Test: set_track_volume
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("track_id", 0);
        p->setProperty ("volume_db", -6.0);
        auto resp = parseResponse (handler.handleCommand (makeCommand ("set_track_volume", juce::var (p))));
        expect (resp["status"].toString() == "ok", "set_track_volume should work");
    }

    // Test: set_track_pan
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("track_id", 0);
        p->setProperty ("pan", 0.5);
        auto resp = parseResponse (handler.handleCommand (makeCommand ("set_track_pan", juce::var (p))));
        expect (resp["status"].toString() == "ok", "set_track_pan should work");
    }

    // Test: rename_track
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("track_id", 0);
        p->setProperty ("name", "Test Track");
        auto resp = parseResponse (handler.handleCommand (makeCommand ("rename_track", juce::var (p))));
        expect (resp["status"].toString() == "ok", "rename_track should work");
    }

    // Test: solo_track
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("track_id", 0);
        p->setProperty ("solo", true);
        auto resp = parseResponse (handler.handleCommand (makeCommand ("solo_track", juce::var (p))));
        expect (resp["status"].toString() == "ok", "solo_track should work");
    }

    // Test: mute_track
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("track_id", 0);
        p->setProperty ("mute", true);
        auto resp = parseResponse (handler.handleCommand (makeCommand ("mute_track", juce::var (p))));
        expect (resp["status"].toString() == "ok", "mute_track should work");
    }

    // Test: transport_play / transport_stop
    {
        auto resp1 = parseResponse (handler.handleCommand (makeCommand ("transport_play")));
        expect (resp1["status"].toString() == "ok", "transport_play should work");

        auto resp2 = parseResponse (handler.handleCommand (makeCommand ("transport_stop")));
        expect (resp2["status"].toString() == "ok", "transport_stop should work");
    }

    // Test: set_tempo
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("bpm", 140.0);
        auto resp = parseResponse (handler.handleCommand (makeCommand ("set_tempo", juce::var (p))));
        expect (resp["status"].toString() == "ok", "set_tempo should work");
    }

    // Test: get_transport_state
    {
        auto resp = parseResponse (handler.handleCommand (makeCommand ("get_transport_state")));
        expect (resp["status"].toString() == "ok", "get_transport_state should work");
        expect (resp.hasProperty ("bpm"), "Should have bpm field");
    }

    // Test: invalid command
    {
        auto resp = parseResponse (handler.handleCommand (makeCommand ("nonexistent_command")));
        expect (resp["status"].toString() == "error", "Unknown command should error");
    }

    // Test: invalid track_id
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("track_id", 999);
        p->setProperty ("volume_db", 0.0);
        auto resp = parseResponse (handler.handleCommand (makeCommand ("set_track_volume", juce::var (p))));
        expect (resp["status"].toString() == "error", "Invalid track_id should error");
    }

    // Test: remove_track
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("track_id", 0);
        auto resp = parseResponse (handler.handleCommand (makeCommand ("remove_track", juce::var (p))));
        expect (resp["status"].toString() == "ok", "remove_track should work");
    }
}
```

### 3. Create `tests/WaiveToolSchemaTests.cpp` — Test AI tool schema generation

```cpp
#include <JuceHeader.h>
#include "AiToolSchema.h"
#include "ToolRegistry.h"

namespace
{
void expect (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}
} // namespace

void runToolSchemaTests()
{
    // Test: generateCommandDefinitions returns non-empty
    {
        auto defs = waive::generateCommandDefinitions();
        expect (! defs.empty(), "Should have command definitions");
        expect (defs.size() >= 20, "Should have at least 20 command definitions, got " + std::to_string (defs.size()));
    }

    // Test: each definition has required fields
    {
        auto defs = waive::generateCommandDefinitions();
        for (auto& d : defs)
        {
            expect (d.name.isNotEmpty(), "Definition name should not be empty");
            expect (d.name.startsWith ("cmd_"), "Command def should start with cmd_: " + d.name.toStdString());
            expect (d.description.isNotEmpty(), "Description should not be empty for: " + d.name.toStdString());
            expect (d.inputSchema.isObject(), "Input schema should be an object for: " + d.name.toStdString());
        }
    }

    // Test: no duplicate names
    {
        auto defs = waive::generateCommandDefinitions();
        std::set<std::string> names;
        for (auto& d : defs)
        {
            auto name = d.name.toStdString();
            expect (names.find (name) == names.end(), "Duplicate definition name: " + name);
            names.insert (name);
        }
    }

    // Test: generateSystemPrompt returns non-empty
    {
        auto prompt = waive::generateSystemPrompt();
        expect (prompt.isNotEmpty(), "System prompt should not be empty");
        expect (prompt.contains ("Waive"), "System prompt should mention Waive");
    }
}
```

### 4. Create `tests/WaiveExternalToolTests.cpp` — Test manifest parsing

```cpp
#include <JuceHeader.h>
#include "ExternalToolManifest.h"

namespace
{
void expect (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}
} // namespace

void runExternalToolManifestTests()
{
    // Test: valid manifest parses correctly
    {
        auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("waive_test_tool");
        tempDir.createDirectory();

        auto manifestFile = tempDir.getChildFile (".waive-tool.json");
        manifestFile.replaceWithText (R"({
            "name": "test_tool",
            "displayName": "Test Tool",
            "version": "1.0.0",
            "description": "A test tool",
            "inputSchema": {"type": "object"},
            "executable": "python3",
            "arguments": ["main.py"],
            "acceptsAudioInput": true,
            "producesAudioOutput": false,
            "timeoutMs": 30000
        })");

        auto manifest = waive::ExternalToolManifest::loadFromFile (manifestFile);
        expect (manifest.has_value(), "Should parse valid manifest");
        expect (manifest->name == "test_tool", "Name should match");
        expect (manifest->displayName == "Test Tool", "Display name should match");
        expect (manifest->executable == "python3", "Executable should match");
        expect (manifest->acceptsAudioInput == true, "acceptsAudioInput should be true");
        expect (manifest->producesAudioOutput == false, "producesAudioOutput should be false");

        tempDir.deleteRecursively();
    }

    // Test: invalid executable rejected
    {
        auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("waive_test_tool_bad");
        tempDir.createDirectory();

        auto manifestFile = tempDir.getChildFile (".waive-tool.json");
        manifestFile.replaceWithText (R"({
            "name": "bad_tool",
            "displayName": "Bad Tool",
            "version": "1.0.0",
            "description": "A bad tool",
            "inputSchema": {"type": "object"},
            "executable": "/bin/bash",
            "arguments": ["evil.sh"]
        })");

        auto manifest = waive::ExternalToolManifest::loadFromFile (manifestFile);
        // Should either fail to parse or have the executable rejected
        if (manifest.has_value())
            expect (manifest->executable != "/bin/bash", "Should not allow arbitrary executables");

        tempDir.deleteRecursively();
    }

    // Test: missing required fields
    {
        auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("waive_test_tool_incomplete");
        tempDir.createDirectory();

        auto manifestFile = tempDir.getChildFile (".waive-tool.json");
        manifestFile.replaceWithText (R"({"name": "incomplete"})");

        auto manifest = waive::ExternalToolManifest::loadFromFile (manifestFile);
        expect (! manifest.has_value(), "Should fail on incomplete manifest");

        tempDir.deleteRecursively();
    }
}
```

### 5. Create `tests/WaiveChatSerializerTests.cpp` — Test chat history serialization

```cpp
#include <JuceHeader.h>
#include "ChatHistorySerializer.h"

namespace
{
void expect (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}
} // namespace

void runChatSerializerTests()
{
    // Test: round-trip serialization
    {
        std::vector<waive::ChatMessage> messages;

        waive::ChatMessage userMsg;
        userMsg.role = waive::ChatMessage::Role::user;
        userMsg.content = "Hello, test message";
        messages.push_back (userMsg);

        waive::ChatMessage assistantMsg;
        assistantMsg.role = waive::ChatMessage::Role::assistant;
        assistantMsg.content = "Hello! How can I help?";
        messages.push_back (assistantMsg);

        auto json = waive::ChatHistorySerializer::serialize (messages);
        expect (json.isNotEmpty(), "Serialized JSON should not be empty");

        auto loaded = waive::ChatHistorySerializer::deserialize (json);
        expect (loaded.size() == 2, "Should load 2 messages");
        expect (loaded[0].role == waive::ChatMessage::Role::user, "First message should be user");
        expect (loaded[0].content == "Hello, test message", "User content should match");
        expect (loaded[1].role == waive::ChatMessage::Role::assistant, "Second message should be assistant");
    }

    // Test: empty conversation
    {
        std::vector<waive::ChatMessage> empty;
        auto json = waive::ChatHistorySerializer::serialize (empty);
        auto loaded = waive::ChatHistorySerializer::deserialize (json);
        expect (loaded.empty(), "Empty conversation should deserialize to empty");
    }

    // Test: message with tool calls
    {
        std::vector<waive::ChatMessage> messages;
        waive::ChatMessage msg;
        msg.role = waive::ChatMessage::Role::assistant;
        msg.content = "Let me check the tracks.";

        waive::ChatMessage::ToolCall tc;
        tc.id = "call_123";
        tc.name = "cmd_get_tracks";
        msg.toolCalls.push_back (tc);
        messages.push_back (msg);

        auto json = waive::ChatHistorySerializer::serialize (messages);
        auto loaded = waive::ChatHistorySerializer::deserialize (json);
        expect (loaded.size() == 1, "Should have 1 message");
        expect (loaded[0].toolCalls.size() == 1, "Should have 1 tool call");
        expect (loaded[0].toolCalls[0].name == "cmd_get_tracks", "Tool call name should match");
    }
}
```

### 6. Modify `tests/CMakeLists.txt` — Add new test executables

Add the new test files to the build:

```cmake
# After the existing WaiveCoreTests and WaiveUiTests:

# CommandHandler tests (engine-level, no GUI)
juce_add_console_app (WaiveCommandTests PRODUCT_NAME "Waive Command Tests")
target_sources (WaiveCommandTests PRIVATE WaiveCommandTests.cpp)
target_include_directories (WaiveCommandTests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/src
    ${CMAKE_SOURCE_DIR}/gui/src/ai
    ${CMAKE_SOURCE_DIR}/gui/src/tools
    ${CMAKE_SOURCE_DIR}/gui/src/edit
    ${CMAKE_SOURCE_DIR}/gui/src/util
)
# Link same libraries as existing tests
target_link_libraries (WaiveCommandTests PRIVATE
    WaiveEngine
    WaiveGuiLib  # or whatever the GUI static lib target is called
    tracktion::tracktion_engine
    juce::juce_audio_utils
    juce::juce_gui_extra
)
target_compile_features (WaiveCommandTests PRIVATE cxx_std_20)

add_test (NAME WaiveCommandTests COMMAND WaiveCommandTests)

# Tool schema tests
juce_add_console_app (WaiveToolSchemaTests PRODUCT_NAME "Waive Tool Schema Tests")
target_sources (WaiveToolSchemaTests PRIVATE WaiveToolSchemaTests.cpp)
# ... same includes and links ...
add_test (NAME WaiveToolSchemaTests COMMAND WaiveToolSchemaTests)

# External tool manifest tests
juce_add_console_app (WaiveExternalToolTests PRODUCT_NAME "Waive External Tool Tests")
target_sources (WaiveExternalToolTests PRIVATE WaiveExternalToolTests.cpp)
# ... same includes and links ...
add_test (NAME WaiveExternalToolTests COMMAND WaiveExternalToolTests)

# Chat serializer tests
juce_add_console_app (WaiveChatSerializerTests PRODUCT_NAME "Waive Chat Serializer Tests")
target_sources (WaiveChatSerializerTests PRIVATE WaiveChatSerializerTests.cpp)
# ... same includes and links ...
add_test (NAME WaiveChatSerializerTests COMMAND WaiveChatSerializerTests)
```

**Important**: Check how the existing test targets are structured in `tests/CMakeLists.txt` and follow the exact same pattern for include paths, link libraries, and compile definitions. Do NOT guess — read the existing CMakeLists.txt and replicate the pattern.

### 7. Add `main()` to each new test file

Each test file needs a main function that sets up the JUCE/Tracktion environment:

```cpp
int main()
{
    // For engine-level tests (WaiveCommandTests):
    auto engine = std::make_unique<te::Engine> ("WaiveCommandTests");
    auto edit = te::Edit::createSingleTrackEdit (*engine);

    try
    {
        runCommandHandlerTests (*edit);
        std::cout << "All command handler tests passed!" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
```

For tests that don't need Tracktion (schema, manifest, serializer):

```cpp
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    try
    {
        runToolSchemaTests(); // or runExternalToolManifestTests() etc.
        std::cout << "All tests passed!" << std::endl;
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
```

### 8. Create Python test scaffold

Create `tools/tests/` directory with basic tests for external tools:

**`tools/tests/test_tool_manifests.py`**:
```python
"""Validate all .waive-tool.json manifests in the tools directory."""
import json
import os
import pytest

TOOLS_DIR = os.path.join(os.path.dirname(__file__), "..")
REQUIRED_FIELDS = ["name", "displayName", "version", "description", "inputSchema", "executable"]
ALLOWED_EXECUTABLES = ["python3", "python", "node", "ruby", "perl"]


def find_manifests():
    manifests = []
    for root, dirs, files in os.walk(TOOLS_DIR):
        for f in files:
            if f == ".waive-tool.json":
                manifests.append(os.path.join(root, f))
    return manifests


@pytest.mark.parametrize("manifest_path", find_manifests(), ids=lambda p: os.path.basename(os.path.dirname(p)))
def test_manifest_valid(manifest_path):
    with open(manifest_path) as f:
        data = json.load(f)

    for field in REQUIRED_FIELDS:
        assert field in data, f"Missing required field: {field}"

    assert data["executable"] in ALLOWED_EXECUTABLES, f"Invalid executable: {data['executable']}"
    assert isinstance(data["inputSchema"], dict), "inputSchema must be an object"
    assert data["inputSchema"].get("type") == "object", "inputSchema type must be 'object'"


@pytest.mark.parametrize("manifest_path", find_manifests(), ids=lambda p: os.path.basename(os.path.dirname(p)))
def test_tool_entry_point_exists(manifest_path):
    with open(manifest_path) as f:
        data = json.load(f)

    tool_dir = os.path.dirname(manifest_path)
    args = data.get("arguments", [])
    if args:
        entry_point = os.path.join(tool_dir, args[0])
        # For Python packages, check __main__.py
        if args[0] == "__main__.py" or args[0].endswith(".py"):
            assert os.path.exists(entry_point), f"Entry point not found: {entry_point}"
```

**`tools/tests/requirements.txt`**:
```
pytest>=7.0
```

### 9. Update `.github/workflows/ci.yml` — Fix Python test paths

The CI currently looks for `ai/requirements.txt` and `ai/tests/`. Update to point to the actual tool test locations:

```yaml
# Replace the python test job's path check:
# Before:
#   if: hashFiles('ai/requirements.txt') != '' && hashFiles('ai/tests/') != ''
# After:
  - name: Run Python tool tests
    if: hashFiles('tools/tests/requirements.txt') != ''
    run: |
      python3 -m pip install -r tools/tests/requirements.txt
      python3 -m pytest tools/tests/ -v
    continue-on-error: true
```

## Files Expected To Change
- `tests/WaiveUiTests.cpp` (fix crash)
- `tests/WaiveCommandTests.cpp` (new)
- `tests/WaiveToolSchemaTests.cpp` (new)
- `tests/WaiveExternalToolTests.cpp` (new)
- `tests/WaiveChatSerializerTests.cpp` (new)
- `tests/CMakeLists.txt`
- `tools/tests/test_tool_manifests.py` (new)
- `tools/tests/requirements.txt` (new)
- `.github/workflows/ci.yml`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
ctest --test-dir build --output-on-failure
```

All tests should pass (including the previously-crashing WaiveUiTests).

## Exit Criteria
- WaiveUiTests runs to completion without crash (all tests either pass or fail gracefully).
- WaiveCommandTests passes — covers ping, add/remove track, volume/pan, transport, tempo, error cases.
- WaiveToolSchemaTests passes — validates command definitions, no duplicates, required fields.
- WaiveExternalToolTests passes — validates manifest parsing, invalid executable rejection, incomplete manifest handling.
- WaiveChatSerializerTests passes — round-trip serialization, empty conversation, tool call serialization.
- Python tool manifest tests exist and validate all `.waive-tool.json` files.
- CI pipeline updated to run Python tests from correct directory.
- Build compiles with no errors.
- `ctest --test-dir build --output-on-failure` shows all tests passing.
