#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include <unordered_map>

namespace te = tracktion;

namespace waive
{

/** Build a map from clip ID to track index for fast lookup.
    Single O(tracks * clips) scan replaces many per-clip linear searches.
*/
std::unordered_map<te::EditItemID, int> buildClipTrackIndexMap (te::Edit& edit);

} // namespace waive
