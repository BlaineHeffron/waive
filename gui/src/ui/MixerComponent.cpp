#include "MixerComponent.h"
#include "MixerChannelStrip.h"
#include "EditSession.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"

//==============================================================================
MixerComponent::MixerComponent (EditSession& session)
    : editSession (session)
{
    editSession.addListener (this);

    stripViewport.setViewedComponent (&stripContainer, false);
    stripViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (stripViewport);

    setWantsKeyboardFocus (true);

    rebuildStrips();
    startTimerHz (30);
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
    int masterWidth = MixerChannelStrip::stripWidth + waive::Spacing::xs;
    auto masterBounds = bounds.removeFromRight (masterWidth);
    if (masterStrip)
        masterStrip->setBounds (masterBounds.reduced (waive::Spacing::xxs));

    // Track strips in viewport
    stripViewport.setBounds (bounds);

    constexpr int stripGap = 4; // Visual separation between strips
    int totalWidth = (int) strips.size() * (MixerChannelStrip::stripWidth + stripGap);
    stripContainer.setSize (juce::jmax (totalWidth, bounds.getWidth()),
                             bounds.getHeight());

    int x = 0;
    for (auto& strip : strips)
    {
        strip->setBounds (x, 0, MixerChannelStrip::stripWidth, bounds.getHeight());
        x += MixerChannelStrip::stripWidth + stripGap;
    }
}

void MixerComponent::paint (juce::Graphics& g)
{
    auto* pal = waive::getWaivePalette (*this);

    g.fillAll (pal ? pal->windowBg : juce::Colour (0xff121212));

    if (strips.empty())
    {
        g.setFont (waive::Fonts::body());
        g.setColour (pal ? pal->textMuted : juce::Colour (0xff808080));
        g.drawText ("Add tracks to see the mixer", getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    // Separator before master
    auto bounds = getLocalBounds();
    int masterX = bounds.getRight() - MixerChannelStrip::stripWidth - 4;
    g.setColour (pal ? pal->border.brighter (0.15f) : juce::Colour (0xff3a3a3a));
    g.drawVerticalLine (masterX, 0.0f, (float) getHeight());
}

void MixerComponent::setHighlightedTrackIndices (const juce::Array<int>& trackIndices)
{
    highlightedTrackIndices = trackIndices;
    applyTrackHighlights();
}

void MixerComponent::clearHighlightedTrackIndices()
{
    highlightedTrackIndices.clear();
    applyTrackHighlights();
}

juce::Array<int> MixerComponent::getHighlightedTrackIndicesForTesting() const
{
    return highlightedTrackIndices;
}

void MixerComponent::timerCallback()
{
    auto tracks = te::getAudioTracks (editSession.getEdit());
    if (tracks.size() != lastTrackCount)
        rebuildStrips();

    // Poll only visible strips
    auto visibleArea = stripViewport.getViewArea();
    for (auto& strip : strips)
        if (strip != nullptr && strip->getBounds().intersects (visibleArea))
            strip->pollState();

    if (masterStrip != nullptr)
        masterStrip->pollState();
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
    applyTrackHighlights();
    resized();
}

void MixerComponent::applyTrackHighlights()
{
    for (int i = 0; i < (int) strips.size(); ++i)
    {
        auto* strip = strips[(size_t) i].get();
        if (strip != nullptr)
            strip->setHighlighted (highlightedTrackIndices.contains (i));
    }
}

bool MixerComponent::keyPressed (const juce::KeyPress& key)
{
    // Arrow key navigation between channel strips
    if (key.isKeyCode (juce::KeyPress::leftKey))
    {
        if (focusedStripIndex > 0)
        {
            focusedStripIndex--;
            if (focusedStripIndex < (int) strips.size() && strips[(size_t) focusedStripIndex] != nullptr)
                strips[(size_t) focusedStripIndex]->grabKeyboardFocus();
            return true;
        }
    }
    else if (key.isKeyCode (juce::KeyPress::rightKey))
    {
        if (focusedStripIndex < (int) strips.size() - 1)
        {
            focusedStripIndex++;
            if (strips[(size_t) focusedStripIndex] != nullptr)
                strips[(size_t) focusedStripIndex]->grabKeyboardFocus();
            return true;
        }
        else if (focusedStripIndex == (int) strips.size() - 1 && masterStrip != nullptr)
        {
            focusedStripIndex = (int) strips.size();
            masterStrip->grabKeyboardFocus();
            return true;
        }
    }

    return false;
}
