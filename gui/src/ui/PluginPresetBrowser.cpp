#include "PluginPresetBrowser.h"
#include "../theme/WaiveLookAndFeel.h"
#include "../tools/PluginPresetManager.h"
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace waive {

PluginPresetBrowser::PluginPresetBrowser()
    : presetManager (std::make_unique<PluginPresetManager>())
{
    addAndMakeVisible (presetComboBox);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (deleteButton);

    saveButton.setButtonText ("Save");
    deleteButton.setButtonText ("×");

    presetComboBox.onChange = [this] { onPresetSelected(); };
    saveButton.onClick = [this] { onSaveClicked(); };
    deleteButton.onClick = [this] { onDeleteClicked(); };

    presetComboBox.setTextWhenNothingSelected ("No preset selected");
    presetComboBox.setTextWhenNoChoicesAvailable ("No presets available");
}

PluginPresetBrowser::~PluginPresetBrowser() = default;

void PluginPresetBrowser::setPlugin (tracktion::engine::Plugin* plugin)
{
    currentPlugin = plugin;
    refreshPresetList();
}

void PluginPresetBrowser::refreshPresetList()
{
    presetComboBox.clear (juce::dontSendNotification);

    if (!currentPlugin)
    {
        saveButton.setEnabled (false);
        deleteButton.setEnabled (false);
        return;
    }

    saveButton.setEnabled (true);

    auto pluginId = PluginPresetManager::getPluginIdentifier (*currentPlugin);
    auto presets = presetManager->getPresetsForPlugin (pluginId);

    int itemId = 1;
    for (auto& preset : presets)
        presetComboBox.addItem (preset, itemId++);

    deleteButton.setEnabled (presetComboBox.getNumItems() > 0);
}

void PluginPresetBrowser::onPresetSelected()
{
    if (!currentPlugin)
        return;

    auto selectedText = presetComboBox.getText();
    if (selectedText.isEmpty())
        return;

    if (!presetManager->loadPreset (*currentPlugin, selectedText))
    {
        juce::NativeMessageBox::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Load Failed",
            "Failed to load preset \"" + selectedText + "\". The file may be corrupted or deleted.");
    }
}

void PluginPresetBrowser::onSaveClicked()
{
    if (!currentPlugin)
        return;

    juce::AlertWindow window ("Save Preset", "Enter a name for this preset:", juce::MessageBoxIconType::NoIcon);
    window.addTextEditor ("presetName", "", "Preset Name:");
    window.addButton ("Save", 1);
    window.addButton ("Cancel", 0);

    if (window.runModalLoop() == 1)
    {
        auto presetName = window.getTextEditorContents ("presetName").trim();
        if (presetName.isEmpty())
            return;

        if (presetManager->savePreset (*currentPlugin, presetName))
        {
            refreshPresetList();

            // Select the newly saved preset
            for (int i = 0; i < presetComboBox.getNumItems(); ++i)
            {
                if (presetComboBox.getItemText (i) == presetName)
                {
                    presetComboBox.setSelectedItemIndex (i, juce::dontSendNotification);
                    break;
                }
            }
        }
        else
        {
            juce::NativeMessageBox::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Save Failed",
                "Failed to save preset \"" + presetName + "\". Check disk space and permissions.");
        }
    }
}

void PluginPresetBrowser::onDeleteClicked()
{
    if (!currentPlugin)
        return;

    auto selectedText = presetComboBox.getText();
    if (selectedText.isEmpty())
        return;

    bool confirmed = juce::NativeMessageBox::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon,
        "Delete Preset",
        "Are you sure you want to delete the preset \"" + selectedText + "\"?");

    if (confirmed)
    {
        auto pluginId = PluginPresetManager::getPluginIdentifier (*currentPlugin);
        if (presetManager->deletePreset (pluginId, selectedText))
            refreshPresetList();
    }
}

void PluginPresetBrowser::paint (juce::Graphics& g)
{
    auto* palette = getWaivePalette (*this);
    g.fillAll (palette ? palette->panelBg : juce::Colour (0xff1a1a1a));
}

void PluginPresetBrowser::resized()
{
    auto bounds = getLocalBounds().reduced (4);

    auto buttonWidth = 60;
    auto deleteButtonWidth = 30;
    auto spacing = 4;

    auto deleteButtonBounds = bounds.removeFromRight (deleteButtonWidth);
    bounds.removeFromRight (spacing);

    auto saveButtonBounds = bounds.removeFromRight (buttonWidth);
    bounds.removeFromRight (spacing);

    deleteButton.setBounds (deleteButtonBounds);
    saveButton.setBounds (saveButtonBounds);
    presetComboBox.setBounds (bounds);
}

} // namespace waive
