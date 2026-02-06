#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

//==============================================================================
/** Tracks which clips are selected. */
class SelectionManager
{
public:
    SelectionManager() = default;

    void selectClip (te::Clip* clip, bool addToSelection = false);
    void deselectAll();
    bool isSelected (te::Clip* clip) const;
    juce::Array<te::Clip*> getSelectedClips() const;

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void selectionChanged() = 0;
    };

    void addListener (Listener* l)     { listeners.add (l); }
    void removeListener (Listener* l)  { listeners.remove (l); }

private:
    juce::Array<te::Clip*> selectedClips;
    juce::ListenerList<Listener> listeners;
};
