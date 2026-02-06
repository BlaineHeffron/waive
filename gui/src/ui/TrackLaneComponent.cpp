#include "TrackLaneComponent.h"
#include "ClipComponent.h"
#include "TimelineComponent.h"

//==============================================================================
TrackLaneComponent::TrackLaneComponent (te::AudioTrack& t, TimelineComponent& tl)
    : track (t), timeline (tl)
{
    headerLabel.setText (track.getName(), juce::dontSendNotification);
    headerLabel.setJustificationType (juce::Justification::centredLeft);
    headerLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (headerLabel);

    updateClips();
    startTimerHz (5);
}

TrackLaneComponent::~TrackLaneComponent()
{
    stopTimer();
}

void TrackLaneComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Track header background
    auto headerBounds = bounds.removeFromLeft (TimelineComponent::trackHeaderWidth);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRect (headerBounds);

    // Lane background
    g.setColour (juce::Colour (0xff222222));
    g.fillRect (bounds);

    // Bottom separator
    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void TrackLaneComponent::resized()
{
    auto bounds = getLocalBounds();
    headerLabel.setBounds (bounds.removeFromLeft (TimelineComponent::trackHeaderWidth).reduced (4));

    for (auto& cc : clipComponents)
        cc->updatePosition();
}

void TrackLaneComponent::updateClips()
{
    clipComponents.clear();
    removeAllChildren();
    addAndMakeVisible (headerLabel);

    for (auto* clip : track.getClips())
    {
        auto cc = std::make_unique<ClipComponent> (*clip, timeline);
        addAndMakeVisible (cc.get());
        clipComponents.push_back (std::move (cc));
    }

    lastClipCount = track.getClips().size();
    resized();
}

void TrackLaneComponent::timerCallback()
{
    int currentCount = track.getClips().size();
    if (currentCount != lastClipCount)
        updateClips();
    else
    {
        for (auto& cc : clipComponents)
            cc->updatePosition();
    }
}
