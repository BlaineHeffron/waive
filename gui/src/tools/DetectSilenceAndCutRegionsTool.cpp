#include "DetectSilenceAndCutRegionsTool.h"

#include <algorithm>
#include <map>
#include <optional>
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
    double clipEndSeconds = 0.0;
};

double parseThresholdDb (const juce::var& params)
{
    double thresholdDb = -42.0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("threshold_db"))
            thresholdDb = (double) paramsObj->getProperty ("threshold_db");
    }

    return juce::jlimit (-80.0, -6.0, thresholdDb);
}

double parseMinTrimSeconds (const juce::var& params)
{
    double minTrimMs = 20.0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("min_trim_ms"))
            minTrimMs = (double) paramsObj->getProperty ("min_trim_ms");
    }

    return juce::jlimit (0.0, 2000.0, minTrimMs) / 1000.0;
}

double parsePaddingSeconds (const juce::var& params)
{
    double paddingMs = 5.0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("padding_ms"))
            paddingMs = (double) paramsObj->getProperty ("padding_ms");
    }

    return juce::jlimit (0.0, 250.0, paddingMs) / 1000.0;
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

ToolDescription DetectSilenceAndCutRegionsTool::describe() const
{
    ToolDescription desc;
    desc.name = "detect_silence_and_cut_regions";
    desc.displayName = "Detect Silence And Cut Regions";
    desc.version = "1.0.0";
    desc.description = "Trim leading and trailing silence from selected audio clips.";

    auto* schemaObj = new juce::DynamicObject();
    schemaObj->setProperty ("type", "object");

    auto* propsObj = new juce::DynamicObject();

    auto* thresholdObj = new juce::DynamicObject();
    thresholdObj->setProperty ("type", "number");
    thresholdObj->setProperty ("minimum", -80.0);
    thresholdObj->setProperty ("maximum", -6.0);
    thresholdObj->setProperty ("default", -42.0);
    thresholdObj->setProperty ("description", "Silence threshold in dBFS");
    propsObj->setProperty ("threshold_db", juce::var (thresholdObj));

    auto* minTrimObj = new juce::DynamicObject();
    minTrimObj->setProperty ("type", "number");
    minTrimObj->setProperty ("minimum", 0.0);
    minTrimObj->setProperty ("maximum", 2000.0);
    minTrimObj->setProperty ("default", 20.0);
    minTrimObj->setProperty ("description", "Minimum trim size per side in milliseconds");
    propsObj->setProperty ("min_trim_ms", juce::var (minTrimObj));

    auto* paddingObj = new juce::DynamicObject();
    paddingObj->setProperty ("type", "number");
    paddingObj->setProperty ("minimum", 0.0);
    paddingObj->setProperty ("maximum", 250.0);
    paddingObj->setProperty ("default", 5.0);
    paddingObj->setProperty ("description", "Padding to keep around detected content in milliseconds");
    propsObj->setProperty ("padding_ms", juce::var (paddingObj));

    auto* delayObj = new juce::DynamicObject();
    delayObj->setProperty ("type", "integer");
    delayObj->setProperty ("minimum", 0);
    delayObj->setProperty ("default", 0);
    delayObj->setProperty ("description", "Extra analysis delay per clip for deterministic cancellation tests");
    propsObj->setProperty ("analysis_delay_ms", juce::var (delayObj));

    schemaObj->setProperty ("properties", juce::var (propsObj));
    desc.inputSchema = juce::var (schemaObj);

    auto* defaults = new juce::DynamicObject();
    defaults->setProperty ("threshold_db", -42.0);
    defaults->setProperty ("min_trim_ms", 20.0);
    defaults->setProperty ("padding_ms", 5.0);
    defaults->setProperty ("analysis_delay_ms", 0);
    desc.defaultParams = juce::var (defaults);

    return desc;
}

juce::Result DetectSilenceAndCutRegionsTool::preparePlan (const ToolExecutionContext& context,
                                                          const juce::var& params,
                                                          ToolPlanTask& outTask)
{
    auto selectedClips = context.sessionComponent.getTimeline().getSelectionManager().getSelectedClips();
    if (selectedClips.isEmpty())
        return juce::Result::fail ("No clips selected");

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
        input.clipEndSeconds = clip->getPosition().getEnd().inSeconds();

        if (input.trackIndex >= 0)
            clipsToProcess.push_back (std::move (input));
    }

    if (clipsToProcess.empty())
        return juce::Result::fail ("Selected clips are not analysable audio clips");

    const auto thresholdDb = parseThresholdDb (params);
    const auto thresholdGain = juce::Decibels::decibelsToGain ((float) thresholdDb);
    const auto minTrimSeconds = parseMinTrimSeconds (params);
    const auto paddingSeconds = parsePaddingSeconds (params);
    const auto analysisDelayMs = parseAnalysisDelayMs (params);
    const auto cacheDirectory = context.projectCacheDirectory;
    const auto description = describe();

    outTask.jobName = "Plan: " + description.displayName;
    outTask.run = [clipsToProcess = std::move (clipsToProcess),
                   thresholdGain, minTrimSeconds, paddingSeconds,
                   analysisDelayMs, cacheDirectory, params, description] (ProgressReporter& reporter)
    {
        ToolPlan plan;
        plan.toolName = description.name;
        plan.toolVersion = description.version;
        plan.planID = juce::Uuid().toString();
        plan.inputParams = params;

        int clippedCount = 0;

        const int total = (int) clipsToProcess.size();
        for (int i = 0; i < total; ++i)
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
                thresholdGain,
                [&reporter]() { return reporter.isCancelled(); });

            if (! analysis.valid || analysis.sampleRate <= 0.0 || analysis.totalSamples <= 0
                || analysis.firstAboveSample < 0 || analysis.lastAboveSample < 0)
            {
                reporter.setProgress ((float) (i + 1) / (float) total,
                                      "Skipped silent clip: " + clipInput.clipName);
                continue;
            }

            const auto clipLengthSeconds = clipInput.clipEndSeconds - clipInput.clipStartSeconds;
            if (clipLengthSeconds <= 0.01)
                continue;

            const auto activeStartSecondsRaw = ((double) analysis.firstAboveSample / analysis.sampleRate) - paddingSeconds;
            const auto activeEndSecondsRaw = ((double) analysis.lastAboveSample / analysis.sampleRate) + paddingSeconds;

            const auto activeStartSeconds = juce::jlimit (0.0, clipLengthSeconds, activeStartSecondsRaw);
            const auto activeEndSeconds = juce::jlimit (0.0, clipLengthSeconds, activeEndSecondsRaw);

            auto newClipStart = clipInput.clipStartSeconds + activeStartSeconds;
            auto newClipEnd = clipInput.clipStartSeconds + activeEndSeconds;

            newClipStart = juce::jlimit (clipInput.clipStartSeconds,
                                         clipInput.clipEndSeconds - 0.01,
                                         newClipStart);
            newClipEnd = juce::jlimit (newClipStart + 0.01,
                                       clipInput.clipEndSeconds,
                                       newClipEnd);

            const auto trimStartSeconds = newClipStart - clipInput.clipStartSeconds;
            const auto trimEndSeconds = clipInput.clipEndSeconds - newClipEnd;

            bool changed = false;
            if (trimStartSeconds >= minTrimSeconds)
            {
                ToolDiffEntry startTrim;
                startTrim.kind = ToolDiffKind::clipTrimmed;
                startTrim.trackIndex = clipInput.trackIndex;
                startTrim.clipID = clipInput.clipID;
                startTrim.targetName = clipInput.clipName;
                startTrim.parameterID = "clip.start_seconds";
                startTrim.beforeValue = clipInput.clipStartSeconds;
                startTrim.afterValue = newClipStart;
                startTrim.summary = "Trim start of clip '" + clipInput.clipName + "' by "
                                    + juce::String (trimStartSeconds, 3) + " s";
                plan.changes.add (startTrim);
                changed = true;
            }

            if (trimEndSeconds >= minTrimSeconds)
            {
                ToolDiffEntry endTrim;
                endTrim.kind = ToolDiffKind::clipTrimmed;
                endTrim.trackIndex = clipInput.trackIndex;
                endTrim.clipID = clipInput.clipID;
                endTrim.targetName = clipInput.clipName;
                endTrim.parameterID = "clip.end_seconds";
                endTrim.beforeValue = clipInput.clipEndSeconds;
                endTrim.afterValue = newClipEnd;
                endTrim.summary = "Trim end of clip '" + clipInput.clipName + "' by "
                                  + juce::String (trimEndSeconds, 3) + " s";
                plan.changes.add (endTrim);
                changed = true;
            }

            if (changed)
                ++clippedCount;

            reporter.setProgress ((float) (i + 1) / (float) total,
                                  "Analysed " + juce::String (i + 1) + " / " + juce::String (total));
        }

        plan.summary = "Trim silence on " + juce::String (clippedCount) + " clip(s)";

        writePlanArtifact (cacheDirectory, description.name, plan);
        return plan;
    };

    return juce::Result::ok();
}

juce::Result DetectSilenceAndCutRegionsTool::apply (const ToolExecutionContext& context,
                                                    const ToolPlan& plan)
{
    if (plan.toolName != describe().name)
        return juce::Result::fail ("Tool plan does not match silence cut tool");

    if (plan.changes.isEmpty())
        return juce::Result::fail ("Tool plan has no changes to apply");

    struct TrimValues
    {
        std::optional<double> start;
        std::optional<double> end;
    };

    struct ClipTrimPlan
    {
        te::EditItemID clipID;
        TrimValues values;
    };

    std::map<uint64_t, ClipTrimPlan> trimsByClip;
    for (const auto& change : plan.changes)
    {
        if (change.kind != ToolDiffKind::clipTrimmed)
            continue;

        if (! change.clipID.isValid())
            continue;

        auto& clipTrim = trimsByClip[(uint64_t) change.clipID.getRawID()];
        clipTrim.clipID = change.clipID;

        if (change.parameterID == "clip.start_seconds")
            clipTrim.values.start = change.afterValue;
        else if (change.parameterID == "clip.end_seconds")
            clipTrim.values.end = change.afterValue;
    }

    if (trimsByClip.empty())
        return juce::Result::fail ("No clip trim changes in plan");

    int appliedCount = 0;
    const auto ok = context.editSession.performEdit ("Detect Silence And Cut Regions", [&] (te::Edit& edit)
    {
        for (const auto& pair : trimsByClip)
        {
            auto* clip = te::findClipForID (edit, pair.second.clipID);
            if (clip == nullptr)
                continue;

            auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip);
            if (waveClip == nullptr)
                continue;

            double newStart = clip->getPosition().getStart().inSeconds();
            double newEnd = clip->getPosition().getEnd().inSeconds();

            if (pair.second.values.start.has_value())
                newStart = *pair.second.values.start;

            if (pair.second.values.end.has_value())
                newEnd = *pair.second.values.end;

            if (newEnd <= newStart + 0.01)
                continue;

            clip->setStart (te::TimePosition::fromSeconds (newStart), false, true);
            clip->setEnd (te::TimePosition::fromSeconds (newEnd), true);
            ++appliedCount;
        }
    });

    if (! ok)
        return juce::Result::fail ("Failed to apply silence trim changes");

    if (appliedCount <= 0)
        return juce::Result::fail ("No clip trim changes from the plan could be applied");

    return juce::Result::ok();
}

} // namespace waive
