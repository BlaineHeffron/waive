#include "ExternalTool.h"
#include "ExternalToolRunner.h"
#include "ToolDiff.h"
#include "JobQueue.h"
#include "EditSession.h"
#include "SelectionManager.h"
#include "SessionComponent.h"
#include "TimelineComponent.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace
{
te::AudioTrack* findOrCreateTrackByName (te::Edit& edit, const juce::String& trackName)
{
    auto tracks = te::getAudioTracks (edit);
    for (auto* track : tracks)
    {
        if (track != nullptr && track->getName() == trackName)
            return track;
    }

    const auto before = tracks.size();
    edit.ensureNumberOfAudioTracks (before + 1);
    tracks = te::getAudioTracks (edit);

    if (! juce::isPositiveAndBelow (before, tracks.size()))
        return nullptr;

    auto* createdTrack = tracks.getUnchecked (before);
    if (createdTrack != nullptr)
        createdTrack->setName (trackName);
    return createdTrack;
}
}

namespace waive
{

ExternalTool::ExternalTool (const ExternalToolManifest& m, ExternalToolRunner& r)
    : manifest (m), runner (r)
{
}

ToolDescription ExternalTool::describe() const
{
    ToolDescription desc;
    desc.name = manifest.name;
    desc.displayName = manifest.displayName;
    desc.version = manifest.version;
    desc.description = manifest.description;
    desc.inputSchema = manifest.inputSchema;
    desc.defaultParams = manifest.defaultParams;
    desc.modelRequirement = "";
    return desc;
}

juce::Result ExternalTool::preparePlan (const ToolExecutionContext& context,
                                        const juce::var& params,
                                        ToolPlanTask& outTask)
{
    juce::File inputAudioFile;
    double clipStartSeconds = 0.0;
    double clipEndSeconds = 0.0;
    juce::String clipName;

    if (manifest.acceptsAudioInput)
    {
        auto selectedClips = context.sessionComponent.getTimeline().getSelectionManager().getSelectedClips();
        if (selectedClips.isEmpty())
            return juce::Result::fail ("No clips selected (external tool requires audio input)");

        auto* firstClip = selectedClips.getFirst();
        if (firstClip == nullptr)
            return juce::Result::fail ("Selected clip is invalid");

        auto* waveClip = dynamic_cast<te::WaveAudioClip*> (firstClip);
        if (waveClip == nullptr)
            return juce::Result::fail ("Selected clip is not a wave audio clip");

        inputAudioFile = waveClip->getSourceFileReference().getFile();
        if (! inputAudioFile.existsAsFile())
            return juce::Result::fail ("Selected clip audio file not found");

        clipStartSeconds = firstClip->getPosition().getStart().inSeconds();
        clipEndSeconds = firstClip->getPosition().getEnd().inSeconds();
        clipName = firstClip->getName();
    }

    outTask.jobName = manifest.displayName;
    outTask.run = [this, params, inputAudioFile, clipStartSeconds, clipEndSeconds, clipName]
                  (ProgressReporter& reporter)
    {
        ToolPlan plan;
        plan.toolName = manifest.name;
        plan.toolVersion = manifest.version;
        plan.planID = juce::Uuid().toString();
        plan.inputParams = params;

        auto output = runner.run (manifest, params, inputAudioFile, reporter);

        if (! output.success)
        {
            plan.summary = "External tool failed: " + output.message;
            return plan;
        }

        plan.summary = output.message;

        // If tool produces audio output, add clipInserted entry
        if (manifest.producesAudioOutput && output.outputAudioFile.existsAsFile())
        {
            ToolDiffEntry insert;
            insert.kind = ToolDiffKind::clipInserted;
            insert.targetName = clipName.isEmpty() ? "External Tool Output" : clipName + " [processed]";
            insert.parameterID = "clip.file_path";
            insert.beforeValue = clipStartSeconds;
            insert.afterValue = clipEndSeconds;
            insert.afterText = output.outputAudioFile.getFullPathName();
            insert.summary = "Insert processed audio clip";
            plan.changes.add (insert);
        }

        return plan;
    };

    return juce::Result::ok();
}

juce::Result ExternalTool::apply (const ToolExecutionContext& context,
                                  const ToolPlan& plan)
{
    if (plan.toolName != manifest.name)
        return juce::Result::fail ("Tool plan does not match external tool");

    if (! manifest.producesAudioOutput)
        return juce::Result::ok();

    int appliedCount = 0;
    const auto ok = context.editSession.performEdit (manifest.displayName, [&] (te::Edit& edit)
    {
        auto* destTrack = findOrCreateTrackByName (edit, manifest.displayName + " Output");
        if (destTrack == nullptr)
            return;

        for (const auto& change : plan.changes)
        {
            if (change.kind != ToolDiffKind::clipInserted)
                continue;

            // Validate that output file path is within a safe temp directory
            auto audioFile = juce::File (change.afterText);
            auto canonicalPath = audioFile.getFullPathName();
            auto tempBase = juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName();

            if (! canonicalPath.startsWith (tempBase))
                continue;  // Reject files outside temp directory

            if (canonicalPath.contains ("..") || canonicalPath.startsWith ("/etc") ||
                canonicalPath.startsWith ("/root") || canonicalPath.startsWith ("/sys"))
                continue;  // Reject suspicious paths

            if (! audioFile.existsAsFile())
                continue;

            auto insertedClip = destTrack->insertWaveClip (
                change.targetName,
                audioFile,
                { { te::TimePosition::fromSeconds (change.beforeValue),
                    te::TimePosition::fromSeconds (change.afterValue) },
                  te::TimeDuration() },
                false);

            if (insertedClip != nullptr)
                ++appliedCount;
        }
    });

    if (! ok)
        return juce::Result::fail ("Failed to apply external tool changes");

    if (manifest.producesAudioOutput && appliedCount <= 0)
        return juce::Result::fail ("No output clips from the plan could be inserted");

    return juce::Result::ok();
}

} // namespace waive
