#pragma once

#include <JuceHeader.h>
#include <vector>

#include "AiProvider.h"

namespace waive
{

class ToolRegistry;

/** Generate LLM tool definitions from the DAW's existing capabilities. */
std::vector<AiToolDefinition> generateToolDefinitions (const ToolRegistry& registry);

/** Generate command definitions for CommandHandler actions. */
std::vector<AiToolDefinition> generateCommandDefinitions();

/** Generate all tool + command definitions. */
std::vector<AiToolDefinition> generateAllDefinitions (const ToolRegistry& registry);

/** Returns only the core tools that should always be in the AI's tool list. */
std::vector<AiToolDefinition> generateCoreDefinitions();

/** Search all definitions by keyword query, returning up to maxResults matches. */
std::vector<AiToolDefinition> searchDefinitions (const ToolRegistry& registry,
                                                  const juce::String& query,
                                                  int maxResults = 5);

/** Generate the system prompt for the AI assistant. */
juce::String generateSystemPrompt();

} // namespace waive
