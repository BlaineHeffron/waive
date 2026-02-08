#include "AiSettings.h"

namespace waive
{

AiSettings::AiSettings()
{
    providers.push_back ({ AiProviderType::anthropic, "Anthropic", {}, "claude-sonnet-4-5-20250929",
                           { "claude-sonnet-4-5-20250929", "claude-haiku-4-5-20251001" } });
    providers.push_back ({ AiProviderType::openai, "OpenAI", {}, "gpt-4o",
                           { "gpt-4o", "gpt-4o-mini" } });
    providers.push_back ({ AiProviderType::google, "Google", {}, "gemini-2.0-flash",
                           { "gemini-2.0-flash", "gemini-2.5-pro" } });
}

void AiSettings::setApiKey (AiProviderType provider, const juce::String& key)
{
    getMutableConfig (provider).apiKey = key;
    autoSave();
}

juce::String AiSettings::getApiKey (AiProviderType provider) const
{
    return getProviderConfig (provider).apiKey;
}

void AiSettings::setSelectedModel (AiProviderType provider, const juce::String& model)
{
    getMutableConfig (provider).selectedModel = model;
}

juce::String AiSettings::getSelectedModel (AiProviderType provider) const
{
    return getProviderConfig (provider).selectedModel;
}

void AiSettings::setActiveProvider (AiProviderType provider)
{
    activeProvider = provider;
}

AiProviderType AiSettings::getActiveProvider() const
{
    return activeProvider;
}

void AiSettings::setAutoApply (bool enabled)
{
    autoApply = enabled;
}

bool AiSettings::isAutoApply() const
{
    return autoApply;
}

const AiProviderConfig& AiSettings::getProviderConfig (AiProviderType provider) const
{
    for (auto& p : providers)
        if (p.type == provider)
            return p;

    return providers.front();
}

AiProviderConfig& AiSettings::getMutableConfig (AiProviderType provider)
{
    for (auto& p : providers)
        if (p.type == provider)
            return p;

    return providers.front();
}

juce::String AiSettings::providerKey (AiProviderType type)
{
    switch (type)
    {
        case AiProviderType::anthropic: return "anthropic";
        case AiProviderType::openai:    return "openai";
        case AiProviderType::google:    return "google";
    }
    return "anthropic";
}

void AiSettings::loadFromProperties (juce::ApplicationProperties& props)
{
    boundProperties = &props;

    auto* settings = props.getUserSettings();
    if (settings == nullptr)
        return;

    for (auto& p : providers)
    {
        auto key = providerKey (p.type);
        p.apiKey = settings->getValue ("ai_" + key + "_key", "");
        auto model = settings->getValue ("ai_" + key + "_model", "");
        if (model.isNotEmpty())
            p.selectedModel = model;
    }

    auto activeStr = settings->getValue ("ai_active_provider", "anthropic");
    if (activeStr == "openai")        activeProvider = AiProviderType::openai;
    else if (activeStr == "google")   activeProvider = AiProviderType::google;
    else                              activeProvider = AiProviderType::anthropic;

    autoApply = settings->getBoolValue ("ai_auto_apply", true);
}

void AiSettings::saveToProperties (juce::ApplicationProperties& props) const
{
    auto* settings = props.getUserSettings();
    if (settings == nullptr)
        return;

    for (auto& p : providers)
    {
        auto key = providerKey (p.type);
        settings->setValue ("ai_" + key + "_key", p.apiKey);
        settings->setValue ("ai_" + key + "_model", p.selectedModel);
    }

    settings->setValue ("ai_active_provider", providerKey (activeProvider));
    settings->setValue ("ai_auto_apply", autoApply);
    settings->saveIfNeeded();
}

void AiSettings::autoSave()
{
    if (boundProperties != nullptr)
        saveToProperties (*boundProperties);
}

} // namespace waive
