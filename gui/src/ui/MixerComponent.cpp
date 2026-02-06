#include "MixerComponent.h"
#include "MixerChannelStrip.h"
#include "EditSession.h"

//==============================================================================
MixerComponent::MixerComponent (EditSession& session)
    : editSession (session)
{
    editSession.addListener (this);

    stripViewport.setViewedComponent (&stripContainer, false);
    stripViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (stripViewport);

    rebuildStrips();
    startTimerHz (5);
}

MixerComponent::~MixerComponent()
{
    stopTimer();
    editSession.removeListener (this);
}

void MixerComponent::resized()
{
    auto bounds = getLocalBounds();

    // Master strip on the right
    int masterWidth = MixerChannelStrip::stripWidth + 4;
    auto masterBounds = bounds.removeFromRight (masterWidth);
    if (masterStrip)
        masterStrip->setBounds (masterBounds.reduced (2));

    // Track strips in viewport
    stripViewport.setBounds (bounds);

    int totalWidth = (int) strips.size() * MixerChannelStrip::stripWidth;
    stripContainer.setSize (juce::jmax (totalWidth, bounds.getWidth()),
                             bounds.getHeight());

    int x = 0;
    for (auto& strip : strips)
    {
        strip->setBounds (x, 0, MixerChannelStrip::stripWidth, bounds.getHeight());
        x += MixerChannelStrip::stripWidth;
    }
}

void MixerComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));

    // Separator before master
    auto bounds = getLocalBounds();
    int masterX = bounds.getRight() - MixerChannelStrip::stripWidth - 4;
    g.setColour (juce::Colour (0xff4a4a4a));
    g.drawVerticalLine (masterX, 0.0f, (float) getHeight());
}

void MixerComponent::timerCallback()
{
    auto tracks = te::getAudioTracks (editSession.getEdit());
    if (tracks.size() != lastTrackCount)
        rebuildStrips();
}

void MixerComponent::editAboutToChange()
{
    strips.clear();
    stripContainer.removeAllChildren();
    masterStrip.reset();
    lastTrackCount = -1;
}

void MixerComponent::editChanged()
{
    rebuildStrips();
}

void MixerComponent::rebuildStrips()
{
    strips.clear();
    stripContainer.removeAllChildren();
    masterStrip.reset();

    auto& edit = editSession.getEdit();
    auto tracks = te::getAudioTracks (edit);

    for (auto* track : tracks)
    {
        auto strip = std::make_unique<MixerChannelStrip> (*track, editSession);
        stripContainer.addAndMakeVisible (strip.get());
        strips.push_back (std::move (strip));
    }

    masterStrip = std::make_unique<MixerChannelStrip> (edit, editSession);
    addAndMakeVisible (masterStrip.get());

    lastTrackCount = tracks.size();
    resized();
}
