#include "MixerChannelStrip.h"
#include "EditSession.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"

//==============================================================================
MixerChannelStrip::MixerChannelStrip (te::AudioTrack& t, EditSession& session)
    : editSession (session), stripType (StripType::Track), track (&t), isMaster (false)
{
    nameLabel.setText (track->getName(), juce::dontSendNotification);
    setupControls();

    // Register meter client via LevelMeterPlugin
    if (auto* meter = track->getLevelMeterPlugin())
        meter->measurer.addClient (meterClient);
}

MixerChannelStrip::MixerChannelStrip (te::FolderTrack& folder, EditSession& session)
    : editSession (session), stripType (StripType::Folder), folderTrack (&folder), isMaster (false)
{
    nameLabel.setText (folderTrack->getName(), juce::dontSendNotification);
    setupControls();

    // Folder tracks use submixing - meter plugin available
    auto meterPlugins = folderTrack->pluginList.getPluginsOfType<te::LevelMeterPlugin>();
    if (! meterPlugins.isEmpty())
        meterPlugins.getFirst()->measurer.addClient (meterClient);
}

MixerChannelStrip::MixerChannelStrip (te::Edit& edit, EditSession& session)
    : editSession (session), stripType (StripType::Master), masterEdit (&edit), isMaster (true)
{
    nameLabel.setText ("Master", juce::dontSendNotification);
    setupControls();

    // Master metering via master plugin list's LevelMeterPlugin
    auto meterPlugins = edit.getMasterPluginList().getPluginsOfType<te::LevelMeterPlugin>();
    if (! meterPlugins.isEmpty())
        meterPlugins.getFirst()->measurer.addClient (meterClient);
}

MixerChannelStrip::~MixerChannelStrip()
{
    // Unregister meter client - check validity to avoid dangling pointer access
    if (track != nullptr)
    {
        if (auto* meter = track->getLevelMeterPlugin())
        {
            meter->measurer.removeClient (meterClient);
        }
    }
    else if (folderTrack != nullptr)
    {
        auto meterPlugins = folderTrack->pluginList.getPluginsOfType<te::LevelMeterPlugin>();
        if (! meterPlugins.isEmpty() && meterPlugins.getFirst() != nullptr)
        {
            meterPlugins.getFirst()->measurer.removeClient (meterClient);
        }
    }
    else if (masterEdit != nullptr)
    {
        auto meterPlugins = masterEdit->getMasterPluginList().getPluginsOfType<te::LevelMeterPlugin>();
        if (! meterPlugins.isEmpty() && meterPlugins.getFirst() != nullptr)
        {
            meterPlugins.getFirst()->measurer.removeClient (meterClient);
        }
    }
}

void MixerChannelStrip::setupControls()
{
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (waive::Fonts::caption());
    nameLabel.setEditable (false, true, false);
    nameLabel.onTextChange = [this]
    {
        if (suppressControlCallbacks)
            return;

        editSession.performEdit ("Rename Track", [this] (te::Edit&)
        {
            if (track != nullptr)
                track->setName (nameLabel.getText());
            else if (folderTrack != nullptr)
                folderTrack->setName (nameLabel.getText());
        });
    };
    nameLabel.setTitle ("Track Name");
    nameLabel.setDescription ("Channel strip name");
    addAndMakeVisible (nameLabel);

    faderSlider.setSliderStyle (juce::Slider::LinearVertical);
    faderSlider.setRange (-60.0, 6.0, 0.1);
    faderSlider.setValue (0.0, juce::dontSendNotification);
    faderSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    faderSlider.setTextValueSuffix (" dB");
    faderSlider.setTooltip ("Track Volume (dB)");
    faderSlider.setTitle ("Volume");
    faderSlider.setDescription ("Track volume in dB");
    faderSlider.setWantsKeyboardFocus (true);
    addAndMakeVisible (faderSlider);

    faderSlider.onValueChange = [this]
    {
        if (suppressControlCallbacks)
            return;

        float db = (float) faderSlider.getValue();
        editSession.performEdit ("Set Volume", true, [this, db] (te::Edit&)
        {
            te::VolumeAndPanPlugin* vp = nullptr;
            if (track != nullptr)
            {
                auto plugins = track->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
                if (! plugins.isEmpty())
                    vp = plugins.getFirst();
            }
            else if (folderTrack != nullptr)
            {
                auto plugins = folderTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
                if (! plugins.isEmpty())
                    vp = plugins.getFirst();
            }
            else if (masterEdit != nullptr)
            {
                vp = masterEdit->getMasterVolumePlugin().get();
            }

            if (vp != nullptr)
                vp->volParam->setParameter (te::decibelsToVolumeFaderPosition (db),
                                            juce::sendNotification);
        });
    };
    faderSlider.onDragEnd = [this] { editSession.endCoalescedTransaction(); };

    if (! isMaster)
    {
        soloButton.setButtonText ("S");
        soloButton.setTooltip ("Solo (S)");
        soloButton.setTitle ("Solo");
        soloButton.setDescription ("Solo this track (S)");
        soloButton.setWantsKeyboardFocus (true);
        soloButton.onClick = [this]
        {
            if (suppressControlCallbacks)
                return;

            bool newState = soloButton.getToggleState();
            editSession.performEdit ("Toggle Solo", [this, newState] (te::Edit&)
            {
                if (track != nullptr)
                {
                    track->setSolo (newState);
                }
                else if (folderTrack != nullptr)
                {
                    folderTrack->setSolo (newState);
                    // Apply to all children
                    for (auto* child : folderTrack->getAllSubTracks (false))
                        child->setSolo (newState);
                }
            });
        };
        addAndMakeVisible (soloButton);

        muteButton.setButtonText ("M");
        muteButton.setTooltip ("Mute (M)");
        muteButton.setTitle ("Mute");
        muteButton.setDescription ("Mute this track (M)");
        muteButton.setWantsKeyboardFocus (true);
        muteButton.onClick = [this]
        {
            if (suppressControlCallbacks)
                return;

            bool newState = muteButton.getToggleState();
            editSession.performEdit ("Toggle Mute", [this, newState] (te::Edit&)
            {
                if (track != nullptr)
                {
                    track->setMute (newState);
                }
                else if (folderTrack != nullptr)
                {
                    folderTrack->setMute (newState);
                    // Apply to all children
                    for (auto* child : folderTrack->getAllSubTracks (false))
                        child->setMute (newState);
                }
            });
        };
        addAndMakeVisible (muteButton);

        // Arm button - only for audio tracks, not folders
        if (stripType == StripType::Track)
        {
            armButton.setButtonText ("R");
            armButton.setTooltip ("Arm for Recording (R)");
            armButton.setTitle ("Arm");
            armButton.setDescription ("Arm this track for recording");
            armButton.setWantsKeyboardFocus (true);
            if (auto* pal = waive::getWaivePalette (*this))
                armButton.setColour (juce::ToggleButton::tickColourId, pal->danger);
            armButton.onClick = [this]
            {
                if (suppressControlCallbacks || track == nullptr)
                    return;

                bool newState = armButton.getToggleState();
                editSession.performEdit ("Toggle Arm", [this, newState] (te::Edit& edit)
                {
                    auto& eid = edit.getEditInputDevices();
                    for (auto* idi : eid.getDevicesForTargetTrack (*track))
                    {
                        if (idi != nullptr)
                            idi->setRecordingEnabled (track->itemID, newState);
                    }
                });
            };
            addAndMakeVisible (armButton);

            // Input combo - only for audio tracks
            inputCombo.setTooltip ("Input Device");
            inputCombo.setTitle ("Input Device");
            inputCombo.setDescription ("Select input device for recording");
            inputCombo.setWantsKeyboardFocus (true);
            inputCombo.addItem ("(No Input)", 1);
            auto& dm = editSession.getEdit().engine.getDeviceManager();
            auto waveInputs = dm.getWaveInputDevices();
            for (size_t i = 0; i < waveInputs.size(); ++i)
                inputCombo.addItem (waveInputs[i]->getName(), (int)i + 2);
            inputCombo.setSelectedId (1, juce::dontSendNotification);
            inputCombo.onChange = [this]
            {
                if (suppressControlCallbacks || track == nullptr)
                    return;

            int selectedId = inputCombo.getSelectedId();
            editSession.performEdit ("Set Input Device", [this, selectedId] (te::Edit& edit)
            {
                auto& deviceMgr = edit.engine.getDeviceManager();
                auto inputs = deviceMgr.getWaveInputDevices();

                if (selectedId <= 1 || (size_t)(selectedId - 2) >= inputs.size())
                {
                    // Clear input assignment
                    auto& eid = edit.getEditInputDevices();
                    for (auto* idi : eid.getDevicesForTargetTrack (*track))
                    {
                        if (idi != nullptr && !idi->setTarget ({}, false, &edit.getUndoManager()))
                            DBG ("Failed to clear input assignment for track");
                    }
                }
                else
                {
                    // Assign wave input
                    auto* waveIn = inputs[(size_t)(selectedId - 2)];
                    edit.getTransport().ensureContextAllocated();
                    (void) edit.getEditInputDevices().getInstanceStateForInputDevice (*waveIn);

                    if (auto* epc = edit.getCurrentPlaybackContext())
                    {
                        if (auto* idi = epc->getInputFor (waveIn))
                        {
                            if (!idi->setTarget (track->itemID, false, &edit.getUndoManager()))
                                DBG ("Failed to assign input device to track");
                        }
                    }
                }
            });
        };
            addAndMakeVisible (inputCombo);
        }

        panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        panKnob.setRange (-1.0, 1.0, 0.01);
        panKnob.setValue (0.0, juce::dontSendNotification);
        panKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        panKnob.setTooltip ("Pan L/R");
        panKnob.setTitle ("Pan");
        panKnob.setDescription ("Pan left/right");
        panKnob.setWantsKeyboardFocus (true);
        addAndMakeVisible (panKnob);

        panKnob.onValueChange = [this]
        {
            if (suppressControlCallbacks)
                return;

            float pan = (float) panKnob.getValue();
            editSession.performEdit ("Set Pan", true, [this, pan] (te::Edit&)
            {
                te::VolumeAndPanPlugin* vp = nullptr;
                if (track != nullptr)
                {
                    auto plugins = track->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
                    if (! plugins.isEmpty())
                        vp = plugins.getFirst();
                }
                else if (folderTrack != nullptr)
                {
                    auto plugins = folderTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
                    if (! plugins.isEmpty())
                        vp = plugins.getFirst();
                }

                if (vp != nullptr)
                    vp->panParam->setParameter ((pan + 1.0f) / 2.0f, juce::sendNotification);
            });
        };
        panKnob.onDragEnd = [this] { editSession.endCoalescedTransaction(); };
    }
}

void MixerChannelStrip::resized()
{
    auto bounds = getLocalBounds().reduced (waive::Spacing::xxs);

    nameLabel.setBounds (bounds.removeFromTop (waive::Spacing::labelHeight));
    bounds.removeFromTop (waive::Spacing::xxs);

    if (! isMaster)
    {
        auto buttonRow = bounds.removeFromTop (waive::Spacing::controlHeightSmall);
        soloButton.setBounds (buttonRow.removeFromLeft (waive::Spacing::toolbarRowHeight));
        buttonRow.removeFromLeft (waive::Spacing::xxs);
        muteButton.setBounds (buttonRow.removeFromLeft (waive::Spacing::toolbarRowHeight));
        bounds.removeFromTop (waive::Spacing::xxs);

        // Arm + input row (only for audio tracks, not folders)
        if (stripType == StripType::Track)
        {
            auto armRow = bounds.removeFromTop (waive::Spacing::controlHeightMedium);
            armButton.setBounds (armRow.removeFromLeft (waive::Spacing::controlHeightMedium));
            armRow.removeFromLeft (waive::Spacing::xxs);
            inputCombo.setBounds (armRow);
            bounds.removeFromTop (waive::Spacing::xxs);
        }

        panKnob.setBounds (bounds.removeFromTop (waive::Spacing::toolbarRowHeight).withSizeKeepingCentre (36, 36));
        bounds.removeFromTop (waive::Spacing::xxs);
    }

    faderSlider.setBounds (bounds.getX(), bounds.getY(),
                            bounds.getWidth(), bounds.getHeight());
}

void MixerChannelStrip::setHighlighted (bool shouldHighlight)
{
    if (highlighted == shouldHighlight)
        return;

    highlighted = shouldHighlight;
    repaint();
}

void MixerChannelStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto* pal = waive::getWaivePalette (*this);

    // Background (master strip and folder strips use distinct backgrounds)
    juce::Colour bgColour = pal ? pal->surfaceBg : juce::Colour (0xff2a2a2a);
    if (isMaster && pal)
        bgColour = pal->surfaceBgAlt;
    else if (stripType == StripType::Folder && pal)
        bgColour = pal->folderTrackBg;

    g.setColour (bgColour);
    g.fillRoundedRectangle (bounds, 3.0f);

    // Border
    g.setColour (pal ? pal->border : juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    if (highlighted)
    {
        g.setColour (pal ? pal->accent : juce::Colour (0xff4477aa));
        g.drawRoundedRectangle (bounds.reduced (1.5f), 3.0f, 2.0f);
    }

    // Professional meter bars (beside the fader)
    auto meterBounds = getLocalBounds().reduced (2);
    // Skip name + controls based on strip type
    constexpr int masterHeaderHeight = waive::Spacing::labelHeight + waive::Spacing::xxs + waive::Spacing::toolbarRowHeight;  // 58
    constexpr int folderHeaderHeight = waive::Spacing::labelHeight + waive::Spacing::xxs
                                     + waive::Spacing::controlHeightSmall + waive::Spacing::xxs
                                     + waive::Spacing::toolbarRowHeight + waive::Spacing::xxs;  // folder: no arm/input
    constexpr int trackHeaderHeight = waive::Spacing::labelHeight + waive::Spacing::xxs
                                    + waive::Spacing::controlHeightSmall + waive::Spacing::xxs
                                    + waive::Spacing::controlHeightMedium + waive::Spacing::xxs
                                    + waive::Spacing::toolbarRowHeight + waive::Spacing::xxs;  // 104
    int headerHeight = isMaster ? masterHeaderHeight : (stripType == StripType::Folder ? folderHeaderHeight : trackHeaderHeight);
    meterBounds.removeFromTop (headerHeight);
    constexpr int meterWidth = 20;  // component-specific: 6px bars + scale + padding
    meterBounds = meterBounds.removeFromRight (meterWidth);
    lastMeterBounds = meterBounds;

    int meterHeight = meterBounds.getHeight() - 20;
    if (meterHeight > 0)
    {
        // dB scale markings
        const float dbMarks[] = {-60.0f, -40.0f, -20.0f, -10.0f, -6.0f, -3.0f, 0.0f, 6.0f};
        g.setColour (pal ? pal->textMuted : juce::Colour (0xff999999));
        g.setFont (waive::Fonts::meter());

        for (float db : dbMarks)
        {
            float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
            int y = meterBounds.getY() + (int) (meterHeight * (1.0f - norm));
            g.drawLine (meterBounds.getX() + 13, (float) y, meterBounds.getX() + 16, (float) y, 1.0f);
        }

        // Draw meter bars with gradient
        auto drawMeterBar = [&] (float peakDB, float peakHoldDB, int xOffset)
        {
            float norm = juce::jlimit (0.0f, 1.0f, (peakDB + 60.0f) / 66.0f);
            int barH = (int) (meterHeight * norm);
            int barY = meterBounds.getY() + meterHeight - barH;

            // Use gradient fill instead of pixel-by-pixel
            auto meterRect = juce::Rectangle<int> (meterBounds.getX() + xOffset, barY, 6, barH);

            // Cache gradient - rebuild only when meterBounds changes
            if (meterBounds != cachedMeterGradientBounds)
            {
                cachedMeterGradientBounds = meterBounds;
                meterGradient = juce::ColourGradient (
                    pal ? pal->meterClip : juce::Colour (0xffff0000),
                    (float) meterBounds.getX(), (float) meterBounds.getY(),
                    pal ? pal->meterNormal : juce::Colour (0xff00ff00),
                    (float) meterBounds.getX(), (float) meterBounds.getBottom(),
                    false);

                // Add intermediate color stops
                meterGradient.addColour (0.05, pal ? pal->meterClip : juce::Colour (0xffff0000));        // -3dB
                meterGradient.addColour (0.45, pal ? pal->meterWarning : juce::Colour (0xffffff00));  // -12dB
            }

            g.setGradientFill (meterGradient);
            g.fillRect (meterRect);

            // Peak hold line
            float peakHoldNorm = juce::jlimit (0.0f, 1.0f, (peakHoldDB + 60.0f) / 66.0f);
            int peakHoldY = meterBounds.getY() + (int) (meterHeight * (1.0f - peakHoldNorm));
            g.setColour (pal ? pal->textOnPrimary : juce::Colour (0xffffffff));
            g.fillRect (meterBounds.getX() + xOffset, peakHoldY, 6, 2);
        };

        drawMeterBar (peakL, peakHoldL, 0);
        drawMeterBar (peakR, peakHoldR, 7);
    }
}

void MixerChannelStrip::pollState()
{
    // Pull current engine state into controls without generating new undo transactions.
    suppressControlCallbacks = true;
    if (track != nullptr)
    {
        nameLabel.setText (track->getName(), juce::dontSendNotification);

        if (auto* vp = track->getVolumePlugin())
        {
            faderSlider.setValue (te::volumeFaderPositionToDB (vp->volParam->getCurrentValue()),
                                  juce::dontSendNotification);
            panKnob.setValue (vp->panParam->getCurrentValue() * 2.0f - 1.0f,
                              juce::dontSendNotification);
        }

        soloButton.setToggleState (track->isSolo (false), juce::dontSendNotification);
        muteButton.setToggleState (track->isMuted (false), juce::dontSendNotification);

        // Update arm button and input combo state (audio tracks only)
        if (stripType == StripType::Track)
        {
            auto& eid = editSession.getEdit().getEditInputDevices();
            bool isArmed = false;
            te::InputDevice* assignedDevice = nullptr;

            for (auto* idi : eid.getDevicesForTargetTrack (*track))
            {
                if (idi->isRecordingEnabled (track->itemID))
                    isArmed = true;
                assignedDevice = &idi->getInputDevice();
            }

            armButton.setToggleState (isArmed, juce::dontSendNotification);

            // Update input combo selection
            if (assignedDevice != nullptr)
            {
                auto& deviceMgr = editSession.getEdit().engine.getDeviceManager();
                auto inputs = deviceMgr.getWaveInputDevices();
                for (size_t i = 0; i < inputs.size(); ++i)
                {
                    if (inputs[i] == assignedDevice)
                    {
                        inputCombo.setSelectedId ((int)i + 2, juce::dontSendNotification);
                        break;
                    }
                }
            }
            else
            {
                inputCombo.setSelectedId (1, juce::dontSendNotification);
            }
        }
    }
    else if (folderTrack != nullptr)
    {
        nameLabel.setText (folderTrack->getName(), juce::dontSendNotification);

        auto volPlugins = folderTrack->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
        if (! volPlugins.isEmpty())
        {
            auto* vp = volPlugins.getFirst();
            faderSlider.setValue (te::volumeFaderPositionToDB (vp->volParam->getCurrentValue()),
                                  juce::dontSendNotification);
            panKnob.setValue (vp->panParam->getCurrentValue() * 2.0f - 1.0f,
                              juce::dontSendNotification);
        }

        soloButton.setToggleState (folderTrack->isSolo (false), juce::dontSendNotification);
        muteButton.setToggleState (folderTrack->isMuted (false), juce::dontSendNotification);
    }
    else if (masterEdit != nullptr)
    {
        if (auto* vp = masterEdit->getMasterVolumePlugin().get())
            faderSlider.setValue (te::volumeFaderPositionToDB (vp->volParam->getCurrentValue()),
                                  juce::dontSendNotification);
    }
    suppressControlCallbacks = false;

    auto levelL = meterClient.getAndClearAudioLevel (0);
    auto levelR = meterClient.getAndClearAudioLevel (1);

    // Decay
    peakL = juce::jmax (levelL.dB, peakL - 1.5f);
    peakR = juce::jmax (levelR.dB, peakR - 1.5f);

    // Peak hold tracking
    if (peakL > peakHoldL)
    {
        peakHoldL = peakL;
        peakHoldDecayCounterL = 0;
    }
    if (peakR > peakHoldR)
    {
        peakHoldR = peakR;
        peakHoldDecayCounterR = 0;
    }

    // Peak hold decay (2 seconds = ~100 poll cycles at ~50Hz)
    if (++peakHoldDecayCounterL > 100)
    {
        peakHoldL = juce::jmax (peakHoldL - 1.0f, -60.0f);
        peakHoldDecayCounterL = 100;  // keep decaying
    }
    if (++peakHoldDecayCounterR > 100)
    {
        peakHoldR = juce::jmax (peakHoldR - 1.0f, -60.0f);
        peakHoldDecayCounterR = 100;  // keep decaying
    }

    // Repaint only meter region if levels changed meaningfully (>0.5 dB)
    constexpr float threshold = 0.5f;
    bool metersChanged = std::abs (peakL - lastPeakL) > threshold ||
                         std::abs (peakR - lastPeakR) > threshold;

    if (metersChanged && ! lastMeterBounds.isEmpty())
    {
        lastPeakL = peakL;
        lastPeakR = peakR;
        repaint (lastMeterBounds);
    }
}
