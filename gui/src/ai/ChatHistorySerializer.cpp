#include "ChatHistorySerializer.h"

namespace waive
{

namespace ChatHistorySerializer
{

static juce::String roleToString (ChatMessage::Role role)
{
    switch (role)
    {
        case ChatMessage::Role::user:       return "user";
        case ChatMessage::Role::assistant:  return "assistant";
        case ChatMessage::Role::system:     return "system";
        case ChatMessage::Role::toolResult: return "toolResult";
        default:                            return "user";
    }
}

static ChatMessage::Role stringToRole (const juce::String& str)
{
    if (str == "assistant")  return ChatMessage::Role::assistant;
    if (str == "system")     return ChatMessage::Role::system;
    if (str == "toolResult") return ChatMessage::Role::toolResult;
    return ChatMessage::Role::user;
}

juce::var conversationToJson (const std::vector<ChatMessage>& messages)
{
    juce::Array<juce::var> arr;

    for (const auto& msg : messages)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("role", roleToString (msg.role));
        obj->setProperty ("content", msg.content);

        if (! msg.toolCalls.empty())
        {
            juce::Array<juce::var> toolCallsArr;
            for (const auto& tc : msg.toolCalls)
            {
                auto* tcObj = new juce::DynamicObject();
                tcObj->setProperty ("id", tc.id);
                tcObj->setProperty ("name", tc.name);
                tcObj->setProperty ("arguments", tc.arguments);
                toolCallsArr.add (juce::var (tcObj));
            }
            obj->setProperty ("toolCalls", toolCallsArr);
        }

        if (msg.toolCallId.isNotEmpty())
            obj->setProperty ("toolCallId", msg.toolCallId);

        arr.add (juce::var (obj));
    }

    return juce::var (arr);
}

std::vector<ChatMessage> conversationFromJson (const juce::var& json)
{
    std::vector<ChatMessage> messages;

    if (! json.isArray())
        return messages;

    const auto* arr = json.getArray();
    if (arr == nullptr)
        return messages;

    for (const auto& item : *arr)
    {
        if (! item.isObject())
            continue;

        auto* obj = item.getDynamicObject();
        if (obj == nullptr)
            continue;

        ChatMessage msg;
        msg.role = stringToRole (obj->getProperty ("role").toString());
        msg.content = obj->getProperty ("content").toString();

        if (obj->hasProperty ("toolCalls"))
        {
            auto toolCallsVar = obj->getProperty ("toolCalls");
            if (toolCallsVar.isArray())
            {
                const auto* tcArr = toolCallsVar.getArray();
                if (tcArr != nullptr)
                {
                    for (const auto& tcItem : *tcArr)
                    {
                        if (! tcItem.isObject())
                            continue;

                        auto* tcObj = tcItem.getDynamicObject();
                        if (tcObj == nullptr)
                            continue;

                        ChatMessage::ToolCall tc;
                        tc.id = tcObj->getProperty ("id").toString();
                        tc.name = tcObj->getProperty ("name").toString();
                        tc.arguments = tcObj->getProperty ("arguments");
                        msg.toolCalls.push_back (std::move (tc));
                    }
                }
            }
        }

        if (obj->hasProperty ("toolCallId"))
            msg.toolCallId = obj->getProperty ("toolCallId").toString();

        messages.push_back (std::move (msg));
    }

    return messages;
}

bool saveChatHistory (const std::vector<ChatMessage>& messages, const juce::File& file)
{
    auto json = conversationToJson (messages);
    auto jsonText = juce::JSON::toString (json, false);

    auto parentDir = file.getParentDirectory();
    if (! parentDir.exists())
    {
        auto result = parentDir.createDirectory();
        if (! result.wasOk())
            return false;
    }

    return file.replaceWithText (jsonText);
}

std::vector<ChatMessage> loadChatHistory (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};

    auto text = file.loadFileAsString();
    if (text.isEmpty())
        return {};

    auto json = juce::JSON::parse (text);
    if (! json.isArray())
        return {};

    return conversationFromJson (json);
}

} // namespace ChatHistorySerializer

} // namespace waive
