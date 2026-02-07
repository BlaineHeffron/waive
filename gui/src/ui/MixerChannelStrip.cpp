#include "MixerChannelStrip.h"
#include "EditSession.h"
#include "WaiveLookAndFeel.h"

//==============================================================================
MixerChannelStrip::MixerChannelStrip (te::AudioTrack& t, EditSession& session)
    : editSession (session), track (&t), isMaster (false)
{
    nameLabel.setText (track->getName(), juce::dontSendNotification);
    setupControls();

    // Register meter client via LevelMeterPlugin
    if (auto* meter = track->getLevelMeterPlugin())
        meter->measurer.addClient (meterClient);
}

MixerChannelStrip::MixerChannelStrip (te::Edit& edit, EditSession& session)
    : editSession (session), masterEdit (&edit), isMaster (true)
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
    // Unregister meter client
    if (track != nullptr)
    {
        if (auto* meter = track->getLevelMeterPlugin())
            meter->measurer.removeClient (meterClient);
    }
    else if (masterEdit != nullptr)
    {
        auto meterPlugins = masterEdit->getMasterPluginList().getPluginsOfType<te::LevelMeterPlugin>();
        if (! meterPlugins.isEmpty())
            meterPlugins.getFirst()->measurer.removeClient (meterClient);
    }
}

void MixerChannelStrip::setupControls()
{
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (juce::FontOptions (11.0f));
    nameLabel.setEditable (false, true, false);
    nameLabel.onTextChange = [this]
    {
        if (suppressControlCallbacks || track == nullptr)
            return;

        editSession.performEdit ("Rename Track", [this] (te::Edit&)
        {
            track->setName (nameLabel.getText());
        });
    };
    addAndMakeVisible (nameLabel);

    faderSlider.setSliderStyle (juce::Slider::LinearVertical);
    faderSlider.setRange (-60.0, 6.0, 0.1);
    faderSlider.setValue (0.0, juce::dontSendNotification);
    faderSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    faderSlider.setTextValueSuffix (" dB");
    faderSlider.setTooltip ("Track Volume (dB)");
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
        soloButton.setWantsKeyboardFocus (true);
        soloButton.onClick = [this]
        {
            if (suppressControlCallbacks || track == nullptr)
                return;

            bool newState = soloButton.getToggleState();
            editSession.performEdit ("Toggle Solo", [this, newState] (te::Edit&)
            {
                track->setSolo (newState);
            });
        };
        addAndMakeVisible (soloButton);

        muteButton.setButtonText ("M");
        muteButton.setTooltip ("Mute (M)");
        muteButton.setWantsKeyboardFocus (true);
        muteButton.onClick = [this]
        {
            if (suppressControlCallbacks || track == nullptr)
                return;

            bool newState = muteButton.getToggleState();
            editSession.performEdit ("Toggle Mute", [this, newState] (te::Edit&)
            {
                track->setMute (newState);
            });
        };
        addAndMakeVisible (muteButton);
        panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        panKnob.setRange (-1.0, 1.0, 0.01);
        panKnob.setValue (0.0, juce::dontSendNotification);
        panKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        panKnob.setTooltip ("Pan L/R");
        addAndMakeVisible (panKnob);

        panKnob.onValueChange = [this]
        {
            if (suppressControlCallbacks)
                return;

            float pan = (float) panKnob.getValue();
            editSession.performEdit ("Set Pan", true, [this, pan] (te::Edit&)
            {
                if (track != nullptr)
                {
                    auto plugins = track->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>();
                    if (! plugins.isEmpty())
                        plugins.getFirst()->panParam->setParameter ((pan + 1.0f) / 2.0f,
                                                                    juce::sendNotification);
                }
            });
        };
        panKnob.onDragEnd = [this] { editSession.endCoalescedTransaction(); };
    }
}

void MixerChannelStrip::resized()
{
    auto bounds = getLocalBounds().reduced (2);

    nameLabel.setBounds (bounds.removeFromTop (18));
    bounds.removeFromTop (2);

    if (! isMaster)
    {
        auto buttonRow = bounds.removeFromTop (20);
        soloButton.setBounds (buttonRow.removeFromLeft (36));
        buttonRow.removeFromLeft (2);
        muteButton.setBounds (buttonRow.removeFromLeft (36));
        bounds.removeFromTop (2);

        panKnob.setBounds (bounds.removeFromTop (36).withSizeKeepingCentre (36, 36));
        bounds.removeFromTop (2);
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

    // Background
    g.setColour (pal ? pal->surfaceBg : juce::Colour (0xff2a2a2a));
    g.fillRoundedRectangle (bounds, 3.0f);

    // Border
    g.setColour (pal ? pal->border : juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    if (highlighted)
    {
        g.setColour (pal ? pal->accent : juce::Colour (0xfff0b429));
        g.drawRoundedRectangle (bounds.reduced (1.5f), 3.0f, 2.0f);
    }

    // Meter bars (beside the fader)
    auto meterBounds = getLocalBounds().reduced (2);
    meterBounds.removeFromTop (58);  // skip name + pan
    meterBounds = meterBounds.removeFromRight (8);
    lastMeterBounds = meterBounds;

    int meterHeight = meterBounds.getHeight() - 20;
    if (meterHeight > 0)
    {
        float normL = juce::jlimit (0.0f, 1.0f, (peakL + 60.0f) / 66.0f);
        float normR = juce::jlimit (0.0f, 1.0f, (peakR + 60.0f) / 66.0f);

        int barW = 3;
        int barH = (int) (meterHeight * normL);
        g.setColour (normL > 0.9f ? (pal ? pal->meterClip : juce::Colours::red)
                                   : (pal ? pal->meterNormal : juce::Colours::limegreen));
        g.fillRect (meterBounds.getX(), meterBounds.getY() + meterHeight - barH, barW, barH);

        barH = (int) (meterHeight * normR);
        g.setColour (normR > 0.9f ? (pal ? pal->meterClip : juce::Colours::red)
                                   : (pal ? pal->meterNormal : juce::Colours::limegreen));
        g.fillRect (meterBounds.getX() + barW + 1, meterBounds.getY() + meterHeight - barH, barW, barH);
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
