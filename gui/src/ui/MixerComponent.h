#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "EditSession.h"

namespace te = tracktion;

class EditSession;
class MixerChannelStrip;

//==============================================================================
/** Container with horizontal strip viewport + master strip. */
class MixerComponent : public juce::Component,
                       private EditSession::Listener,
                       private juce::Timer
{
public:
    explicit MixerComponent (EditSession& session);
    ~MixerComponent() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void setHighlightedTrackIndices (const juce::Array<int>& trackIndices);
    void clearHighlightedTrackIndices();
    juce::Array<int> getHighlightedTrackIndicesForTesting() const;

private:
    void editAboutToChange() override;
    void editChanged() override;
    void timerCallback() override;
    void rebuildStrips();
    void applyTrackHighlights();

    EditSession& editSession;

    juce::Viewport stripViewport;
    juce::Component stripContainer;
    std::vector<std::unique_ptr<MixerChannelStrip>> strips;
    std::unique_ptr<MixerChannelStrip> masterStrip;
    juce::Array<int> highlightedTrackIndices;

    int lastTrackCount = -1;
};
