# Phase 10: Dynamic Tool Discovery for AI

## Objective
Implement a Claude Code-style dynamic tool discovery system so the AI agent's context is not polluted with every tool schema. Instead, the AI gets a compact set of core commands plus a `search_tools` meta-command. When the AI needs a specific tool, it searches first, gets the schema, and then calls it. This keeps the system prompt lean as the tool count grows.

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH).
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.
- Tracktion Engine namespace is `namespace te = tracktion;`

## Architecture Context

### Current AI tool loading (gui/src/ai/AiAgent.cpp)
In `runConversationLoop()`:
```cpp
auto toolDefs = generateAllDefinitions (toolRegistry);
```
This loads ALL command definitions + ALL tool definitions into every LLM call. As tools grow (30+ commands, 10+ tools), this wastes tokens and can confuse the model.

### AiToolSchema (gui/src/ai/AiToolSchema.cpp)
- `generateCommandDefinitions()` — returns all `cmd_*` definitions (currently ~24 commands).
- `generateToolDefinitions(registry)` — returns all `tool_*` definitions from ToolRegistry.
- `generateAllDefinitions(registry)` — combines both.
- `generateSystemPrompt()` — static system prompt text.

### AiAgent (gui/src/ai/AiAgent.h/.cpp)
- `executeToolCall()` routes `cmd_*` → `executeCommand()`, `tool_*` → tool execution.
- `runConversationLoop()` iterates up to 10 turns with the LLM.
- The tool definitions are passed to every `provider->sendRequest()` call.

### ToolDescription (gui/src/tools/Tool.h)
```cpp
struct ToolDescription {
    juce::String name;
    juce::String displayName;
    juce::String version;
    juce::String description;
    juce::var inputSchema;
    juce::var defaultParams;
    juce::String modelRequirement;
};
```
Currently has no category or tag fields.

### AiToolDefinition (gui/src/ai/AiToolSchema.h)
```cpp
struct AiToolDefinition {
    juce::String name;
    juce::String description;
    juce::var inputSchema;
};
```

## Design

### Approach: Two-tier tool system

**Tier 1: Always-available (core commands)**
A small set of essential commands always included in the LLM's tool list:
- `cmd_get_tracks` — query project state
- `cmd_get_transport_state` — query transport
- `cmd_get_edit_state` — full state (verbose)
- `cmd_search_tools` — **NEW**: discover available tools by keyword

**Tier 2: On-demand (discoverable commands + tools)**
All other `cmd_*` and `tool_*` definitions are NOT included in the initial tool list. The AI discovers them via `cmd_search_tools`, which returns matching tool schemas. Those schemas are then added to the tool list for subsequent LLM calls in the same conversation loop.

### search_tools command
- Input: `query` (string) — keyword(s) to search for
- Searches tool names, descriptions, and categories
- Returns up to 5 matching tool definitions with full schemas
- The returned schemas are injected into the active tool list for the current conversation

### Tool categories
Add a `category` field to `AiToolDefinition`:
- `"query"` — read-only commands (get_tracks, get_transport_state, etc.)
- `"transport"` — play, stop, seek, record
- `"track"` — add, remove, rename, solo, mute, duplicate
- `"clip"` — split, delete, move, duplicate, trim, rename, set_gain
- `"mixing"` — volume, pan, plugin management
- `"export"` — mixdown, stems, bounce
- `"recording"` — arm, record_from_mic
- `"analysis"` — normalize, gain stage, silence detection, transient alignment
- `"ai"` — stem separation, auto-mix, timbre transfer, music generation, etc.

## Implementation Tasks

### 1. Modify `gui/src/ai/AiToolSchema.h` — Add category field

Add `category` to `AiToolDefinition`:
```cpp
struct AiToolDefinition
{
    juce::String name;
    juce::String description;
    juce::var inputSchema;
    juce::String category;  // NEW
};
```

### 2. Modify `gui/src/ai/AiToolSchema.h` — Add new function declarations

Add these function declarations:
```cpp
/** Returns only the core tools that should always be in the AI's tool list. */
std::vector<AiToolDefinition> generateCoreDefinitions();

/** Search all definitions by keyword query, returning up to maxResults matches. */
std::vector<AiToolDefinition> searchDefinitions (const ToolRegistry& registry,
                                                  const juce::String& query,
                                                  int maxResults = 5);
```

### 3. Modify `gui/src/ai/AiToolSchema.cpp` — Add categories to all command definitions

In `generateCommandDefinitions()`, add a `category` field to every definition. Update each existing `defs.push_back()` call to include the category. For example:

```cpp
// get_tracks
{ "cmd_get_tracks",
  "Get all audio tracks with their clips, names, indices, solo/mute state.",
  makeSchema ("object"),
  "query" }   // <-- category

// add_track
{ "cmd_add_track",
  "Add a new empty audio track to the session.",
  makeSchema ("object"),
  "track" }

// transport_play
{ "cmd_transport_play",
  "Start playback.",
  makeSchema ("object"),
  "transport" }
```

Categories for all existing commands:
- `"query"`: get_tracks, get_edit_state, get_transport_state, list_plugins, get_plugin_parameters, ping
- `"transport"`: transport_play, transport_stop, transport_seek, set_tempo, set_loop_region
- `"track"`: add_track, remove_track, rename_track, solo_track, mute_track, duplicate_track
- `"clip"`: split_clip, delete_clip, move_clip, duplicate_clip, trim_clip, set_clip_gain, rename_clip
- `"mixing"`: set_track_volume, set_track_pan, load_plugin, set_parameter, remove_plugin, bypass_plugin
- `"export"`: export_mixdown, export_stems, bounce_track
- `"recording"`: arm_track, record_from_mic
- `"audio"`: insert_audio_clip, insert_midi_clip

### 4. Modify `gui/src/ai/AiToolSchema.cpp` — Add categories to tool definitions

In `generateToolDefinitions()`, map tool names to categories:

```cpp
for (auto& tool : registry.getTools())
{
    auto desc = tool->describe();
    AiToolDefinition def;
    def.name = "tool_" + desc.name;
    def.description = desc.description;
    def.inputSchema = desc.inputSchema;

    // Categorize tools
    if (desc.name.containsIgnoreCase ("stem") ||
        desc.name.containsIgnoreCase ("mix") ||
        desc.name.containsIgnoreCase ("timbre") ||
        desc.name.containsIgnoreCase ("generation"))
        def.category = "ai";
    else
        def.category = "analysis";

    defs.push_back (std::move (def));
}
```

### 5. Modify `gui/src/ai/AiToolSchema.cpp` — Implement generateCoreDefinitions

```cpp
std::vector<AiToolDefinition> generateCoreDefinitions()
{
    std::vector<AiToolDefinition> core;

    // Always-available query tools
    core.push_back ({ "cmd_get_tracks",
                      "Get all audio tracks with clips, names, indices, solo/mute.",
                      makeSchema ("object"),
                      "query" });

    core.push_back ({ "cmd_get_transport_state",
                      "Get transport state: position, tempo, playing/recording, loop region.",
                      makeSchema ("object"),
                      "query" });

    // The search tool itself
    core.push_back ({ "cmd_search_tools",
                      "Search for available tools and commands by keyword. Returns matching tool schemas that you can then call. "
                      "Use this to discover clip editing, mixing, export, recording, AI, and analysis tools. "
                      "Categories: query, transport, track, clip, mixing, export, recording, audio, analysis, ai.",
                      makeSchema ("object",
                                  { { "query", prop ("string", "Search keywords (e.g. 'split clip', 'export', 'tempo', 'plugin', 'ai stem')") } },
                                  { "query" }),
                      "query" });

    return core;
}
```

### 6. Modify `gui/src/ai/AiToolSchema.cpp` — Implement searchDefinitions

```cpp
std::vector<AiToolDefinition> searchDefinitions (const ToolRegistry& registry,
                                                  const juce::String& query,
                                                  int maxResults)
{
    auto allDefs = generateAllDefinitions (registry);
    auto queryLower = query.toLowerCase();
    auto keywords = juce::StringArray::fromTokens (queryLower, " ", "");

    // Score each definition
    struct ScoredDef
    {
        AiToolDefinition def;
        int score = 0;
    };

    std::vector<ScoredDef> scored;
    for (auto& d : allDefs)
    {
        // Skip the search tool itself
        if (d.name == "cmd_search_tools")
            continue;

        int score = 0;
        auto nameLower = d.name.toLowerCase();
        auto descLower = d.description.toLowerCase();
        auto catLower = d.category.toLowerCase();

        for (auto& kw : keywords)
        {
            if (nameLower.contains (kw))  score += 3;
            if (catLower == kw)           score += 2;
            if (catLower.contains (kw))   score += 2;
            if (descLower.contains (kw))  score += 1;
        }

        if (score > 0)
            scored.push_back ({ d, score });
    }

    // Sort by score descending
    std::sort (scored.begin(), scored.end(),
               [] (const ScoredDef& a, const ScoredDef& b) { return a.score > b.score; });

    std::vector<AiToolDefinition> results;
    for (int i = 0; i < std::min ((int) scored.size(), maxResults); ++i)
        results.push_back (scored[(size_t) i].def);

    return results;
}
```

### 7. Modify `gui/src/ai/AiAgent.h` — Add dynamic tool tracking

Add a member to track discovered tools in the conversation:
```cpp
std::vector<AiToolDefinition> discoveredTools;
```

### 8. Modify `gui/src/ai/AiAgent.cpp` — Use core definitions + dynamic discovery

In `runConversationLoop()`, replace:
```cpp
auto toolDefs = generateAllDefinitions (toolRegistry);
```
with:
```cpp
auto toolDefs = generateCoreDefinitions();
// Add any tools discovered during this conversation
toolDefs.insert (toolDefs.end(), discoveredTools.begin(), discoveredTools.end());
```

### 9. Modify `gui/src/ai/AiAgent.cpp` — Handle search_tools in executeToolCall

In `executeToolCall()`, add handling for `cmd_search_tools` BEFORE the existing `cmd_*` routing:

```cpp
juce::String AiAgent::executeToolCall (const ChatMessage::ToolCall& call)
{
    // Handle search_tools specially — it returns schemas and adds them to the active set
    if (call.name == "cmd_search_tools")
    {
        auto query = call.arguments.hasProperty ("query")
                       ? call.arguments["query"].toString()
                       : juce::String();

        if (query.isEmpty())
            return "{\"status\":\"error\",\"message\":\"Missing query parameter\"}";

        auto results = searchDefinitions (toolRegistry, query);

        // Add discovered tools to the active set (avoid duplicates)
        for (auto& r : results)
        {
            bool alreadyKnown = false;
            for (auto& existing : discoveredTools)
            {
                if (existing.name == r.name)
                {
                    alreadyKnown = true;
                    break;
                }
            }
            if (! alreadyKnown)
                discoveredTools.push_back (r);
        }

        // Build response
        auto* resultObj = new juce::DynamicObject();
        resultObj->setProperty ("status", "ok");
        resultObj->setProperty ("count", (int) results.size());

        juce::Array<juce::var> toolList;
        for (auto& r : results)
        {
            auto* tObj = new juce::DynamicObject();
            tObj->setProperty ("name", r.name);
            tObj->setProperty ("description", r.description);
            tObj->setProperty ("category", r.category);
            // Include input schema so the AI knows the parameters
            tObj->setProperty ("input_schema", r.inputSchema);
            toolList.add (juce::var (tObj));
        }
        resultObj->setProperty ("tools", toolList);

        if (results.empty())
            resultObj->setProperty ("message", "No tools found matching: " + query);
        else
            resultObj->setProperty ("message", juce::String (results.size()) + " tools found. You can now call them directly.");

        return juce::JSON::toString (juce::var (resultObj));
    }

    if (call.name.startsWith ("cmd_"))
    {
        auto action = call.name.substring (4);
        return executeCommand (action, call.arguments);
    }
    // ... rest of existing code
```

### 10. Modify `gui/src/ai/AiAgent.cpp` — Clear discovered tools on new conversation

In `clearConversation()`, add:
```cpp
discoveredTools.clear();
```

Also in `runConversationLoop()`, clear at the start:
```cpp
discoveredTools.clear();
```

### 11. Modify `gui/src/ai/AiToolSchema.cpp` — Update system prompt

Replace the existing `generateSystemPrompt()` with an updated version that explains the discovery system:

```cpp
juce::String generateSystemPrompt()
{
    return "You are Waive AI, an intelligent assistant for the Waive digital audio workstation.\n\n"
           "You have a small set of core tools always available:\n"
           "- cmd_get_tracks: Query all tracks and clips\n"
           "- cmd_get_transport_state: Check playback position, tempo, loop settings\n"
           "- cmd_search_tools: Discover more tools by keyword\n\n"
           "When you need to perform an action (editing clips, mixing, exporting, using AI tools, etc.), "
           "first use cmd_search_tools to find the right tool. For example:\n"
           "- To split a clip: search for 'split clip'\n"
           "- To export audio: search for 'export'\n"
           "- To adjust volume: search for 'volume mixing'\n"
           "- To use AI features: search for 'ai' or 'stem separation'\n\n"
           "Tool categories: query, transport, track, clip, mixing, export, recording, audio, analysis, ai.\n\n"
           "Guidelines:\n"
           "- Be concise and helpful.\n"
           "- Track IDs and clip indices are 0-based.\n"
           "- Volume is in decibels (dB). 0 dB is unity gain.\n"
           "- Pan ranges from -1.0 (full left) to 1.0 (full right).\n"
           "- Always query project state before making changes.\n"
           "- Do not invent file paths. Ask the user for paths if needed.\n"
           "- Tool names prefixed with 'cmd_' are direct DAW commands.\n"
           "- Tool names prefixed with 'tool_' are higher-level audio processing tools.\n";
}
```

## Files Expected To Change
- `gui/src/ai/AiToolSchema.h`
- `gui/src/ai/AiToolSchema.cpp`
- `gui/src/ai/AiAgent.h`
- `gui/src/ai/AiAgent.cpp`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- AI agent starts with only 3 core tools (get_tracks, get_transport_state, search_tools).
- `cmd_search_tools` returns matching tools with full schemas by keyword.
- Discovered tools are added to the active tool set for subsequent LLM calls.
- Categories assigned to all existing commands and tools.
- System prompt updated to explain tool discovery workflow.
- All existing commands remain callable (just need to be discovered first).
- Build compiles with no errors.
