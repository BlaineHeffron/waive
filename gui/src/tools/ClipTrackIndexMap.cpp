#include "ClipTrackIndexMap.h"

namespace waive
{

std::unordered_map<te::EditItemID, int> buildClipTrackIndexMap (te::Edit& edit)
{
    std::unordered_map<te::EditItemID, int> clipToTrack;
    auto tracks = te::getAudioTracks (edit);

    for (int trackIdx = 0; trackIdx < tracks.size(); ++trackIdx)
    {
        auto* track = tracks.getUnchecked (trackIdx);
        if (track == nullptr)
            continue;

        for (auto* clip : track->getClips())
        {
            if (clip != nullptr)
                clipToTrack[clip->itemID] = trackIdx;
        }
    }

    return clipToTrack;
}

} // namespace waive
