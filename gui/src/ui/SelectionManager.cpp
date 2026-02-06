#include "SelectionManager.h"

void SelectionManager::selectClip (te::Clip* clip, bool addToSelection)
{
    if (! addToSelection)
        selectedClipIDs.clear();

    if (clip != nullptr && clip->itemID.isValid() && ! selectedClipIDs.contains (clip->itemID))
        selectedClipIDs.add (clip->itemID);

    listeners.call (&Listener::selectionChanged);
}

void SelectionManager::deselectAll()
{
    if (selectedClipIDs.isEmpty())
        return;

    selectedClipIDs.clear();
    listeners.call (&Listener::selectionChanged);
}

bool SelectionManager::isSelected (te::Clip* clip) const
{
    if (clip == nullptr || ! clip->itemID.isValid())
        return false;

    return selectedClipIDs.contains (clip->itemID);
}

juce::Array<te::Clip*> SelectionManager::getSelectedClips() const
{
    juce::Array<te::Clip*> clips;

    if (currentEdit == nullptr)
        return clips;

    for (auto clipID : selectedClipIDs)
    {
        if (auto* clip = te::findClipForID (*currentEdit, clipID))
            clips.add (clip);
    }

    return clips;
}
