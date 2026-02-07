#include "AiProvider.h"
#include "AiSettings.h"

namespace waive
{

//==============================================================================
// Helpers
//==============================================================================

static juce::var parseJson (const juce::String& text)
{
    return juce::JSON::parse (text);
}

static juce::String toJson (const juce::var& v)
{
    return juce::JSON::toString (v, true);
}

//==============================================================================
// Anthropic Provider
//==============================================================================

AiResponse AnthropicProvider::sendRequest (const juce::String& apiKey,
                                           const juce::String& model,
                                           const juce::String& systemPrompt,
                                           const std::vector<ChatMessage>& messages,
                                           const std::vector<AiToolDefinition>& tools)
{
    AiResponse response;

    // Build messages array
    juce::Array<juce::var> msgList;

    for (auto& msg : messages)
    {
        auto* msgObj = new juce::DynamicObject();

        if (msg.role == ChatMessage::Role::user)
            msgObj->setProperty ("role", "user");
        else if (msg.role == ChatMessage::Role::assistant)
            msgObj->setProperty ("role", "assistant");
        else if (msg.role == ChatMessage::Role::toolResult)
        {
            msgObj->setProperty ("role", "user");
            juce::Array<juce::var> contentArr;
            auto* block = new juce::DynamicObject();
            block->setProperty ("type", "tool_result");
            block->setProperty ("tool_use_id", msg.toolCallId);
            block->setProperty ("content", msg.content);
            contentArr.add (juce::var (block));
            msgObj->setProperty ("content", contentArr);
            msgList.add (juce::var (msgObj));
            continue;
        }
        else
            continue;  // skip system messages (handled separately)

        // If assistant message has tool calls, format as content blocks
        if (msg.role == ChatMessage::Role::assistant && ! msg.toolCalls.empty())
        {
            juce::Array<juce::var> contentArr;

            if (msg.content.isNotEmpty())
            {
                auto* textBlock = new juce::DynamicObject();
                textBlock->setProperty ("type", "text");
                textBlock->setProperty ("text", msg.content);
                contentArr.add (juce::var (textBlock));
            }

            for (auto& tc : msg.toolCalls)
            {
                auto* tcBlock = new juce::DynamicObject();
                tcBlock->setProperty ("type", "tool_use");
                tcBlock->setProperty ("id", tc.id);
                tcBlock->setProperty ("name", tc.name);
                tcBlock->setProperty ("input", tc.arguments);
                contentArr.add (juce::var (tcBlock));
            }

            msgObj->setProperty ("content", contentArr);
        }
        else
        {
            msgObj->setProperty ("content", msg.content);
        }

        msgList.add (juce::var (msgObj));
    }

    // Build request body
    auto* reqObj = new juce::DynamicObject();
    reqObj->setProperty ("model", model);
    reqObj->setProperty ("max_tokens", 4096);
    reqObj->setProperty ("system", systemPrompt);
    reqObj->setProperty ("messages", msgList);

    // Add tools
    if (! tools.empty())
    {
        juce::Array<juce::var> toolsArr;
        for (auto& tool : tools)
        {
            auto* toolObj = new juce::DynamicObject();
            toolObj->setProperty ("name", tool.name);
            toolObj->setProperty ("description", tool.description);
            toolObj->setProperty ("input_schema", tool.inputSchema);
            toolsArr.add (juce::var (toolObj));
        }
        reqObj->setProperty ("tools", toolsArr);
    }

    auto body = toJson (juce::var (reqObj));

    // Make HTTP request
    juce::URL url ("https://api.anthropic.com/v1/messages");
    url = url.withPOSTData (body);

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders ("x-api-key: " + apiKey
                                          + "\r\nanthropic-version: 2023-06-01"
                                          + "\r\nContent-Type: application/json")
                       .withConnectionTimeoutMs (60000)
                       .withNumRedirectsToFollow (5);

    auto stream = url.createInputStream (options);
    if (stream == nullptr)
    {
        response.isError = true;
        response.errorMessage = "Failed to connect to Anthropic API";
        return response;
    }

    auto responseStr = stream->readEntireStreamAsString();
    auto json = parseJson (responseStr);

    if (! json.isObject())
    {
        response.isError = true;
        response.errorMessage = "Invalid response from Anthropic API";
        return response;
    }

    // Check for error
    if (json.hasProperty ("error"))
    {
        response.isError = true;
        response.errorMessage = json["error"]["message"].toString();
        return response;
    }

    // Parse response
    response.stopReason = json["stop_reason"].toString();

    if (json.hasProperty ("usage"))
    {
        response.inputTokens = (int) json["usage"]["input_tokens"];
        response.outputTokens = (int) json["usage"]["output_tokens"];
    }

    auto content = json["content"];
    if (content.isArray())
    {
        for (int i = 0; i < content.size(); ++i)
        {
            auto block = content[i];
            auto type = block["type"].toString();

            if (type == "text")
            {
                response.textContent += block["text"].toString();
            }
            else if (type == "tool_use")
            {
                ChatMessage::ToolCall tc;
                tc.id = block["id"].toString();
                tc.name = block["name"].toString();
                tc.arguments = block["input"];
                response.toolCalls.push_back (std::move (tc));
            }
        }
    }

    return response;
}

//==============================================================================
// OpenAI Provider
//==============================================================================

AiResponse OpenAiProvider::sendRequest (const juce::String& apiKey,
                                        const juce::String& model,
                                        const juce::String& systemPrompt,
                                        const std::vector<ChatMessage>& messages,
                                        const std::vector<AiToolDefinition>& tools)
{
    AiResponse response;

    // Build messages array
    juce::Array<juce::var> msgList;

    // System message
    {
        auto* sysMsg = new juce::DynamicObject();
        sysMsg->setProperty ("role", "system");
        sysMsg->setProperty ("content", systemPrompt);
        msgList.add (juce::var (sysMsg));
    }

    for (auto& msg : messages)
    {
        auto* msgObj = new juce::DynamicObject();

        if (msg.role == ChatMessage::Role::user)
        {
            msgObj->setProperty ("role", "user");
            msgObj->setProperty ("content", msg.content);
        }
        else if (msg.role == ChatMessage::Role::assistant)
        {
            msgObj->setProperty ("role", "assistant");

            if (! msg.toolCalls.empty())
            {
                if (msg.content.isNotEmpty())
                    msgObj->setProperty ("content", msg.content);

                juce::Array<juce::var> tcArr;
                for (auto& tc : msg.toolCalls)
                {
                    auto* tcObj = new juce::DynamicObject();
                    tcObj->setProperty ("id", tc.id);
                    tcObj->setProperty ("type", "function");
                    auto* fnObj = new juce::DynamicObject();
                    fnObj->setProperty ("name", tc.name);
                    fnObj->setProperty ("arguments", toJson (tc.arguments));
                    tcObj->setProperty ("function", juce::var (fnObj));
                    tcArr.add (juce::var (tcObj));
                }
                msgObj->setProperty ("tool_calls", tcArr);
            }
            else
            {
                msgObj->setProperty ("content", msg.content);
            }
        }
        else if (msg.role == ChatMessage::Role::toolResult)
        {
            msgObj->setProperty ("role", "tool");
            msgObj->setProperty ("tool_call_id", msg.toolCallId);
            msgObj->setProperty ("content", msg.content);
        }
        else
            continue;

        msgList.add (juce::var (msgObj));
    }

    // Build request body
    auto* reqObj = new juce::DynamicObject();
    reqObj->setProperty ("model", model);
    reqObj->setProperty ("messages", msgList);

    if (! tools.empty())
    {
        juce::Array<juce::var> toolsArr;
        for (auto& tool : tools)
        {
            auto* toolObj = new juce::DynamicObject();
            toolObj->setProperty ("type", "function");
            auto* fnObj = new juce::DynamicObject();
            fnObj->setProperty ("name", tool.name);
            fnObj->setProperty ("description", tool.description);
            fnObj->setProperty ("parameters", tool.inputSchema);
            toolObj->setProperty ("function", juce::var (fnObj));
            toolsArr.add (juce::var (toolObj));
        }
        reqObj->setProperty ("tools", toolsArr);
    }

    auto body = toJson (juce::var (reqObj));

    juce::URL url ("https://api.openai.com/v1/chat/completions");
    url = url.withPOSTData (body);

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders ("Authorization: Bearer " + apiKey
                                          + "\r\nContent-Type: application/json")
                       .withConnectionTimeoutMs (60000)
                       .withNumRedirectsToFollow (5);

    auto stream = url.createInputStream (options);
    if (stream == nullptr)
    {
        response.isError = true;
        response.errorMessage = "Failed to connect to OpenAI API";
        return response;
    }

    auto responseStr = stream->readEntireStreamAsString();
    auto json = parseJson (responseStr);

    if (! json.isObject())
    {
        response.isError = true;
        response.errorMessage = "Invalid response from OpenAI API";
        return response;
    }

    if (json.hasProperty ("error"))
    {
        response.isError = true;
        response.errorMessage = json["error"]["message"].toString();
        return response;
    }

    // Parse usage
    if (json.hasProperty ("usage"))
    {
        response.inputTokens = (int) json["usage"]["prompt_tokens"];
        response.outputTokens = (int) json["usage"]["completion_tokens"];
    }

    // Parse choice
    auto choices = json["choices"];
    if (choices.isArray() && choices.size() > 0)
    {
        auto message = choices[0]["message"];
        response.stopReason = choices[0]["finish_reason"].toString();
        response.textContent = message["content"].toString();

        auto toolCallsArr = message["tool_calls"];
        if (toolCallsArr.isArray())
        {
            for (int i = 0; i < toolCallsArr.size(); ++i)
            {
                auto tc = toolCallsArr[i];
                ChatMessage::ToolCall call;
                call.id = tc["id"].toString();
                call.name = tc["function"]["name"].toString();
                call.arguments = parseJson (tc["function"]["arguments"].toString());
                response.toolCalls.push_back (std::move (call));
            }
        }
    }

    return response;
}

//==============================================================================
// Gemini Provider
//==============================================================================

AiResponse GeminiProvider::sendRequest (const juce::String& apiKey,
                                        const juce::String& model,
                                        const juce::String& systemPrompt,
                                        const std::vector<ChatMessage>& messages,
                                        const std::vector<AiToolDefinition>& tools)
{
    AiResponse response;

    // Build contents array
    juce::Array<juce::var> contents;

    for (auto& msg : messages)
    {
        auto* contentObj = new juce::DynamicObject();
        juce::Array<juce::var> parts;

        if (msg.role == ChatMessage::Role::user)
        {
            contentObj->setProperty ("role", "user");
            auto* part = new juce::DynamicObject();
            part->setProperty ("text", msg.content);
            parts.add (juce::var (part));
        }
        else if (msg.role == ChatMessage::Role::assistant)
        {
            contentObj->setProperty ("role", "model");

            if (msg.content.isNotEmpty())
            {
                auto* textPart = new juce::DynamicObject();
                textPart->setProperty ("text", msg.content);
                parts.add (juce::var (textPart));
            }

            for (auto& tc : msg.toolCalls)
            {
                auto* fcPart = new juce::DynamicObject();
                auto* fcObj = new juce::DynamicObject();
                fcObj->setProperty ("name", tc.name);
                fcObj->setProperty ("args", tc.arguments);
                fcPart->setProperty ("functionCall", juce::var (fcObj));
                parts.add (juce::var (fcPart));
            }
        }
        else if (msg.role == ChatMessage::Role::toolResult)
        {
            contentObj->setProperty ("role", "user");
            auto* frPart = new juce::DynamicObject();
            auto* frObj = new juce::DynamicObject();
            frObj->setProperty ("name", msg.toolCallId);
            auto* respObj = new juce::DynamicObject();
            respObj->setProperty ("result", msg.content);
            frObj->setProperty ("response", juce::var (respObj));
            frPart->setProperty ("functionResponse", juce::var (frObj));
            parts.add (juce::var (frPart));
        }
        else
            continue;

        contentObj->setProperty ("parts", parts);
        contents.add (juce::var (contentObj));
    }

    // Build request
    auto* reqObj = new juce::DynamicObject();
    reqObj->setProperty ("contents", contents);

    // System instruction
    {
        auto* sysObj = new juce::DynamicObject();
        juce::Array<juce::var> sysParts;
        auto* sysPart = new juce::DynamicObject();
        sysPart->setProperty ("text", systemPrompt);
        sysParts.add (juce::var (sysPart));
        sysObj->setProperty ("parts", sysParts);
        reqObj->setProperty ("system_instruction", juce::var (sysObj));
    }

    // Tools
    if (! tools.empty())
    {
        juce::Array<juce::var> toolsArr;
        juce::Array<juce::var> funcDecls;

        for (auto& tool : tools)
        {
            auto* fdObj = new juce::DynamicObject();
            fdObj->setProperty ("name", tool.name);
            fdObj->setProperty ("description", tool.description);
            fdObj->setProperty ("parameters", tool.inputSchema);
            funcDecls.add (juce::var (fdObj));
        }

        auto* toolObj = new juce::DynamicObject();
        toolObj->setProperty ("function_declarations", funcDecls);
        toolsArr.add (juce::var (toolObj));
        reqObj->setProperty ("tools", toolsArr);
    }

    auto body = toJson (juce::var (reqObj));

    juce::String urlStr = "https://generativelanguage.googleapis.com/v1beta/models/"
                          + model + ":generateContent?key=" + apiKey;

    juce::URL url (urlStr);
    url = url.withPOSTData (body);

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                       .withExtraHeaders ("Content-Type: application/json")
                       .withConnectionTimeoutMs (60000)
                       .withNumRedirectsToFollow (5);

    auto stream = url.createInputStream (options);
    if (stream == nullptr)
    {
        response.isError = true;
        response.errorMessage = "Failed to connect to Google Gemini API";
        return response;
    }

    auto responseStr = stream->readEntireStreamAsString();
    auto json = parseJson (responseStr);

    if (! json.isObject())
    {
        response.isError = true;
        response.errorMessage = "Invalid response from Gemini API";
        return response;
    }

    if (json.hasProperty ("error"))
    {
        response.isError = true;
        response.errorMessage = json["error"]["message"].toString();
        return response;
    }

    // Parse usage
    if (json.hasProperty ("usageMetadata"))
    {
        response.inputTokens = (int) json["usageMetadata"]["promptTokenCount"];
        response.outputTokens = (int) json["usageMetadata"]["candidatesTokenCount"];
    }

    // Parse candidates
    auto candidates = json["candidates"];
    if (candidates.isArray() && candidates.size() > 0)
    {
        auto content = candidates[0]["content"];
        auto partsArr = content["parts"];

        if (partsArr.isArray())
        {
            for (int i = 0; i < partsArr.size(); ++i)
            {
                auto part = partsArr[i];

                if (part.hasProperty ("text"))
                {
                    response.textContent += part["text"].toString();
                }
                else if (part.hasProperty ("functionCall"))
                {
                    auto fc = part["functionCall"];
                    ChatMessage::ToolCall call;
                    call.id = "gemini_" + juce::String (i);
                    call.name = fc["name"].toString();
                    call.arguments = fc["args"];
                    response.toolCalls.push_back (std::move (call));
                }
            }
        }

        response.stopReason = candidates[0]["finishReason"].toString();
    }

    return response;
}

//==============================================================================
std::unique_ptr<AiProvider> createProvider (int providerType)
{
    switch (static_cast<AiProviderType> (providerType))
    {
        case AiProviderType::anthropic: return std::make_unique<AnthropicProvider>();
        case AiProviderType::openai:    return std::make_unique<OpenAiProvider>();
        case AiProviderType::google:    return std::make_unique<GeminiProvider>();
    }
    return std::make_unique<AnthropicProvider>();
}

} // namespace waive
