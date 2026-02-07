#pragma once

#include <JuceHeader.h>

namespace waive
{

enum class AiProviderType
{
    anthropic,
    openai,
    google
};

struct AiProviderConfig
{
    AiProviderType type;
    juce::String displayName;
    juce::String apiKey;
    juce::String selectedModel;
    juce::StringArray availableModels;
};

class AiSettings
{
public:
    AiSettings();

    void setApiKey (AiProviderType provider, const juce::String& key);
    juce::String getApiKey (AiProviderType provider) const;

    void setSelectedModel (AiProviderType provider, const juce::String& model);
    juce::String getSelectedModel (AiProviderType provider) const;

    void setActiveProvider (AiProviderType provider);
    AiProviderType getActiveProvider() const;

    void setAutoApply (bool enabled);
    bool isAutoApply() const;

    const AiProviderConfig& getProviderConfig (AiProviderType provider) const;
    const std::vector<AiProviderConfig>& getAllProviders() const { return providers; }

    void loadFromProperties (juce::ApplicationProperties& props);
    void saveToProperties (juce::ApplicationProperties& props) const;

private:
    static juce::String providerKey (AiProviderType type);
    AiProviderConfig& getMutableConfig (AiProviderType provider);

    std::vector<AiProviderConfig> providers;
    AiProviderType activeProvider = AiProviderType::anthropic;
    bool autoApply = true;
};

} // namespace waive
