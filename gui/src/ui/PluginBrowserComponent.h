#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class EditSession;
class UndoableCommandHandler;

//==============================================================================
/** Plugin scanning + browser + per-track insert chain UI. */
class PluginBrowserComponent : public juce::Component,
                               private juce::Timer
{
public:
    PluginBrowserComponent (EditSession& session, UndoableCommandHandler& handler);
    ~PluginBrowserComponent() override;

    void resized() override;

private:
    void timerCallback() override;
    void rebuildTrackListIfNeeded();

    void updateControlsFromSelection();

    te::AudioTrack* getSelectedTrack() const;
    te::PluginList& getSelectedPluginList() const;

    void insertSelectedBrowserPlugin();
    void removeSelectedChainPlugin();
    void moveSelectedChainPlugin (int delta);
    void toggleSelectedChainPluginBypass();
    void openSelectedChainPluginEditor();
    void closeSelectedChainPluginEditor();

    void ensureAuxSendOnSelectedTrack();
    void updateAuxSendGainFromSlider();
    void ensureReverbReturnOnMaster();

    void applyInputSelection();
    void setArmEnabled (bool armed);
    void setMonitorEnabled (bool monitorOn);

    //==============================================================================
    EditSession& editSession;
    UndoableCommandHandler& commandHandler;

    std::unique_ptr<juce::PluginListComponent> pluginList;
    juce::TextButton insertButton { "Insert" };

    juce::ComboBox trackCombo;
    juce::Label trackLabel { {}, "Track" };

    juce::ListBox chainList;
    juce::TextButton removeButton { "Remove" };
    juce::TextButton upButton { "Up" };
    juce::TextButton downButton { "Down" };
    juce::TextButton bypassButton { "Bypass" };
    juce::TextButton openEditorButton { "Open UI" };
    juce::TextButton closeEditorButton { "Close UI" };

    juce::Label inputLabel { {}, "Input" };
    juce::ComboBox inputCombo;
    juce::ToggleButton armButton { "Arm" };
    juce::ToggleButton monitorButton { "Monitor" };

    juce::Label sendLabel { {}, "Send A" };
    juce::Slider sendSlider;
    juce::TextButton addReverbReturnButton { "Add Reverb Return" };

    struct ChainModel;
    std::unique_ptr<ChainModel> chainModel;

    int lastTrackCount = -1;
};

