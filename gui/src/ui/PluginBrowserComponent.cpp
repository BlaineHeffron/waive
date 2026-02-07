#include "PluginBrowserComponent.h"

#include "EditSession.h"
#include "UndoableCommandHandler.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"

#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <limits>

namespace te = tracktion;

namespace
{
constexpr int trackItemIdBase = 1000;
constexpr int masterItemId = 1;

bool isDefaultTrackPlugin (te::Plugin& p)
{
    return dynamic_cast<te::VolumeAndPanPlugin*> (&p) != nullptr
        || dynamic_cast<te::LevelMeterPlugin*> (&p) != nullptr;
}

te::Plugin::Array getChainPlugins (te::PluginList& list)
{
    te::Plugin::Array filtered;
    for (auto* p : list.getPlugins())
        if (p != nullptr && ! isDefaultTrackPlugin (*p))
            filtered.add (p);
    return filtered;
}

te::Plugin::Ptr createPluginFromDescription (te::Edit& edit, const juce::PluginDescription& desc)
{
    // Built-in plugins use fileOrIdentifier = xmlTypeName
    if (desc.pluginFormatName == te::PluginManager::builtInPluginFormatName)
    {
        auto v = te::createValueTree (te::IDs::PLUGIN,
                                      te::IDs::type, desc.fileOrIdentifier);
        return edit.getPluginCache().createNewPlugin (v);
    }

    return edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);
}

te::WaveInputDevice* findWaveInputDeviceByName (te::Engine& engine, const juce::String& name)
{
    for (auto* d : engine.getDeviceManager().getWaveInputDevices())
        if (d != nullptr && d->getName() == name)
            return d;

    return nullptr;
}

te::InputDeviceInstance* findWaveInputInstanceOnTrack (te::Edit& edit, te::AudioTrack& track)
{
    for (auto* idi : edit.getEditInputDevices().getDevicesForTargetTrack (track))
        if (idi != nullptr && idi->getInputDevice().getDeviceType() == te::InputDevice::DeviceType::waveDevice)
            return idi;

    return nullptr;
}

te::AuxSendPlugin* findAuxSend (te::AudioTrack& track, int busNum)
{
    for (auto* p : track.pluginList)
        if (auto* s = dynamic_cast<te::AuxSendPlugin*> (p))
            if ((int) s->busNumber == busNum)
                return s;
    return nullptr;
}

te::AuxReturnPlugin* findAuxReturn (te::Edit& edit, int busNum)
{
    for (auto* p : edit.getMasterPluginList())
        if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (p))
            if ((int) r->busNumber == busNum)
                return r;
    return nullptr;
}

} // namespace

//==============================================================================
struct PluginBrowserComponent::ChainModel : public juce::ListBoxModel
{
    explicit ChainModel (PluginBrowserComponent& o) : owner (o) {}

    int getNumRows() override
    {
        auto& edit = owner.editSession.getEdit();

        if (owner.trackCombo.getSelectedId() == masterItemId)
            return getChainPlugins (edit.getMasterPluginList()).size();

        if (auto* t = owner.getSelectedTrack())
            return getChainPlugins (t->pluginList).size();

        return 0;
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        auto& edit = owner.editSession.getEdit();
        auto* list = owner.trackCombo.getSelectedId() == masterItemId
                         ? &edit.getMasterPluginList()
                         : (owner.getSelectedTrack() != nullptr ? &owner.getSelectedTrack()->pluginList : nullptr);

        if (list == nullptr)
            return;

        auto plugins = getChainPlugins (*list);
        if (! juce::isPositiveAndBelow (row, plugins.size()))
            return;

        auto* p = plugins[row].get();
        if (p == nullptr)
            return;

        if (selected)
        {
            auto* pal = waive::getWaivePalette (owner);
            g.fillAll (pal ? pal->selection : juce::Colour (0xff2f4f4f));
        }

        juce::String text = p->getName();
        if (! p->isEnabled())
            text = "[byp] " + text;

        {
            auto* pal = waive::getWaivePalette (owner);
            g.setColour (pal ? pal->textPrimary : juce::Colour (0xffffffff));
        }
        g.drawFittedText (text, 6, 0, width - 12, height, juce::Justification::centredLeft, 1);
    }

    PluginBrowserComponent& owner;
};

//==============================================================================
PluginBrowserComponent::PluginBrowserComponent (EditSession& session, UndoableCommandHandler& handler)
    : editSession (session), commandHandler (handler)
{
    editSession.addListener (this);

    // Plugin browser
    auto& pm = editSession.getEngine().getPluginManager();
    auto deadMans = editSession.getEngine().getPropertyStorage()
                        .getAppCacheFolder()
                        .getChildFile ("waive_plugin_scan_dead_mans_pedal.txt");

    deadMans.getParentDirectory().createDirectory();

    pluginList = std::make_unique<juce::PluginListComponent> (
        pm.pluginFormatManager,
        pm.knownPluginList,
        deadMans,
        &editSession.getEngine().getPropertyStorage().getPropertiesFile(),
        false);

    addAndMakeVisible (pluginList.get());

    insertButton.onClick = [this] { insertSelectedBrowserPlugin(); };
    insertButton.setTitle ("Insert Plugin");
    insertButton.setDescription ("Insert selected plugin to track or master");
    insertButton.setWantsKeyboardFocus (true);
    addAndMakeVisible (insertButton);

    // Track picker
    trackLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (trackLabel);
    trackCombo.onChange = [this] { updateControlsFromSelection(); };
    trackCombo.setTitle ("Track Selector");
    trackCombo.setDescription ("Select track for plugin chain");
    trackCombo.setWantsKeyboardFocus (true);
    addAndMakeVisible (trackCombo);

    // Chain list
    chainModel = std::make_unique<ChainModel> (*this);
    chainList.setModel (chainModel.get());
    chainList.setTitle ("Plugin Chain");
    chainList.setDescription ("Plugin chain for selected track");
    chainList.setWantsKeyboardFocus (true);
    addAndMakeVisible (chainList);

    removeButton.onClick = [this] { removeSelectedChainPlugin(); };
    upButton.onClick = [this] { moveSelectedChainPlugin (-1); };
    downButton.onClick = [this] { moveSelectedChainPlugin (1); };
    bypassButton.onClick = [this] { toggleSelectedChainPluginBypass(); };
    openEditorButton.onClick = [this] { openSelectedChainPluginEditor(); };
    closeEditorButton.onClick = [this] { closeSelectedChainPluginEditor(); };

    removeButton.setTitle ("Remove");
    removeButton.setDescription ("Remove selected plugin from chain");
    upButton.setTitle ("Up");
    upButton.setDescription ("Move plugin up in chain");
    downButton.setTitle ("Down");
    downButton.setDescription ("Move plugin down in chain");
    bypassButton.setTitle ("Bypass");
    bypassButton.setDescription ("Toggle plugin bypass");
    openEditorButton.setTitle ("Editor");
    openEditorButton.setDescription ("Open plugin editor window");
    closeEditorButton.setTitle ("Close Editor");
    closeEditorButton.setDescription ("Close plugin editor window");

    removeButton.setWantsKeyboardFocus (true);
    upButton.setWantsKeyboardFocus (true);
    downButton.setWantsKeyboardFocus (true);
    bypassButton.setWantsKeyboardFocus (true);
    openEditorButton.setWantsKeyboardFocus (true);
    closeEditorButton.setWantsKeyboardFocus (true);

    addAndMakeVisible (removeButton);
    addAndMakeVisible (upButton);
    addAndMakeVisible (downButton);
    addAndMakeVisible (bypassButton);
    addAndMakeVisible (openEditorButton);
    addAndMakeVisible (closeEditorButton);

    // Input + record controls (per track)
    inputLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (inputLabel);

    inputCombo.setTextWhenNothingSelected ("None");
    inputCombo.onChange = [this] { applyInputSelection(); };
    inputCombo.setTitle ("Audio Input");
    inputCombo.setDescription ("Select audio input device for track");
    inputCombo.setWantsKeyboardFocus (true);
    addAndMakeVisible (inputCombo);

    armButton.onClick = [this] { setArmEnabled (armButton.getToggleState()); };
    monitorButton.onClick = [this] { setMonitorEnabled (monitorButton.getToggleState()); };
    armButton.setTitle ("Arm");
    armButton.setDescription ("Arm track for recording");
    monitorButton.setTitle ("Monitor");
    monitorButton.setDescription ("Monitor input signal");
    armButton.setWantsKeyboardFocus (true);
    monitorButton.setWantsKeyboardFocus (true);
    addAndMakeVisible (armButton);
    addAndMakeVisible (monitorButton);

    // Routing: one send, one reverb return
    sendLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (sendLabel);

    sendSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sendSlider.setRange (-60.0, 6.0, 0.1);
    sendSlider.setValue (-60.0, juce::dontSendNotification);
    sendSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 18);
    sendSlider.setTextValueSuffix (" dB");
    sendSlider.onValueChange = [this] { updateAuxSendGainFromSlider(); };
    sendSlider.onDragEnd = [this] { editSession.endCoalescedTransaction(); };
    sendSlider.setTitle ("Send Level");
    sendSlider.setDescription ("Aux send level in dB");
    sendSlider.setWantsKeyboardFocus (true);
    addAndMakeVisible (sendSlider);

    addReverbReturnButton.onClick = [this] { ensureReverbReturnOnMaster(); };
    addReverbReturnButton.setTitle ("Add Reverb");
    addReverbReturnButton.setDescription ("Add reverb return to master");
    addReverbReturnButton.setWantsKeyboardFocus (true);
    addAndMakeVisible (addReverbReturnButton);

    rebuildTrackListIfNeeded();
    updateControlsFromSelection();

    startTimerHz (2);
}

PluginBrowserComponent::~PluginBrowserComponent()
{
    chainList.setModel (nullptr);
    stopTimer();
    editSession.removeListener (this);
}

void PluginBrowserComponent::paint (juce::Graphics& g)
{
    if (scanningInProgress)
    {
        g.setFont (waive::Fonts::body());
        auto* pal = waive::getWaivePalette (*this);
        g.setColour (pal ? pal->textMuted : juce::Colour (0xff808080));
        g.drawText ("Scanning plugins...", getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    auto& knownPlugins = editSession.getEdit().engine.getPluginManager().knownPluginList;
    if (knownPlugins.getNumTypes() == 0)
    {
        g.setFont (waive::Fonts::body());
        auto* pal = waive::getWaivePalette (*this);
        g.setColour (pal ? pal->textMuted : juce::Colour (0xff808080));
        g.drawText ("Click 'Scan' to find installed plugins", getLocalBounds(), juce::Justification::centred, true);
    }
}

void PluginBrowserComponent::setScanning (bool scanning)
{
    scanningInProgress = scanning;
    repaint();
}

void PluginBrowserComponent::resized()
{
    auto bounds = getLocalBounds().reduced (waive::Spacing::sm);

    auto topRow = bounds.removeFromTop (28);
    trackLabel.setBounds (topRow.removeFromLeft (44));
    trackCombo.setBounds (topRow.removeFromLeft (240));
    topRow.removeFromLeft (waive::Spacing::sm);
    insertButton.setBounds (topRow.removeFromLeft (90));
    topRow.removeFromLeft (waive::Spacing::sm);
    addReverbReturnButton.setBounds (topRow.removeFromLeft (160));

    bounds.removeFromTop (waive::Spacing::sm);

    // Split: plugin browser (left), chain + routing/input (right)
    auto left = bounds.removeFromLeft (juce::jmax (360, bounds.getWidth() / 2));
    pluginList->setBounds (left);

    bounds.removeFromLeft (waive::Spacing::sm);

    auto right = bounds;
    auto ioRow = right.removeFromTop (28);
    inputLabel.setBounds (ioRow.removeFromLeft (44));
    inputCombo.setBounds (ioRow.removeFromLeft (220));
    ioRow.removeFromLeft (waive::Spacing::sm);
    armButton.setBounds (ioRow.removeFromLeft (60));
    ioRow.removeFromLeft (waive::Spacing::xs);
    monitorButton.setBounds (ioRow.removeFromLeft (84));

    right.removeFromTop (6);

    auto sendRow = right.removeFromTop (28);
    sendLabel.setBounds (sendRow.removeFromLeft (52));
    sendSlider.setBounds (sendRow);

    right.removeFromTop (waive::Spacing::sm);

    auto buttonRow = right.removeFromTop (28);
    removeButton.setBounds (buttonRow.removeFromLeft (80));
    buttonRow.removeFromLeft (6);
    upButton.setBounds (buttonRow.removeFromLeft (50));
    buttonRow.removeFromLeft (waive::Spacing::xs);
    downButton.setBounds (buttonRow.removeFromLeft (60));
    buttonRow.removeFromLeft (6);
    bypassButton.setBounds (buttonRow.removeFromLeft (80));
    buttonRow.removeFromLeft (6);
    openEditorButton.setBounds (buttonRow.removeFromLeft (80));
    buttonRow.removeFromLeft (6);
    closeEditorButton.setBounds (buttonRow.removeFromLeft (80));

    right.removeFromTop (waive::Spacing::sm);
    chainList.setBounds (right);
}

bool PluginBrowserComponent::selectTrackForTesting (int trackIndex)
{
    const int targetItemId = (trackIndex < 0) ? masterItemId : (trackItemIdBase + trackIndex);
    bool found = false;

    for (int i = 0; i < trackCombo.getNumItems(); ++i)
    {
        if (trackCombo.getItemId (i) == targetItemId)
        {
            found = true;
            break;
        }
    }

    if (! found)
        return false;

    trackCombo.setSelectedId (targetItemId, juce::sendNotificationSync);
    return trackCombo.getSelectedId() == targetItemId;
}

bool PluginBrowserComponent::insertBuiltInPluginForTesting (const juce::String& pluginType)
{
    const auto type = pluginType.trim();
    if (type.isEmpty())
        return false;

    bool inserted = false;
    editSession.performEdit ("Insert Built-In Plugin (Test)", [&] (te::Edit& edit)
    {
        auto state = te::createValueTree (te::IDs::PLUGIN,
                                          te::IDs::type, type);
        auto plugin = edit.getPluginCache().createNewPlugin (state);
        if (plugin == nullptr)
            return;

        auto& list = getSelectedPluginList();
        list.insertPlugin (plugin, 0, nullptr);
        inserted = true;
    });

    chainList.updateContent();
    chainList.repaint();
    return inserted;
}

bool PluginBrowserComponent::selectChainRowForTesting (int row)
{
    chainList.updateContent();

    if (! juce::isPositiveAndBelow (row, chainModel->getNumRows()))
        return false;

    chainList.selectRow (row);
    return chainList.getSelectedRow() == row;
}

bool PluginBrowserComponent::moveSelectedChainPluginForTesting (int delta)
{
    if (delta == 0 || chainList.getSelectedRow() < 0)
        return false;

    const auto before = getChainPluginTypeOrderForTesting();
    moveSelectedChainPlugin (delta);
    const auto after = getChainPluginTypeOrderForTesting();
    return after != before;
}

bool PluginBrowserComponent::removeSelectedChainPluginForTesting()
{
    const auto before = getChainPluginCountForTesting();
    removeSelectedChainPlugin();
    return getChainPluginCountForTesting() < before;
}

bool PluginBrowserComponent::toggleSelectedChainPluginBypassForTesting()
{
    const int row = chainList.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, getChainPluginCountForTesting()))
        return false;

    const bool wasBypassed = isChainPluginBypassedForTesting (row);
    toggleSelectedChainPluginBypass();
    return isChainPluginBypassedForTesting (row) != wasBypassed;
}

bool PluginBrowserComponent::openSelectedChainPluginEditorForTesting()
{
    const int row = chainList.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, getChainPluginCountForTesting()))
        return false;

    openSelectedChainPluginEditor();
    return true;
}

bool PluginBrowserComponent::closeSelectedChainPluginEditorForTesting()
{
    const int row = chainList.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, getChainPluginCountForTesting()))
        return false;

    closeSelectedChainPluginEditor();
    return true;
}

bool PluginBrowserComponent::isChainPluginBypassedForTesting (int row) const
{
    auto& list = getSelectedPluginList();
    auto plugins = getChainPlugins (list);
    if (! juce::isPositiveAndBelow (row, plugins.size()))
        return false;

    if (auto plugin = plugins[row])
        return ! plugin->isEnabled();

    return false;
}

int PluginBrowserComponent::getChainPluginCountForTesting() const
{
    return getChainPlugins (getSelectedPluginList()).size();
}

juce::StringArray PluginBrowserComponent::getChainPluginTypeOrderForTesting() const
{
    juce::StringArray order;
    auto& list = getSelectedPluginList();
    auto plugins = getChainPlugins (list);

    for (auto plugin : plugins)
    {
        if (plugin == nullptr)
            continue;

        auto type = plugin->state.getProperty (te::IDs::type).toString();
        if (type.isEmpty())
            type = plugin->getName();
        order.add (type);
    }

    return order;
}

int PluginBrowserComponent::getAvailableInputCountForTesting() const
{
    return juce::jmax (0, inputCombo.getNumItems() - 1);
}

bool PluginBrowserComponent::selectFirstAvailableInputForTesting()
{
    if (getSelectedTrack() == nullptr || getAvailableInputCountForTesting() <= 0)
        return false;

    inputCombo.setSelectedId (2, juce::sendNotificationSync);
    return hasAssignedInputForTesting();
}

bool PluginBrowserComponent::clearInputForTesting()
{
    if (getSelectedTrack() == nullptr)
        return false;

    inputCombo.setSelectedId (1, juce::sendNotificationSync);
    return ! hasAssignedInputForTesting();
}

bool PluginBrowserComponent::hasAssignedInputForTesting() const
{
    auto* track = getSelectedTrack();
    if (track == nullptr)
        return false;

    return findWaveInputInstanceOnTrack (editSession.getEdit(), *track) != nullptr;
}

bool PluginBrowserComponent::setArmEnabledForTesting (bool armed)
{
    if (! hasAssignedInputForTesting())
        return false;

    setArmEnabled (armed);
    return isArmEnabledForTesting() == armed;
}

bool PluginBrowserComponent::isArmEnabledForTesting() const
{
    auto* track = getSelectedTrack();
    if (track == nullptr)
        return false;

    if (auto* idi = findWaveInputInstanceOnTrack (editSession.getEdit(), *track))
        return idi->isRecordingEnabled (track->itemID);

    return false;
}

bool PluginBrowserComponent::setMonitorEnabledForTesting (bool monitorOn)
{
    if (! hasAssignedInputForTesting())
        return false;

    setMonitorEnabled (monitorOn);
    return isMonitorEnabledForTesting() == monitorOn;
}

bool PluginBrowserComponent::isMonitorEnabledForTesting() const
{
    auto* track = getSelectedTrack();
    if (track == nullptr)
        return false;

    if (auto* idi = findWaveInputInstanceOnTrack (editSession.getEdit(), *track))
        return idi->getInputDevice().getMonitorMode() == te::InputDevice::MonitorMode::on;

    return false;
}

bool PluginBrowserComponent::setSendLevelDbForTesting (float gainDb)
{
    if (getSelectedTrack() == nullptr)
        return false;

    sendSlider.setValue (juce::jlimit (-60.0, 6.0, (double) gainDb), juce::sendNotificationSync);
    const auto actual = getAuxSendGainDbForTesting (0);
    return std::isfinite (actual) && std::abs ((double) gainDb - actual) < 0.6;
}

float PluginBrowserComponent::getAuxSendGainDbForTesting (int busNum) const
{
    auto* track = getSelectedTrack();
    if (track == nullptr)
        return std::numeric_limits<float>::quiet_NaN();

    if (auto* send = findAuxSend (*track, busNum))
        return send->getGainDb();

    return std::numeric_limits<float>::quiet_NaN();
}

void PluginBrowserComponent::ensureReverbReturnOnMasterForTesting()
{
    const int previousId = trackCombo.getSelectedId();
    trackCombo.setSelectedId (masterItemId, juce::dontSendNotification);
    updateControlsFromSelection();
    ensureReverbReturnOnMaster();

    if (previousId != 0 && previousId != masterItemId)
    {
        trackCombo.setSelectedId (previousId, juce::dontSendNotification);
        updateControlsFromSelection();
    }
}

int PluginBrowserComponent::getMasterAuxReturnCountForTesting (int busNum) const
{
    int count = 0;
    for (auto* plugin : editSession.getEdit().getMasterPluginList())
        if (auto* auxReturn = dynamic_cast<te::AuxReturnPlugin*> (plugin))
            if ((int) auxReturn->busNumber == busNum)
                ++count;
    return count;
}

int PluginBrowserComponent::getMasterReverbCountForTesting() const
{
    int count = 0;
    for (auto* plugin : editSession.getEdit().getMasterPluginList())
        if (dynamic_cast<te::ReverbPlugin*> (plugin) != nullptr)
            ++count;
    return count;
}

void PluginBrowserComponent::timerCallback()
{
    rebuildTrackListIfNeeded();
}

void PluginBrowserComponent::editAboutToChange()
{
    lastTrackCount = -1;
}

void PluginBrowserComponent::editChanged()
{
    lastTrackCount = -1;
    rebuildTrackListIfNeeded();
    updateControlsFromSelection();
}

void PluginBrowserComponent::rebuildTrackListIfNeeded()
{
    auto& edit = editSession.getEdit();
    auto tracks = te::getAudioTracks (edit);
    if (tracks.size() == lastTrackCount && trackCombo.getNumItems() > 0)
        return;

    const int previous = trackCombo.getSelectedId();

    trackCombo.clear (juce::dontSendNotification);
    trackCombo.addItem ("Master", masterItemId);

    int idx = 0;
    for (auto* t : tracks)
    {
        if (t == nullptr)
            continue;

        const int id = trackItemIdBase + idx;
        trackCombo.addItem (t->getName(), id);
        ++idx;
    }

    lastTrackCount = tracks.size();

    auto hasItemId = [&] (int id)
    {
        for (int i = 0; i < trackCombo.getNumItems(); ++i)
            if (trackCombo.getItemId (i) == id)
                return true;
        return false;
    };

    if (previous != 0 && hasItemId (previous))
        trackCombo.setSelectedId (previous, juce::dontSendNotification);
    else
        trackCombo.setSelectedId (tracks.isEmpty() ? masterItemId : trackItemIdBase, juce::dontSendNotification);

    updateControlsFromSelection();
}

te::AudioTrack* PluginBrowserComponent::getSelectedTrack() const
{
    auto id = trackCombo.getSelectedId();
    if (id < trackItemIdBase)
        return nullptr;

    const int index = id - trackItemIdBase;
    auto tracks = te::getAudioTracks (editSession.getEdit());
    if (! juce::isPositiveAndBelow (index, tracks.size()))
        return nullptr;

    return tracks[index];
}

te::PluginList& PluginBrowserComponent::getSelectedPluginList() const
{
    auto& edit = editSession.getEdit();
    if (trackCombo.getSelectedId() == masterItemId)
        return edit.getMasterPluginList();

    if (auto* t = getSelectedTrack())
        return t->pluginList;

    return edit.getMasterPluginList();
}

void PluginBrowserComponent::updateControlsFromSelection()
{
    const bool isMaster = trackCombo.getSelectedId() == masterItemId;

    inputLabel.setEnabled (! isMaster);
    inputCombo.setEnabled (! isMaster);
    armButton.setEnabled (! isMaster);
    monitorButton.setEnabled (! isMaster);
    sendLabel.setEnabled (! isMaster);
    sendSlider.setEnabled (! isMaster);

    chainList.updateContent();
    chainList.repaint();

    // Refresh input list for selected track
    inputCombo.clear (juce::dontSendNotification);
    inputCombo.addItem ("None", 1);

    int nextId = 2;
    for (auto* d : editSession.getEngine().getDeviceManager().getWaveInputDevices())
        if (d != nullptr)
            inputCombo.addItem (d->getName(), nextId++);

    inputCombo.setSelectedId (1, juce::dontSendNotification);

    armButton.setToggleState (false, juce::dontSendNotification);
    monitorButton.setToggleState (false, juce::dontSendNotification);
    sendSlider.setValue (-60.0, juce::dontSendNotification);

    if (auto* t = getSelectedTrack())
    {
        auto& edit = editSession.getEdit();
        if (auto* idi = findWaveInputInstanceOnTrack (edit, *t))
        {
            const auto deviceName = idi->getInputDevice().getName();
            for (int i = 0; i < inputCombo.getNumItems(); ++i)
            {
                if (inputCombo.getItemText (i) == deviceName)
                {
                    inputCombo.setSelectedItemIndex (i, juce::dontSendNotification);
                    break;
                }
            }

            armButton.setToggleState (idi->isRecordingEnabled (t->itemID), juce::dontSendNotification);
            monitorButton.setToggleState (idi->getInputDevice().getMonitorMode() == te::InputDevice::MonitorMode::on,
                                          juce::dontSendNotification);
        }

        if (auto* send = findAuxSend (*t, 0))
            sendSlider.setValue (send->getGainDb(), juce::dontSendNotification);
    }
}

void PluginBrowserComponent::insertSelectedBrowserPlugin()
{
    auto& pm = editSession.getEngine().getPluginManager();
    auto row = pluginList->getTableListBox().getSelectedRow();

    auto types = pm.knownPluginList.getTypes();
    if (! juce::isPositiveAndBelow (row, types.size()))
        return;

    auto desc = types[row];

    editSession.performEdit ("Insert Plugin", [&] (te::Edit& edit)
    {
        auto plugin = createPluginFromDescription (edit, desc);
        if (plugin == nullptr)
            return;

        auto& list = getSelectedPluginList();
        list.insertPlugin (plugin, 0, nullptr);
    });

    chainList.updateContent();
    chainList.repaint();
}

void PluginBrowserComponent::removeSelectedChainPlugin()
{
    const int row = chainList.getSelectedRow();
    if (row < 0)
        return;

    editSession.performEdit ("Remove Plugin", [&] (te::Edit&)
    {
        auto& list = getSelectedPluginList();
        auto plugins = getChainPlugins (list);
        if (! juce::isPositiveAndBelow (row, plugins.size()))
            return;

        if (auto p = plugins[row])
            p->deleteFromParent();
    });

    chainList.updateContent();
    chainList.repaint();
}

void PluginBrowserComponent::moveSelectedChainPlugin (int delta)
{
    const int row = chainList.getSelectedRow();
    if (row < 0 || delta == 0)
        return;

    editSession.performEdit ("Move Plugin", [&] (te::Edit& edit)
    {
        auto& list = getSelectedPluginList();
        auto plugins = getChainPlugins (list);
        if (! juce::isPositiveAndBelow (row, plugins.size()))
            return;

        const int newRow = juce::jlimit (0, plugins.size() - 1, row + delta);
        if (newRow == row)
            return;

        auto* p = plugins[row].get();
        if (p == nullptr)
            return;

        auto stateIndex = list.state.indexOf (p->state);
        if (stateIndex < 0)
            return;

        // Map from chain-row to ValueTree child index by counting non-default plugins.
        int targetStateIndex = stateIndex;
        if (delta < 0)
        {
            for (int i = stateIndex - 1; i >= 0; --i)
            {
                if (auto v = list.state.getChild (i); v.hasType (te::IDs::PLUGIN))
                {
                    if (auto candidate = edit.getPluginCache().getPluginFor (v))
                        if (! isDefaultTrackPlugin (*candidate))
                        {
                            targetStateIndex = i;
                            break;
                        }
                }
            }
        }
        else
        {
            for (int i = stateIndex + 1; i < list.state.getNumChildren(); ++i)
            {
                if (auto v = list.state.getChild (i); v.hasType (te::IDs::PLUGIN))
                {
                    if (auto candidate = edit.getPluginCache().getPluginFor (v))
                        if (! isDefaultTrackPlugin (*candidate))
                        {
                            targetStateIndex = i;
                            break;
                        }
                }
            }
        }

        if (targetStateIndex != stateIndex)
            list.state.moveChild (stateIndex, targetStateIndex, &edit.getUndoManager());
    });

    chainList.updateContent();
    chainList.selectRow (juce::jlimit (0, chainModel->getNumRows() - 1, row + delta));
    chainList.repaint();
}

void PluginBrowserComponent::toggleSelectedChainPluginBypass()
{
    const int row = chainList.getSelectedRow();
    if (row < 0)
        return;

    editSession.performEdit ("Toggle Bypass", [&] (te::Edit&)
    {
        auto& list = getSelectedPluginList();
        auto plugins = getChainPlugins (list);
        if (! juce::isPositiveAndBelow (row, plugins.size()))
            return;

        if (auto p = plugins[row])
            p->setEnabled (! p->isEnabled());
    });

    chainList.repaintRow (row);
}

void PluginBrowserComponent::openSelectedChainPluginEditor()
{
    const int row = chainList.getSelectedRow();
    if (row < 0)
        return;

    auto& list = getSelectedPluginList();
    auto plugins = getChainPlugins (list);
    if (! juce::isPositiveAndBelow (row, plugins.size()))
        return;

    if (auto p = plugins[row])
        p->showWindowExplicitly();
}

void PluginBrowserComponent::closeSelectedChainPluginEditor()
{
    const int row = chainList.getSelectedRow();
    if (row < 0)
        return;

    auto& list = getSelectedPluginList();
    auto plugins = getChainPlugins (list);
    if (! juce::isPositiveAndBelow (row, plugins.size()))
        return;

    if (auto p = plugins[row])
        p->hideWindowForShutdown();
}

void PluginBrowserComponent::applyInputSelection()
{
    if (trackCombo.getSelectedId() == masterItemId)
        return;

    auto* t = getSelectedTrack();
    if (t == nullptr)
        return;

    auto deviceName = inputCombo.getText();
    if (deviceName.isEmpty() || deviceName == "None")
    {
        editSession.performEdit ("Clear Input", [&] (te::Edit& edit)
        {
            for (auto* idi : edit.getEditInputDevices().getDevicesForTargetTrack (*t))
                if (idi != nullptr && idi->getInputDevice().getDeviceType() == te::InputDevice::DeviceType::waveDevice)
                    [[ maybe_unused ]] auto res = idi->removeTarget (t->itemID, &edit.getUndoManager());
        });

        updateControlsFromSelection();
        return;
    }

    if (auto* d = findWaveInputDeviceByName (editSession.getEngine(), deviceName))
    {
        editSession.performEdit ("Set Input", [&] (te::Edit& edit)
        {
            // Remove any existing wave device assignments to this track.
            for (auto* idi : edit.getEditInputDevices().getDevicesForTargetTrack (*t))
                if (idi != nullptr && idi->getInputDevice().getDeviceType() == te::InputDevice::DeviceType::waveDevice)
                    [[ maybe_unused ]] auto res = idi->removeTarget (t->itemID, &edit.getUndoManager());

            edit.getTransport().ensureContextAllocated();
            (void) edit.getEditInputDevices().getInstanceStateForInputDevice (*d);

            if (auto* epc = edit.getCurrentPlaybackContext())
            {
                if (auto* idi = epc->getInputFor (d))
                {
                    [[ maybe_unused ]] auto res = idi->setTarget (t->itemID, false, &edit.getUndoManager());
                    idi->setRecordingEnabled (t->itemID, armButton.getToggleState());
                }
            }
        });
    }

    updateControlsFromSelection();
}

void PluginBrowserComponent::setArmEnabled (bool armed)
{
    auto* t = getSelectedTrack();
    if (t == nullptr)
        return;

    editSession.performEdit (armed ? "Arm Track" : "Disarm Track", [&] (te::Edit& edit)
    {
        if (auto* idi = findWaveInputInstanceOnTrack (edit, *t))
            idi->setRecordingEnabled (t->itemID, armed);
    });
}

void PluginBrowserComponent::setMonitorEnabled (bool monitorOn)
{
    auto* t = getSelectedTrack();
    if (t == nullptr)
        return;

    editSession.performEdit (monitorOn ? "Monitor On" : "Monitor Auto", [&] (te::Edit& edit)
    {
        if (auto* idi = findWaveInputInstanceOnTrack (edit, *t))
            idi->getInputDevice().setMonitorMode (monitorOn ? te::InputDevice::MonitorMode::on
                                                           : te::InputDevice::MonitorMode::automatic);
    });
}

void PluginBrowserComponent::ensureAuxSendOnSelectedTrack()
{
    auto* t = getSelectedTrack();
    if (t == nullptr)
        return;

    if (findAuxSend (*t, 0) != nullptr)
        return;

    editSession.performEdit ("Add Aux Send", [&] (te::Edit& edit)
    {
        if (findAuxSend (*t, 0) != nullptr)
            return;

        auto v = te::AuxSendPlugin::create();
        v.setProperty (te::IDs::busNum, 0, &edit.getUndoManager());
        auto plugin = edit.getPluginCache().createNewPlugin (v);
        if (plugin == nullptr)
            return;

        t->pluginList.insertPlugin (plugin, 0, nullptr);
    });
}

void PluginBrowserComponent::updateAuxSendGainFromSlider()
{
    auto* t = getSelectedTrack();
    if (t == nullptr)
        return;

    ensureAuxSendOnSelectedTrack();

    const float db = (float) sendSlider.getValue();
    editSession.performEdit ("Set Send Level", true, [&] (te::Edit&)
    {
        if (auto* send = findAuxSend (*t, 0))
            send->setGainDb (db);
    });
}

bool PluginBrowserComponent::keyPressed (const juce::KeyPress& key)
{
    // Arrow key navigation in plugin chain list
    if (chainList.hasKeyboardFocus (true))
    {
        if (key.isKeyCode (juce::KeyPress::upKey))
        {
            int row = chainList.getSelectedRow();
            if (row > 0)
            {
                chainList.selectRow (row - 1);
                return true;
            }
        }
        else if (key.isKeyCode (juce::KeyPress::downKey))
        {
            int row = chainList.getSelectedRow();
            if (row < chainModel->getNumRows() - 1)
            {
                chainList.selectRow (row + 1);
                return true;
            }
        }
    }

    return false;
}

void PluginBrowserComponent::ensureReverbReturnOnMaster()
{
    editSession.performEdit ("Add Reverb Return", [&] (te::Edit& edit)
    {
        auto& master = edit.getMasterPluginList();

        int returnIndex = -1;
        if (auto* existingReturn = findAuxReturn (edit, 0))
        {
            returnIndex = master.state.indexOf (existingReturn->state);
        }
        else
        {
            auto v = te::createValueTree (te::IDs::PLUGIN,
                                          te::IDs::type, te::AuxReturnPlugin::xmlTypeName,
                                          te::IDs::busNum, 0);
            if (auto newReturn = edit.getPluginCache().createNewPlugin (v))
            {
                master.insertPlugin (newReturn, 0, nullptr);
                returnIndex = master.state.indexOf (newReturn->state);
            }

        }

        // Ensure there's a reverb plugin after the return.
        bool hasReverb = false;
        for (auto* p : master)
            if (dynamic_cast<te::ReverbPlugin*> (p) != nullptr)
                hasReverb = true;

        if (! hasReverb)
        {
            auto v = te::createValueTree (te::IDs::PLUGIN,
                                          te::IDs::type, te::ReverbPlugin::xmlTypeName);
            if (auto rev = edit.getPluginCache().createNewPlugin (v))
                master.insertPlugin (rev, returnIndex >= 0 ? returnIndex + 1 : 0, nullptr);
        }
    });

    chainList.updateContent();
    chainList.repaint();
}
