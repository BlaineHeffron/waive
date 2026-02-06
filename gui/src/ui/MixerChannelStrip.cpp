#include "MixerChannelStrip.h"
#include "EditSession.h"

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
    stopTimer();

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
    addAndMakeVisible (nameLabel);

    faderSlider.setSliderStyle (juce::Slider::LinearVertical);
    faderSlider.setRange (-60.0, 6.0, 0.1);
    faderSlider.setValue (0.0, juce::dontSendNotification);
    faderSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    faderSlider.setTextValueSuffix (" dB");
    addAndMakeVisible (faderSlider);

    faderSlider.onValueChange = [this]
    {
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

    if (! isMaster)
    {
        panKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        panKnob.setRange (-1.0, 1.0, 0.01);
        panKnob.setValue (0.0, juce::dontSendNotification);
        panKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        addAndMakeVisible (panKnob);

        panKnob.onValueChange = [this]
        {
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
    }

    startTimerHz (30);
}

void MixerChannelStrip::resized()
{
    auto bounds = getLocalBounds().reduced (2);

    nameLabel.setBounds (bounds.removeFromTop (18));
    bounds.removeFromTop (2);

    if (! isMaster)
    {
        panKnob.setBounds (bounds.removeFromTop (36).withSizeKeepingCentre (36, 36));
        bounds.removeFromTop (2);
    }

    faderSlider.setBounds (bounds.getX(), bounds.getY(),
                            bounds.getWidth(), bounds.getHeight());
}

void MixerChannelStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Background
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRoundedRectangle (bounds, 3.0f);

    // Border
    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    // Meter bars (beside the fader)
    auto meterBounds = getLocalBounds().reduced (2);
    meterBounds.removeFromTop (58);  // skip name + pan
    meterBounds = meterBounds.removeFromRight (8);

    int meterHeight = meterBounds.getHeight() - 20;
    if (meterHeight > 0)
    {
        float normL = juce::jlimit (0.0f, 1.0f, (peakL + 60.0f) / 66.0f);
        float normR = juce::jlimit (0.0f, 1.0f, (peakR + 60.0f) / 66.0f);

        int barW = 3;
        int barH = (int) (meterHeight * normL);
        g.setColour (normL > 0.9f ? juce::Colours::red : juce::Colours::limegreen);
        g.fillRect (meterBounds.getX(), meterBounds.getY() + meterHeight - barH, barW, barH);

        barH = (int) (meterHeight * normR);
        g.setColour (normR > 0.9f ? juce::Colours::red : juce::Colours::limegreen);
        g.fillRect (meterBounds.getX() + barW + 1, meterBounds.getY() + meterHeight - barH, barW, barH);
    }
}

void MixerChannelStrip::timerCallback()
{
    auto levelL = meterClient.getAndClearAudioLevel (0);
    auto levelR = meterClient.getAndClearAudioLevel (1);

    // Decay
    peakL = juce::jmax (levelL.dB, peakL - 1.5f);
    peakR = juce::jmax (levelR.dB, peakR - 1.5f);

    repaint();
}
