#include "AutoMixSuggestionsTool.h"

#include <algorithm>
#include <map>
#include <tracktion_engine/tracktion_engine.h>

#include "AudioAnalysis.h"
#include "EditSession.h"
#include "JobQueue.h"
#include "ModelManager.h"
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
    float currentPan = 0.0f;
    std::vector<ClipAnalysisInput> clips;
};

double parseTargetPeakDb (const juce::var& params)
{
    double targetPeakDb = -14.0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("target_peak_db"))
            targetPeakDb = (double) paramsObj->getProperty ("target_peak_db");
    }

    return juce::jlimit (-24.0, -6.0, targetPeakDb);
}

double parseMaxAdjustDb (const juce::var& params)
{
    double maxAdjustDb = 8.0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("max_adjust_db"))
            maxAdjustDb = (double) paramsObj->getProperty ("max_adjust_db");
    }

    return juce::jlimit (1.0, 24.0, maxAdjustDb);
}

bool parseStereoSpread (const juce::var& params)
{
    bool stereoSpread = true;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("stereo_spread"))
            stereoSpread = (bool) paramsObj->getProperty ("stereo_spread");
    }

    return stereoSpread;
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

juce::String parseRequestedModelVersion (const juce::var& params)
{
    juce::String version;
    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("model_version"))
            version = paramsObj->getProperty ("model_version").toString().trim();
    }
    return version;
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

void writePlanArtifact (const juce::File& cacheDirectory,
                        const juce::String& toolName,
                        const juce::String& modelVersion,
                        waive::ToolPlan& plan)
{
    if (cacheDirectory == juce::File())
        return;

    auto artifactDir = cacheDirectory.getChildFile ("tools").getChildFile (toolName);
    artifactDir.createDirectory();

    auto artifact = artifactDir.getChildFile ("plan_" + plan.planID + ".json");
    auto planJson = waive::toolPlanToJson (plan);
    if (auto* root = planJson.getDynamicObject())
        root->setProperty ("model_version", modelVersion);

    artifact.replaceWithText (juce::JSON::toString (planJson, true));
    plan.artifactFile = artifact;
}
}

namespace waive
{

ToolDescription AutoMixSuggestionsTool::describe() const
{
    ToolDescription desc;
    desc.name = "auto_mix_suggestions";
    desc.displayName = "Auto-Mix Suggestions (Model)";
    desc.version = "1.0.0";
    desc.description = "Suggest track volume/pan moves from selected clips. Requires installed auto-mix model.";

    auto* schemaObj = new juce::DynamicObject();
    schemaObj->setProperty ("type", "object");

    auto* propsObj = new juce::DynamicObject();

    auto* modelVersionObj = new juce::DynamicObject();
    modelVersionObj->setProperty ("type", "string");
    modelVersionObj->setProperty ("default", "");
    modelVersionObj->setProperty ("description", "Optional installed model version. Empty resolves pinned/latest.");
    propsObj->setProperty ("model_version", juce::var (modelVersionObj));

    auto* targetObj = new juce::DynamicObject();
    targetObj->setProperty ("type", "number");
    targetObj->setProperty ("minimum", -24.0);
    targetObj->setProperty ("maximum", -6.0);
    targetObj->setProperty ("default", -14.0);
    targetObj->setProperty ("description", "Target per-track peak level in dBFS.");
    propsObj->setProperty ("target_peak_db", juce::var (targetObj));

    auto* maxAdjustObj = new juce::DynamicObject();
    maxAdjustObj->setProperty ("type", "number");
    maxAdjustObj->setProperty ("minimum", 1.0);
    maxAdjustObj->setProperty ("maximum", 24.0);
    maxAdjustObj->setProperty ("default", 8.0);
    maxAdjustObj->setProperty ("description", "Maximum absolute fader move per track in dB.");
    propsObj->setProperty ("max_adjust_db", juce::var (maxAdjustObj));

    auto* spreadObj = new juce::DynamicObject();
    spreadObj->setProperty ("type", "boolean");
    spreadObj->setProperty ("default", true);
    spreadObj->setProperty ("description", "Apply deterministic stereo spread suggestions.");
    propsObj->setProperty ("stereo_spread", juce::var (spreadObj));

    auto* delayObj = new juce::DynamicObject();
    delayObj->setProperty ("type", "integer");
    delayObj->setProperty ("minimum", 0);
    delayObj->setProperty ("default", 0);
    delayObj->setProperty ("description", "Extra analysis delay per track for deterministic cancellation tests.");
    propsObj->setProperty ("analysis_delay_ms", juce::var (delayObj));

    schemaObj->setProperty ("properties", juce::var (propsObj));
    desc.inputSchema = juce::var (schemaObj);

    auto* defaults = new juce::DynamicObject();
    defaults->setProperty ("model_version", "");
    defaults->setProperty ("target_peak_db", -14.0);
    defaults->setProperty ("max_adjust_db", 8.0);
    defaults->setProperty ("stereo_spread", true);
    defaults->setProperty ("analysis_delay_ms", 0);
    desc.defaultParams = juce::var (defaults);

    desc.modelRequirement = "auto_mix_suggester";

    return desc;
}

juce::Result AutoMixSuggestionsTool::preparePlan (const ToolExecutionContext& context,
                                                  const juce::var& params,
                                                  ToolPlanTask& outTask)
{
    const auto requestedModelVersion = parseRequestedModelVersion (params);
    auto resolvedModel = context.modelManager.resolveInstalledModel ("auto_mix_suggester", requestedModelVersion);
    if (! resolvedModel.has_value())
    {
        return juce::Result::fail ("Model 'auto_mix_suggester' is not installed. "
                                   "Install it via ModelManager before planning.");
    }

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

        const auto trackIndex = findTrackIndexForClip (edit, *clip);
        if (! juce::isPositiveAndBelow (trackIndex, tracks.size()))
            continue;

        auto* track = tracks.getUnchecked (trackIndex);
        if (track == nullptr)
            continue;

        auto* volumePlugin = track->getVolumePlugin();
        if (volumePlugin == nullptr)
            continue;

        const auto sourceFile = waveClip->getSourceFileReference().getFile();
        if (! sourceFile.existsAsFile())
            continue;

        auto found = trackPlans.find (trackIndex);
        if (found == trackPlans.end())
        {
            TrackPlanInput input;
            input.trackIndex = trackIndex;
            input.trackName = track->getName();
            input.currentVolumeDb = te::volumeFaderPositionToDB (volumePlugin->volParam->getCurrentValue());
            input.currentPan = volumePlugin->panParam->getCurrentValue() * 2.0f - 1.0f;
            found = trackPlans.emplace (trackIndex, std::move (input)).first;
        }

        ClipAnalysisInput clipInput;
        clipInput.sourceFile = sourceFile;
        clipInput.clipGainDb = audioClip->getGainDB();
        found->second.clips.push_back (clipInput);
    }

    if (trackPlans.empty())
        return juce::Result::fail ("Selected clips do not map to analysable tracks");

    std::vector<TrackPlanInput> tracksToProcess;
    tracksToProcess.reserve (trackPlans.size());
    for (auto& entry : trackPlans)
        tracksToProcess.push_back (std::move (entry.second));

    const auto targetPeakDb = parseTargetPeakDb (params);
    const auto maxAdjustDb = parseMaxAdjustDb (params);
    const auto stereoSpread = parseStereoSpread (params);
    const auto analysisDelayMs = parseAnalysisDelayMs (params);
    const auto modelInfo = *resolvedModel;
    const auto cacheDirectory = context.projectCacheDirectory;
    const auto description = describe();

    outTask.jobName = "Plan: " + description.displayName;
    outTask.run = [tracksToProcess = std::move (tracksToProcess),
                   targetPeakDb,
                   maxAdjustDb,
                   stereoSpread,
                   analysisDelayMs,
                   modelInfo,
                   cacheDirectory,
                   params,
                   description] (ProgressReporter& reporter) mutable
    {
        ToolPlan plan;
        plan.toolName = description.name;
        plan.toolVersion = description.version;
        plan.planID = juce::Uuid().toString();
        plan.inputParams = params;

        std::sort (tracksToProcess.begin(), tracksToProcess.end(),
                   [] (const auto& lhs, const auto& rhs) { return lhs.trackIndex < rhs.trackIndex; });

        const int totalTracks = (int) tracksToProcess.size();
        for (int i = 0; i < totalTracks; ++i)
        {
            if (reporter.isCancelled())
                return plan;

            sleepWithCancellation (reporter, analysisDelayMs);
            if (reporter.isCancelled())
                return plan;

            const auto& trackInput = tracksToProcess[(size_t) i];
            float peak = 0.0f;

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
                peak = juce::jmax (peak, effectivePeak);
            }

            if (peak <= 0.0f)
                continue;

            const auto sourcePeakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
            const auto desiredDeltaDb = targetPeakDb - sourcePeakDb;
            const auto constrainedDeltaDb = juce::jlimit (-maxAdjustDb, maxAdjustDb, desiredDeltaDb);
            const auto suggestedVolumeDb = juce::jlimit (-60.0, 6.0,
                                                         (double) trackInput.currentVolumeDb + constrainedDeltaDb);

            ToolDiffEntry volumeChange;
            volumeChange.kind = ToolDiffKind::parameterChanged;
            volumeChange.trackIndex = trackInput.trackIndex;
            volumeChange.targetName = trackInput.trackName;
            volumeChange.parameterID = "track.volume_db";
            volumeChange.beforeValue = trackInput.currentVolumeDb;
            volumeChange.afterValue = suggestedVolumeDb;
            volumeChange.summary = "Suggest volume for '" + trackInput.trackName + "' "
                                   + juce::String (trackInput.currentVolumeDb, 2) + " dB -> "
                                   + juce::String (suggestedVolumeDb, 2) + " dB";
            plan.changes.add (volumeChange);

            if (stereoSpread)
            {
                double targetPan = 0.0;
                if (totalTracks > 1)
                    targetPan = -0.6 + (1.2 * (double) i / (double) (totalTracks - 1));

                const auto suggestedPan = juce::jlimit (-1.0, 1.0,
                                                        (double) trackInput.currentPan * 0.35 + targetPan * 0.65);
                if (std::abs (suggestedPan - trackInput.currentPan) > 0.03)
                {
                    ToolDiffEntry panChange;
                    panChange.kind = ToolDiffKind::parameterChanged;
                    panChange.trackIndex = trackInput.trackIndex;
                    panChange.targetName = trackInput.trackName;
                    panChange.parameterID = "track.pan";
                    panChange.beforeValue = trackInput.currentPan;
                    panChange.afterValue = suggestedPan;
                    panChange.summary = "Suggest pan for '" + trackInput.trackName + "' "
                                        + juce::String (trackInput.currentPan, 2) + " -> "
                                        + juce::String (suggestedPan, 2);
                    plan.changes.add (panChange);
                }
            }

            reporter.setProgress ((float) (i + 1) / (float) totalTracks,
                                  "Analysed track " + juce::String (i + 1) + " / "
                                  + juce::String (totalTracks));
        }

        plan.summary = "Auto-mix suggestions for " + juce::String (totalTracks)
                       + " track(s) with model " + modelInfo.version;
        writePlanArtifact (cacheDirectory, description.name, modelInfo.version, plan);
        return plan;
    };

    return juce::Result::ok();
}

juce::Result AutoMixSuggestionsTool::apply (const ToolExecutionContext& context,
                                            const ToolPlan& plan)
{
    if (plan.toolName != describe().name)
        return juce::Result::fail ("Tool plan does not match auto-mix suggestion tool");

    if (plan.changes.isEmpty())
        return juce::Result::fail ("Tool plan has no changes to apply");

    int appliedCount = 0;
    const auto ok = context.editSession.performEdit ("Apply Auto-Mix Suggestions", [&] (te::Edit& edit)
    {
        auto tracks = te::getAudioTracks (edit);
        for (const auto& change : plan.changes)
        {
            if (change.kind != ToolDiffKind::parameterChanged)
                continue;
            if (! juce::isPositiveAndBelow (change.trackIndex, tracks.size()))
                continue;

            auto* track = tracks.getUnchecked (change.trackIndex);
            if (track == nullptr)
                continue;

            auto* volumePlugin = track->getVolumePlugin();
            if (volumePlugin == nullptr)
                continue;

            if (change.parameterID == "track.volume_db")
            {
                volumePlugin->volume.setValue (
                    te::decibelsToVolumeFaderPosition ((float) change.afterValue),
                    &edit.getUndoManager());
                ++appliedCount;
            }
            else if (change.parameterID == "track.pan")
            {
                const auto normalisedPan = juce::jlimit (0.0f, 1.0f,
                                                         ((float) change.afterValue + 1.0f) * 0.5f);
                volumePlugin->pan.setValue (normalisedPan, &edit.getUndoManager());
                ++appliedCount;
            }
        }
    });

    if (! ok)
        return juce::Result::fail ("Failed to apply auto-mix suggestions");

    if (appliedCount <= 0)
        return juce::Result::fail ("No auto-mix changes from the plan could be applied");

    return juce::Result::ok();
}

} // namespace waive
