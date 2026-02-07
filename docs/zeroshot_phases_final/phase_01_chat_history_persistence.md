# Phase 01: Chat History Persistence

## Objective
Add JSON serialization for AI chat conversations so they persist across app restarts. Save on shutdown, restore on launch (or project open).

## Build System
- CMake build: `cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))`
- NEVER use `-j$(nproc)` — will OOM. Always use `-j$(($(nproc)/2))`.
- Do NOT add precompiled headers (PCH). JUCE module .cpp files fail with `#error "Incorrect use of JUCE cpp file"` when PCH includes JuceHeader.h.
- Do NOT remove any existing source files from `gui/CMakeLists.txt`.
- `unique_ptr<T>` with incomplete T in header needs explicit destructor defined in .cpp where T is complete.
- JUCE `-Wshadow`: careful with constructor parameter names vs member names.

## Architecture Context
- `gui/src/ai/AiAgent.h/.cpp` — owns `std::vector<ChatMessage> conversation` protected by `std::mutex conversationMutex`
- `gui/src/ai/AiProvider.h` — defines `ChatMessage` struct with `Role` enum (`user`, `assistant`, `system`, `toolResult`), `ToolCall` inner struct (`id`, `name`, `arguments`), and `toolCallId` field
- `gui/src/Main.cpp` — `WaiveApplication` class owns `aiAgent`, `editSession`, `projectManager`, `appProperties`; implements `EditSession::Listener` (with `editChanged()`) and `ProjectManager::Listener`
- The app uses `juce::ApplicationProperties` with `folderName = "Waive"` for settings persistence

## Implementation Tasks

### 1. Create `gui/src/ai/ChatHistorySerializer.h`

New file. A stateless utility for converting conversations to/from JSON.

```cpp
#pragma once

#include <JuceHeader.h>
#include <vector>
#include "AiProvider.h"

namespace waive
{

/** JSON serialization helpers for chat conversations. */
namespace ChatHistorySerializer
{
    /** Convert a conversation to a JSON array. */
    juce::var conversationToJson (const std::vector<ChatMessage>& messages);

    /** Parse a JSON array back into a conversation vector. */
    std::vector<ChatMessage> conversationFromJson (const juce::var& json);

    /** Save a conversation to a JSON file. Returns true on success. */
    bool saveChatHistory (const std::vector<ChatMessage>& messages, const juce::File& file);

    /** Load a conversation from a JSON file. Returns empty vector on failure. */
    std::vector<ChatMessage> loadChatHistory (const juce::File& file);
}

} // namespace waive
```

### 2. Create `gui/src/ai/ChatHistorySerializer.cpp`

New file. Implements the serialization.

Each message is serialized as:
```json
{
  "role": "user" | "assistant" | "system" | "toolResult",
  "content": "...",
  "toolCalls": [ { "id": "...", "name": "...", "arguments": { ... } } ],
  "toolCallId": "..."
}
```

Implementation details:
- `conversationToJson()`: iterate messages, create a `juce::DynamicObject` per message with role string, content, optional toolCalls array, optional toolCallId. Return as `juce::var` wrapping a `juce::Array<juce::var>`.
- `conversationFromJson()`: iterate the JSON array, parse each object back into a `ChatMessage`. Map role strings to the `ChatMessage::Role` enum. Parse `toolCalls` array into `ChatMessage::ToolCall` structs. The `arguments` field in each tool call is already a `juce::var` from JSON parsing.
- `saveChatHistory()`: call `conversationToJson()`, then `juce::JSON::toString()`, then write to file. Create parent directory if needed with `file.getParentDirectory().createDirectory()`. Return success/failure.
- `loadChatHistory()`: read file text, parse with `juce::JSON::parse()`, call `conversationFromJson()`. Return empty vector on any failure (file not found, parse error).

Role string mapping:
- `ChatMessage::Role::user` ↔ `"user"`
- `ChatMessage::Role::assistant` ↔ `"assistant"`
- `ChatMessage::Role::system` ↔ `"system"`
- `ChatMessage::Role::toolResult` ↔ `"toolResult"`

### 3. Modify `gui/src/ai/AiAgent.h`

Add two new public methods after `cancelRequest()`:
```cpp
/** Save the current conversation to a JSON file. */
void saveConversation (const juce::File& file);

/** Load a conversation from a JSON file, replacing current. */
void loadConversation (const juce::File& file);
```

### 4. Modify `gui/src/ai/AiAgent.cpp`

Add `#include "ChatHistorySerializer.h"` at the top.

Implement the two methods:

```cpp
void AiAgent::saveConversation (const juce::File& file)
{
    std::lock_guard<std::mutex> lock (conversationMutex);
    ChatHistorySerializer::saveChatHistory (conversation, file);
}

void AiAgent::loadConversation (const juce::File& file)
{
    auto loaded = ChatHistorySerializer::loadChatHistory (file);

    if (loaded.empty())
        return;

    {
        std::lock_guard<std::mutex> lock (conversationMutex);
        conversation = std::move (loaded);
    }

    juce::MessageManager::callAsync ([this] { listeners.call (&AiAgentListener::conversationUpdated); });
}
```

### 5. Modify `gui/src/Main.cpp`

Add `#include "ChatHistorySerializer.h"` at the top (not strictly required but good for clarity).

Add a private helper method to `WaiveApplication`:
```cpp
juce::File getChatHistoryFile() const
{
    if (projectManager && projectManager->getProjectFile().existsAsFile())
    {
        auto projectDir = projectManager->getProjectFile().getParentDirectory();
        auto chatDir = projectDir.getChildFile (".waive_chat");
        return chatDir.getChildFile (projectManager->getProjectName() + ".chat.json");
    }

    // Unsaved project — use app data directory
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Waive")
               .getChildFile ("chat_history.json");
}
```

In `shutdown()`, BEFORE `mainWindow.reset()` and `aiAgent.reset()`, add:
```cpp
if (aiAgent)
    aiAgent->saveConversation (getChatHistoryFile());
```

In `editChanged()`, AFTER the existing code (after `updateWindowTitle()`), add:
```cpp
if (aiAgent)
    aiAgent->loadConversation (getChatHistoryFile());
```

Also at the END of `initialise()`, after the plugin scan submission, add:
```cpp
// Restore chat history for the initial (unsaved) project
if (aiAgent)
    aiAgent->loadConversation (getChatHistoryFile());
```

### 6. Modify `gui/CMakeLists.txt`

In the `# AI` section, add these two lines after the existing AI files:
```
    src/ai/ChatHistorySerializer.h
    src/ai/ChatHistorySerializer.cpp
```

## Files Expected To Change
- `gui/src/ai/ChatHistorySerializer.h` (NEW)
- `gui/src/ai/ChatHistorySerializer.cpp` (NEW)
- `gui/src/ai/AiAgent.h`
- `gui/src/ai/AiAgent.cpp`
- `gui/src/Main.cpp`
- `gui/CMakeLists.txt`

## Validation

```bash
cmake -B build && cmake --build build --target Waive -j$(($(nproc)/2))
```

## Exit Criteria
- ChatHistorySerializer correctly round-trips conversations including tool calls and tool results.
- AiAgent save/load methods work with mutex protection.
- App saves chat on shutdown, loads on startup and project switch.
- Build compiles with no errors.
