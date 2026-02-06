#include "AlignClipsByTransientTool.h"

#include <algorithm>
#include <limits>
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
struct ClipPlanInput
{
    te::EditItemID clipID;
    juce::String clipName;
    juce::File sourceFile;
    int trackIndex = -1;
    double clipStartSeconds = 0.0;
};

double parseThresholdDb (const juce::var& params)
{
    double thresholdDb = -30.0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("threshold_db"))
            thresholdDb = (double) paramsObj->getProperty ("threshold_db");
    }

    return juce::jlimit (-80.0, -6.0, thresholdDb);
}

double parseMaxShiftSeconds (const juce::var& params)
{
    double maxShiftMs = 500.0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("max_shift_ms"))
            maxShiftMs = (double) paramsObj->getProperty ("max_shift_ms");
    }

    return juce::jlimit (1.0, 5000.0, maxShiftMs) / 1000.0;
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

ToolDescription AlignClipsByTransientTool::describe() const
{
    ToolDescription desc;
    desc.name = "align_clips_by_transient";
    desc.displayName = "Align Clips By Transient";
    desc.version = "1.0.0";
    desc.description = "Align selected clips by their first detected transient.";

    auto* schemaObj = new juce::DynamicObject();
    schemaObj->setProperty ("type", "object");

    auto* propsObj = new juce::DynamicObject();

    auto* thresholdObj = new juce::DynamicObject();
    thresholdObj->setProperty ("type", "number");
    thresholdObj->setProperty ("minimum", -80.0);
    thresholdObj->setProperty ("maximum", -6.0);
    thresholdObj->setProperty ("default", -30.0);
    thresholdObj->setProperty ("description", "Transient detection threshold in dBFS");
    propsObj->setProperty ("threshold_db", juce::var (thresholdObj));

    auto* maxShiftObj = new juce::DynamicObject();
    maxShiftObj->setProperty ("type", "number");
    maxShiftObj->setProperty ("minimum", 1.0);
    maxShiftObj->setProperty ("maximum", 5000.0);
    maxShiftObj->setProperty ("default", 500.0);
    maxShiftObj->setProperty ("description", "Maximum clip shift in milliseconds");
    propsObj->setProperty ("max_shift_ms", juce::var (maxShiftObj));

    auto* delayObj = new juce::DynamicObject();
    delayObj->setProperty ("type", "integer");
    delayObj->setProperty ("minimum", 0);
    delayObj->setProperty ("default", 0);
    delayObj->setProperty ("description", "Extra analysis delay per clip for deterministic cancellation tests");
    propsObj->setProperty ("analysis_delay_ms", juce::var (delayObj));

    schemaObj->setProperty ("properties", juce::var (propsObj));
    desc.inputSchema = juce::var (schemaObj);

    auto* defaults = new juce::DynamicObject();
    defaults->setProperty ("threshold_db", -30.0);
    defaults->setProperty ("max_shift_ms", 500.0);
    defaults->setProperty ("analysis_delay_ms", 0);
    desc.defaultParams = juce::var (defaults);

    return desc;
}

juce::Result AlignClipsByTransientTool::preparePlan (const ToolExecutionContext& context,
                                                     const juce::var& params,
                                                     ToolPlanTask& outTask)
{
    auto selectedClips = context.sessionComponent.getTimeline().getSelectionManager().getSelectedClips();
    if (selectedClips.size() < 2)
        return juce::Result::fail ("Select at least two clips for transient alignment");

    auto& edit = context.editSession.getEdit();
    std::vector<ClipPlanInput> clipsToProcess;
    clipsToProcess.reserve ((size_t) selectedClips.size());

    for (auto* clip : selectedClips)
    {
        if (clip == nullptr)
            continue;

        auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip);
        if (waveClip == nullptr)
            continue;

        const auto sourceFile = waveClip->getSourceFileReference().getFile();
        if (! sourceFile.existsAsFile())
            continue;

        ClipPlanInput input;
        input.clipID = clip->itemID;
        input.clipName = clip->getName();
        input.sourceFile = sourceFile;
        input.trackIndex = findTrackIndexForClip (edit, *clip);
        input.clipStartSeconds = clip->getPosition().getStart().inSeconds();

        if (input.trackIndex >= 0)
            clipsToProcess.push_back (std::move (input));
    }

    if (clipsToProcess.size() < 2)
        return juce::Result::fail ("Selected clips are not analysable audio clips");

    const auto thresholdDb = parseThresholdDb (params);
    const auto thresholdGain = juce::Decibels::decibelsToGain ((float) thresholdDb);
    const auto transientRiseGain = juce::jmax (0.01f, thresholdGain * 0.5f);
    const auto maxShiftSeconds = parseMaxShiftSeconds (params);
    const auto analysisDelayMs = parseAnalysisDelayMs (params);
    const auto cacheDirectory = context.projectCacheDirectory;
    const auto description = describe();

    outTask.jobName = "Plan: " + description.displayName;
    outTask.run = [clipsToProcess = std::move (clipsToProcess),
                   thresholdGain, transientRiseGain, maxShiftSeconds,
                   analysisDelayMs, cacheDirectory, params, description] (ProgressReporter& reporter)
    {
        ToolPlan plan;
        plan.toolName = description.name;
        plan.toolVersion = description.version;
        plan.planID = juce::Uuid().toString();
        plan.inputParams = params;

        struct ClipTransient
        {
            ClipPlanInput input;
            double absoluteTransientSeconds = 0.0;
        };

        std::vector<ClipTransient> analysedClips;
        analysedClips.reserve (clipsToProcess.size());

        for (int i = 0; i < (int) clipsToProcess.size(); ++i)
        {
            if (reporter.isCancelled())
                return plan;

            sleepWithCancellation (reporter, analysisDelayMs);
            if (reporter.isCancelled())
                return plan;

            const auto& clipInput = clipsToProcess[(size_t) i];
            const auto analysis = analyseAudioFile (
                clipInput.sourceFile,
                thresholdGain,
                transientRiseGain,
                [&reporter]() { return reporter.isCancelled(); });

            if (! analysis.valid || analysis.sampleRate <= 0.0 || analysis.firstTransientSample < 0)
            {
                reporter.setProgress ((float) (i + 1) / (float) clipsToProcess.size(),
                                      "Skipped clip: " + clipInput.clipName);
                continue;
            }

            const auto transientOffsetSeconds = (double) analysis.firstTransientSample / analysis.sampleRate;

            ClipTransient transient;
            transient.input = clipInput;
            transient.absoluteTransientSeconds = clipInput.clipStartSeconds + transientOffsetSeconds;
            analysedClips.push_back (std::move (transient));

            reporter.setProgress ((float) (i + 1) / (float) clipsToProcess.size(),
                                  "Analysed " + juce::String (i + 1) + " / "
                                  + juce::String ((int) clipsToProcess.size()));
        }

        if (analysedClips.size() < 2)
            return plan;

        double referenceTransient = std::numeric_limits<double>::max();
        for (const auto& clip : analysedClips)
            referenceTransient = std::min (referenceTransient, clip.absoluteTransientSeconds);

        for (const auto& analysed : analysedClips)
        {
            const auto deltaSeconds = analysed.absoluteTransientSeconds - referenceTransient;
            auto newStartSeconds = analysed.input.clipStartSeconds - deltaSeconds;

            const auto unclampedDelta = newStartSeconds - analysed.input.clipStartSeconds;
            if (std::abs (unclampedDelta) > maxShiftSeconds)
                newStartSeconds = analysed.input.clipStartSeconds
                                  + (unclampedDelta > 0.0 ? maxShiftSeconds : -maxShiftSeconds);

            newStartSeconds = juce::jmax (0.0, newStartSeconds);

            if (std::abs (newStartSeconds - analysed.input.clipStartSeconds) < 0.001)
                continue;

            ToolDiffEntry change;
            change.kind = ToolDiffKind::clipMoved;
            change.trackIndex = analysed.input.trackIndex;
            change.clipID = analysed.input.clipID;
            change.targetName = analysed.input.clipName;
            change.parameterID = "clip.start_seconds";
            change.beforeValue = analysed.input.clipStartSeconds;
            change.afterValue = newStartSeconds;
            change.summary = "Move clip '" + analysed.input.clipName + "' "
                             + juce::String (analysed.input.clipStartSeconds, 3) + " s -> "
                             + juce::String (newStartSeconds, 3) + " s";

            plan.changes.add (change);
        }

        plan.summary = "Align " + juce::String (plan.changes.size())
                       + " clip(s) by detected transient";

        writePlanArtifact (cacheDirectory, description.name, plan);
        return plan;
    };

    return juce::Result::ok();
}

juce::Result AlignClipsByTransientTool::apply (const ToolExecutionContext& context,
                                               const ToolPlan& plan)
{
    if (plan.toolName != describe().name)
        return juce::Result::fail ("Tool plan does not match transient align tool");

    if (plan.changes.isEmpty())
        return juce::Result::fail ("Tool plan has no changes to apply");

    int appliedCount = 0;
    const auto ok = context.editSession.performEdit ("Align Clips By Transient", [&] (te::Edit& edit)
    {
        for (const auto& change : plan.changes)
        {
            if (change.kind != ToolDiffKind::clipMoved || change.parameterID != "clip.start_seconds")
                continue;

            auto* clip = te::findClipForID (edit, change.clipID);
            if (clip == nullptr)
                continue;

            clip->setStart (te::TimePosition::fromSeconds (change.afterValue), true, false);
            ++appliedCount;
        }
    });

    if (! ok)
        return juce::Result::fail ("Failed to apply transient alignment changes");

    if (appliedCount <= 0)
        return juce::Result::fail ("No transient alignment changes from the plan could be applied");

    return juce::Result::ok();
}

} // namespace waive
