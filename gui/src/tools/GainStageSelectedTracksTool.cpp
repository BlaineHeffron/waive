#include "GainStageSelectedTracksTool.h"

#include <algorithm>
#include <map>
#include <tracktion_engine/tracktion_engine.h>

#include "AudioAnalysis.h"
#include "EditSession.h"
#include "JobQueue.h"
#include "ProjectManager.h"
#include "SelectionManager.h"
#include "SessionComponent.h"
#include "TimelineComponent.h"

namespace te = tracktion;

namespace
{
struct ClipAnalysisInput
{
    juce::File sourceFile;
    float clipGainDb = 0.0f;
};

struct TrackPlanInput
{
    int trackIndex = -1;
    juce::String trackName;
    float currentVolumeDb = 0.0f;
    std::vector<ClipAnalysisInput> clips;
};

double parseTargetPeakDb (const juce::var& params)
{
    double targetPeakDb = -12.0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("target_peak_db"))
            targetPeakDb = (double) paramsObj->getProperty ("target_peak_db");
    }

    return juce::jlimit (-24.0, -3.0, targetPeakDb);
}

int parseAnalysisDelayMs (const juce::var& params)
{
    int delayMs = 0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("analysis_delay_ms"))
            delayMs = juce::jmax (0, (int) paramsObj->getProperty ("analysis_delay_ms"));
    }

    return delayMs;
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

void sleepWithCancellation (waive::ProgressReporter& reporter, int delayMs)
{
    if (delayMs <= 0)
        return;

    constexpr int stepMs = 20;
    int waited = 0;
    while (waited < delayMs && ! reporter.isCancelled())
    {
        juce::Thread::sleep (juce::jmin (stepMs, delayMs - waited));
        waited += stepMs;
    }
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

ToolDescription GainStageSelectedTracksTool::describe() const
{
    ToolDescription desc;
    desc.name = "gain_stage_selected_tracks";
    desc.displayName = "Gain-Stage Selected Tracks";
    desc.version = "1.0.0";
    desc.description = "Estimate peak level from selected clips and adjust track fader levels toward a target peak.";

    auto* schemaObj = new juce::DynamicObject();
    schemaObj->setProperty ("type", "object");

    auto* propsObj = new juce::DynamicObject();

    auto* targetObj = new juce::DynamicObject();
    targetObj->setProperty ("type", "number");
    targetObj->setProperty ("minimum", -24.0);
    targetObj->setProperty ("maximum", -3.0);
    targetObj->setProperty ("default", -12.0);
    targetObj->setProperty ("description", "Target track peak in dBFS");
    propsObj->setProperty ("target_peak_db", juce::var (targetObj));

    auto* delayObj = new juce::DynamicObject();
    delayObj->setProperty ("type", "integer");
    delayObj->setProperty ("minimum", 0);
    delayObj->setProperty ("default", 0);
    delayObj->setProperty ("description", "Extra analysis delay per track for deterministic cancellation tests");
    propsObj->setProperty ("analysis_delay_ms", juce::var (delayObj));

    schemaObj->setProperty ("properties", juce::var (propsObj));
    desc.inputSchema = juce::var (schemaObj);

    auto* defaults = new juce::DynamicObject();
    defaults->setProperty ("target_peak_db", -12.0);
    defaults->setProperty ("analysis_delay_ms", 0);
    desc.defaultParams = juce::var (defaults);

    return desc;
}

juce::Result GainStageSelectedTracksTool::preparePlan (const ToolExecutionContext& context,
                                                       const juce::var& params,
                                                       ToolPlanTask& outTask)
{
    auto selectedClips = context.sessionComponent.getTimeline().getSelectionManager().getSelectedClips();
    if (selectedClips.isEmpty())
        return juce::Result::fail ("No clips selected");

    auto& edit = context.editSession.getEdit();
    auto tracks = te::getAudioTracks (edit);

    std::map<int, TrackPlanInput> trackPlans;

    for (auto* clip : selectedClips)
    {
        if (clip == nullptr)
            continue;

        auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip);
        auto* audioClip = dynamic_cast<te::AudioClipBase*> (clip);

        if (waveClip == nullptr || audioClip == nullptr)
            continue;

        const int trackIndex = findTrackIndexForClip (edit, *clip);
        if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
            continue;

        const auto sourceFile = waveClip->getSourceFileReference().getFile();
        if (! sourceFile.existsAsFile())
            continue;

        auto* track = tracks.getUnchecked (trackIndex);
        if (track == nullptr)
            continue;

        auto* volumePlugin = track->getVolumePlugin();
        if (volumePlugin == nullptr)
            continue;

        auto found = trackPlans.find (trackIndex);
        if (found == trackPlans.end())
        {
            TrackPlanInput input;
            input.trackIndex = trackIndex;
            input.trackName = track->getName();
            input.currentVolumeDb = te::volumeFaderPositionToDB (volumePlugin->volume.get());
            found = trackPlans.emplace (trackIndex, std::move (input)).first;
        }

        ClipAnalysisInput clipInput;
        clipInput.sourceFile = sourceFile;
        clipInput.clipGainDb = audioClip->getGainDB();
        found->second.clips.push_back (clipInput);
    }

    if (trackPlans.empty())
        return juce::Result::fail ("Selected clips do not map to analysable audio tracks");

    std::vector<TrackPlanInput> tracksToProcess;
    tracksToProcess.reserve (trackPlans.size());
    for (auto& entry : trackPlans)
        tracksToProcess.push_back (std::move (entry.second));

    const auto targetPeakDb = parseTargetPeakDb (params);
    const auto analysisDelayMs = parseAnalysisDelayMs (params);
    const auto cacheDirectory = context.projectCacheDirectory;
    const auto description = describe();

    outTask.jobName = "Plan: " + description.displayName;
    outTask.run = [tracksToProcess = std::move (tracksToProcess),
                   targetPeakDb, analysisDelayMs, cacheDirectory, params, description] (ProgressReporter& reporter)
    {
        ToolPlan plan;
        plan.toolName = description.name;
        plan.toolVersion = description.version;
        plan.planID = juce::Uuid().toString();
        plan.inputParams = params;

        const int total = (int) tracksToProcess.size();

        for (int i = 0; i < total; ++i)
        {
            if (reporter.isCancelled())
                return plan;

            sleepWithCancellation (reporter, analysisDelayMs);
            if (reporter.isCancelled())
                return plan;

            const auto& trackInput = tracksToProcess[(size_t) i];
            float trackPeak = 0.0f;

            for (const auto& clipInput : trackInput.clips)
            {
                const auto analysis = analyseAudioFile (
                    clipInput.sourceFile,
                    0.0f,
                    0.0f,
                    [&reporter]() { return reporter.isCancelled(); });

                if (! analysis.valid || analysis.peakGain <= 0.0f)
                    continue;

                const auto effectivePeak = analysis.peakGain * juce::Decibels::decibelsToGain (clipInput.clipGainDb);
                trackPeak = juce::jmax (trackPeak, effectivePeak);
            }

            if (trackPeak > 0.0f)
            {
                const auto sourcePeakDb = juce::Decibels::gainToDecibels (trackPeak, -120.0f);
                const auto gainDeltaDb = targetPeakDb - sourcePeakDb;
                const auto newTrackDb = juce::jlimit (-60.0f, 6.0f,
                                                      trackInput.currentVolumeDb + (float) gainDeltaDb);

                ToolDiffEntry change;
                change.kind = ToolDiffKind::parameterChanged;
                change.trackIndex = trackInput.trackIndex;
                change.targetName = trackInput.trackName;
                change.parameterID = "track.volume_db";
                change.beforeValue = trackInput.currentVolumeDb;
                change.afterValue = newTrackDb;
                change.summary = "Set track '" + trackInput.trackName + "' volume "
                                 + juce::String (trackInput.currentVolumeDb, 2) + " dB -> "
                                 + juce::String (newTrackDb, 2) + " dB";

                plan.changes.add (change);
            }

            reporter.setProgress ((float) (i + 1) / (float) total,
                                  "Analysed track " + juce::String (i + 1) + " / "
                                  + juce::String (total));
        }

        plan.summary = "Gain-stage " + juce::String (plan.changes.size()) + " track(s) to target peak "
                       + juce::String (targetPeakDb, 1) + " dBFS";

        writePlanArtifact (cacheDirectory, description.name, plan);
        return plan;
    };

    return juce::Result::ok();
}

juce::Result GainStageSelectedTracksTool::apply (const ToolExecutionContext& context,
                                                 const ToolPlan& plan)
{
    if (plan.toolName != describe().name)
        return juce::Result::fail ("Tool plan does not match gain-stage tool");

    if (plan.changes.isEmpty())
        return juce::Result::fail ("Tool plan has no changes to apply");

    int appliedCount = 0;
    const auto ok = context.editSession.performEdit ("Gain-Stage Selected Tracks", [&] (te::Edit& edit)
    {
        auto tracks = te::getAudioTracks (edit);

        for (const auto& change : plan.changes)
        {
            if (change.kind != ToolDiffKind::parameterChanged || change.parameterID != "track.volume_db")
                continue;

            if (! juce::isPositiveAndBelow (change.trackIndex, tracks.size()))
                continue;

            auto* track = tracks.getUnchecked (change.trackIndex);
            if (track == nullptr)
                continue;

            auto* volumePlugin = track->getVolumePlugin();
            if (volumePlugin == nullptr)
                continue;

            volumePlugin->volume = te::decibelsToVolumeFaderPosition ((float) change.afterValue);
            ++appliedCount;
        }
    });

    if (! ok)
        return juce::Result::fail ("Failed to apply gain-stage changes");

    if (appliedCount <= 0)
        return juce::Result::fail ("No gain-stage changes from the plan could be applied");

    return juce::Result::ok();
}

} // namespace waive
