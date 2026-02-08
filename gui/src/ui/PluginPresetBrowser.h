#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace tracktion { inline namespace engine { class Plugin; }}

namespace waive {
    class PluginPresetManager;
}

namespace waive {

class PluginPresetBrowser : public juce::Component
{
public:
    PluginPresetBrowser();
    ~PluginPresetBrowser() override;

    void setPlugin (tracktion::engine::Plugin* plugin);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void refreshPresetList();
    void onPresetSelected();
    void onSaveClicked();
    void onDeleteClicked();

    tracktion::engine::Plugin* currentPlugin = nullptr;
    std::unique_ptr<waive::PluginPresetManager> presetManager;

    juce::ComboBox presetComboBox;
    juce::TextButton saveButton;
    juce::TextButton deleteButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginPresetBrowser)
};

} // namespace waive
