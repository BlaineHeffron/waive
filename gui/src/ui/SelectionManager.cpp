#include "SelectionManager.h"

void SelectionManager::selectClip (te::Clip* clip, bool addToSelection)
{
    if (! addToSelection)
        selectedClips.clear();

    if (clip != nullptr && ! selectedClips.contains (clip))
        selectedClips.add (clip);

    listeners.call (&Listener::selectionChanged);
}

void SelectionManager::deselectAll()
{
    if (selectedClips.isEmpty())
        return;

    selectedClips.clear();
    listeners.call (&Listener::selectionChanged);
}

bool SelectionManager::isSelected (te::Clip* clip) const
{
    return selectedClips.contains (clip);
}

juce::Array<te::Clip*> SelectionManager::getSelectedClips() const
{
    return selectedClips;
}
