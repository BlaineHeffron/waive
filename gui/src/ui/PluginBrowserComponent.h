#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "EditSession.h"

namespace te = tracktion;

class UndoableCommandHandler;

//==============================================================================
/** Plugin scanning + browser + per-track insert chain UI. */
class PluginBrowserComponent : public juce::Component,
                               private EditSession::Listener,
                               private juce::Timer
{
public:
    PluginBrowserComponent (EditSession& session, UndoableCommandHandler& handler);
    ~PluginBrowserComponent() override;

    void resized() override;

    // Test helpers for no-user UI coverage.
    bool selectTrackForTesting (int trackIndex);
    bool insertBuiltInPluginForTesting (const juce::String& pluginType);
    bool selectChainRowForTesting (int row);
    bool moveSelectedChainPluginForTesting (int delta);
    bool removeSelectedChainPluginForTesting();
    bool toggleSelectedChainPluginBypassForTesting();
    bool isChainPluginBypassedForTesting (int row) const;
    bool openSelectedChainPluginEditorForTesting();
    bool closeSelectedChainPluginEditorForTesting();
    int getChainPluginCountForTesting() const;
    juce::StringArray getChainPluginTypeOrderForTesting() const;
    int getAvailableInputCountForTesting() const;
    bool selectFirstAvailableInputForTesting();
    bool clearInputForTesting();
    bool hasAssignedInputForTesting() const;
    bool setArmEnabledForTesting (bool armed);
    bool isArmEnabledForTesting() const;
    bool setMonitorEnabledForTesting (bool monitorOn);
    bool isMonitorEnabledForTesting() const;
    bool setSendLevelDbForTesting (float gainDb);
    float getAuxSendGainDbForTesting (int busNum) const;
    void ensureReverbReturnOnMasterForTesting();
    int getMasterAuxReturnCountForTesting (int busNum) const;
    int getMasterReverbCountForTesting() const;

private:
    void editAboutToChange() override;
    void editChanged() override;
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
