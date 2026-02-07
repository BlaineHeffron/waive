#pragma once

#include <JuceHeader.h>
#include <vector>

namespace waive
{

struct ChatMessage
{
    enum class Role { user, assistant, system, toolResult };

    Role role = Role::user;
    juce::String content;

    struct ToolCall
    {
        juce::String id;
        juce::String name;
        juce::var arguments;
    };

    std::vector<ToolCall> toolCalls;
    juce::String toolCallId;  // for toolResult messages
};

struct AiToolDefinition
{
    juce::String name;
    juce::String description;
    juce::var inputSchema;  // JSON Schema object
};

struct AiResponse
{
    juce::String textContent;
    std::vector<ChatMessage::ToolCall> toolCalls;
    int inputTokens = 0;
    int outputTokens = 0;
    juce::String stopReason;
    juce::String errorMessage;
    bool isError = false;
};

//==============================================================================
class AiProvider
{
public:
    virtual ~AiProvider() = default;

    virtual AiResponse sendRequest (const juce::String& apiKey,
                                    const juce::String& model,
                                    const juce::String& systemPrompt,
                                    const std::vector<ChatMessage>& messages,
                                    const std::vector<AiToolDefinition>& tools) = 0;
};

//==============================================================================
class AnthropicProvider : public AiProvider
{
public:
    AiResponse sendRequest (const juce::String& apiKey,
                            const juce::String& model,
                            const juce::String& systemPrompt,
                            const std::vector<ChatMessage>& messages,
                            const std::vector<AiToolDefinition>& tools) override;
};

//==============================================================================
class OpenAiProvider : public AiProvider
{
public:
    AiResponse sendRequest (const juce::String& apiKey,
                            const juce::String& model,
                            const juce::String& systemPrompt,
                            const std::vector<ChatMessage>& messages,
                            const std::vector<AiToolDefinition>& tools) override;
};

//==============================================================================
class GeminiProvider : public AiProvider
{
public:
    AiResponse sendRequest (const juce::String& apiKey,
                            const juce::String& model,
                            const juce::String& systemPrompt,
                            const std::vector<ChatMessage>& messages,
                            const std::vector<AiToolDefinition>& tools) override;
};

//==============================================================================
std::unique_ptr<AiProvider> createProvider (int providerType);

} // namespace waive
