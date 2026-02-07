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

/** Generate the system prompt for the AI assistant. */
juce::String generateSystemPrompt();

} // namespace waive
