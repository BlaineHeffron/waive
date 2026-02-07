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
