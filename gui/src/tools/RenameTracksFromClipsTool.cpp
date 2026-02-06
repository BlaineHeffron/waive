#include "RenameTracksFromClipsTool.h"

#include <limits>
#include <map>
#include <tracktion_engine/tracktion_engine.h>

#include "EditSession.h"
#include "JobQueue.h"
#include "ProjectManager.h"
#include "SessionComponent.h"
#include "SelectionManager.h"
#include "TimelineComponent.h"

namespace te = tracktion;

namespace
{
struct TrackRenameCandidate
{
    int trackIndex = -1;
    juce::String currentName;
    juce::String newName;
};

bool parseSelectedOnly (const juce::var& params)
{
    bool selectedOnly = true;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("selected_only"))
            selectedOnly = (bool) paramsObj->getProperty ("selected_only");
    }

    return selectedOnly;
}

int findTrackIndexForClip (te::Edit& edit, const te::Clip& clip)
{
    auto tracks = te::getAudioTracks (edit);
    for (int i = 0; i < tracks.size(); ++i)
    {
        auto* track = tracks.getUnchecked (i);
        if (track == nullptr)
            continue;

        for (auto* trackClip : track->getClips())
            if (trackClip == &clip)
                return i;
    }

    return -1;
}

juce::String sanitiseTrackName (const juce::String& clipName)
{
    auto name = clipName.trim();

    if (name.containsChar ('.'))
        name = juce::File (name).getFileNameWithoutExtension();

    while (name.contains ("__"))
        name = name.replace ("__", "_");

    name = name.replaceCharacter ('_', ' ').trim();

    return name;
}

void writePlanArtifact (const juce::File& cacheDirectory, const juce::String& toolName, waive::ToolPlan& plan)
{
    if (cacheDirectory == juce::File())
        return;

    auto artifactDir = cacheDirectory.getChildFile ("tools").getChildFile (toolName);
    artifactDir.createDirectory();

    auto artifact = artifactDir.getChildFile ("plan_" + plan.planID + ".json");
    artifact.replaceWithText (juce::JSON::toString (waive::toolPlanToJson (plan), true));
    plan.artifactFile = artifact;
}
}

namespace waive
{

ToolDescription RenameTracksFromClipsTool::describe() const
{
    ToolDescription desc;
    desc.name = "rename_tracks_from_clips";
    desc.displayName = "Rename Tracks From Clips";
    desc.version = "1.0.0";
    desc.description = "Rename tracks using the earliest clip name found on each target track.";

    auto* schemaObj = new juce::DynamicObject();
    schemaObj->setProperty ("type", "object");

    auto* propsObj = new juce::DynamicObject();
    auto* selectedOnlyObj = new juce::DynamicObject();
    selectedOnlyObj->setProperty ("type", "boolean");
    selectedOnlyObj->setProperty ("default", true);
    selectedOnlyObj->setProperty ("description", "Rename only tracks that contain selected clips");
    propsObj->setProperty ("selected_only", juce::var (selectedOnlyObj));

    schemaObj->setProperty ("properties", juce::var (propsObj));
    desc.inputSchema = juce::var (schemaObj);

    auto* defaults = new juce::DynamicObject();
    defaults->setProperty ("selected_only", true);
    desc.defaultParams = juce::var (defaults);

    return desc;
}

juce::Result RenameTracksFromClipsTool::preparePlan (const ToolExecutionContext& context,
                                                     const juce::var& params,
                                                     ToolPlanTask& outTask)
{
    const bool selectedOnly = parseSelectedOnly (params);

    auto& edit = context.editSession.getEdit();
    auto tracks = te::getAudioTracks (edit);

    std::vector<TrackRenameCandidate> renameCandidates;

    if (selectedOnly)
    {
        auto selectedClips = context.sessionComponent.getTimeline().getSelectionManager().getSelectedClips();
        if (selectedClips.isEmpty())
            return juce::Result::fail ("No clips selected");

        std::map<int, std::pair<double, juce::String>> earliestClipByTrack;

        for (auto* clip : selectedClips)
        {
            if (clip == nullptr)
                continue;

            const int trackIndex = findTrackIndexForClip (edit, *clip);
            if (trackIndex < 0)
                continue;

            const auto clipStart = clip->getPosition().getStart().inSeconds();
            auto found = earliestClipByTrack.find (trackIndex);
            if (found == earliestClipByTrack.end() || clipStart < found->second.first)
                earliestClipByTrack[trackIndex] = { clipStart, clip->getName() };
        }

        for (const auto& pair : earliestClipByTrack)
        {
            const int trackIndex = pair.first;
            if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
                continue;

            auto* track = tracks.getUnchecked (trackIndex);
            if (track == nullptr)
                continue;

            const auto newName = sanitiseTrackName (pair.second.second);
            if (newName.isEmpty() || newName == track->getName())
                continue;

            renameCandidates.push_back ({ trackIndex, track->getName(), newName });
        }
    }
    else
    {
        for (int i = 0; i < tracks.size(); ++i)
        {
            auto* track = tracks.getUnchecked (i);
            if (track == nullptr)
                continue;

            te::Clip* earliestClip = nullptr;
            double earliestStart = std::numeric_limits<double>::max();

            for (auto* clip : track->getClips())
            {
                if (clip == nullptr)
                    continue;

                const auto clipStart = clip->getPosition().getStart().inSeconds();
                if (earliestClip == nullptr || clipStart < earliestStart)
                {
                    earliestClip = clip;
                    earliestStart = clipStart;
                }
            }

            if (earliestClip == nullptr)
                continue;

            const auto newName = sanitiseTrackName (earliestClip->getName());
            if (newName.isEmpty() || newName == track->getName())
                continue;

            renameCandidates.push_back ({ i, track->getName(), newName });
        }
    }

    if (renameCandidates.empty())
        return juce::Result::fail ("No track renames required");

    const auto cacheDirectory = context.projectCacheDirectory;
    const auto description = describe();

    outTask.jobName = "Plan: " + description.displayName;
    outTask.run = [renameCandidates = std::move (renameCandidates),
                   selectedOnly, params, cacheDirectory, description] (ProgressReporter& reporter)
    {
        ToolPlan plan;
        plan.toolName = description.name;
        plan.toolVersion = description.version;
        plan.planID = juce::Uuid().toString();
        plan.inputParams = params;

        for (int i = 0; i < (int) renameCandidates.size(); ++i)
        {
            if (reporter.isCancelled())
                return plan;

            const auto& candidate = renameCandidates[(size_t) i];

            ToolDiffEntry change;
            change.kind = ToolDiffKind::trackRenamed;
            change.trackIndex = candidate.trackIndex;
            change.parameterID = "track.name";
            change.targetName = candidate.newName;
            change.beforeText = candidate.currentName;
            change.afterText = candidate.newName;
            change.summary = "Rename track " + juce::String (candidate.trackIndex + 1)
                             + " '" + candidate.currentName + "' -> '" + candidate.newName + "'";

            plan.changes.add (change);

            reporter.setProgress ((float) (i + 1) / (float) renameCandidates.size(),
                                  "Prepared " + juce::String (i + 1) + " / "
                                  + juce::String ((int) renameCandidates.size()) + " rename(s)");
        }

        plan.summary = "Rename " + juce::String (plan.changes.size()) + " track(s)"
                       + (selectedOnly ? " from selected clips" : " from earliest track clips");

        writePlanArtifact (cacheDirectory, description.name, plan);
        return plan;
    };

    return juce::Result::ok();
}

juce::Result RenameTracksFromClipsTool::apply (const ToolExecutionContext& context,
                                               const ToolPlan& plan)
{
    if (plan.toolName != describe().name)
        return juce::Result::fail ("Tool plan does not match track rename tool");

    if (plan.changes.isEmpty())
        return juce::Result::fail ("Tool plan has no changes to apply");

    int appliedCount = 0;
    const auto ok = context.editSession.performEdit ("Rename Tracks From Clips", [&] (te::Edit& edit)
    {
        auto tracks = te::getAudioTracks (edit);

        for (const auto& change : plan.changes)
        {
            if (change.kind != ToolDiffKind::trackRenamed || change.parameterID != "track.name")
                continue;

            if (! juce::isPositiveAndBelow (change.trackIndex, tracks.size()))
                continue;

            auto* track = tracks.getUnchecked (change.trackIndex);
            if (track == nullptr)
                continue;

            auto newName = change.afterText.isNotEmpty() ? change.afterText : change.targetName;
            if (newName.isEmpty())
                continue;

            track->setName (newName);
            ++appliedCount;
        }
    });

    if (! ok)
        return juce::Result::fail ("Failed to apply track rename changes");

    if (appliedCount <= 0)
        return juce::Result::fail ("No track renames from the plan could be applied");

    return juce::Result::ok();
}

} // namespace waive
