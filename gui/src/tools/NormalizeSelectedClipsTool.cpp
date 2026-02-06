#include "NormalizeSelectedClipsTool.h"

#include <algorithm>
#include <tracktion_engine/tracktion_engine.h>

#include "EditSession.h"
#include "ProjectManager.h"
#include "SessionComponent.h"
#include "TimelineComponent.h"
#include "SelectionManager.h"
#include "JobQueue.h"
#include "ToolDiff.h"

namespace te = tracktion;

namespace
{
struct ClipPlanInput
{
    te::EditItemID clipID;
    juce::String clipName;
    juce::File sourceFile;
    int trackIndex = -1;
    float currentGainDb = 0.0f;
};

double parseTargetPeakDb (const juce::var& params)
{
    double targetPeakDb = -1.0;

    if (auto* paramsObj = params.getDynamicObject())
    {
        if (paramsObj->hasProperty ("target_peak_db"))
            targetPeakDb = (double) paramsObj->getProperty ("target_peak_db");
    }

    return juce::jlimit (-24.0, 0.0, targetPeakDb);
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

float analysePeakGain (const juce::File& sourceFile)
{
    if (! sourceFile.existsAsFile())
        return 0.0f;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (sourceFile));
    if (reader == nullptr || reader->numChannels <= 0)
        return 0.0f;

    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, blockSize);

    float peak = 0.0f;
    int64 samplePos = 0;
    const auto totalSamples = reader->lengthInSamples;

    while (samplePos < totalSamples)
    {
        const auto samplesThisBlock = (int) std::min<int64> ((int64) blockSize, totalSamples - samplePos);
        if (samplesThisBlock <= 0)
            break;

        if (! reader->read (&buffer, 0, samplesThisBlock, samplePos, true, true))
            break;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, samplesThisBlock));

        samplePos += samplesThisBlock;
    }

    return peak;
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
}

namespace waive
{

ToolDescription NormalizeSelectedClipsTool::describe() const
{
    ToolDescription desc;
    desc.name = "normalize_selected_clips";
    desc.displayName = "Normalize Selected Clips";
    desc.version = "1.0.0";
    desc.description = "Analyse selected audio clips and adjust clip gain to a target peak dB.";

    auto* schemaObj = new juce::DynamicObject();
    schemaObj->setProperty ("type", "object");

    auto* propsObj = new juce::DynamicObject();

    auto* targetObj = new juce::DynamicObject();
    targetObj->setProperty ("type", "number");
    targetObj->setProperty ("minimum", -24.0);
    targetObj->setProperty ("maximum", 0.0);
    targetObj->setProperty ("default", -1.0);
    targetObj->setProperty ("description", "Target peak level in dBFS");
    propsObj->setProperty ("target_peak_db", juce::var (targetObj));

    auto* delayObj = new juce::DynamicObject();
    delayObj->setProperty ("type", "integer");
    delayObj->setProperty ("minimum", 0);
    delayObj->setProperty ("default", 0);
    delayObj->setProperty ("description", "Extra analysis delay per clip for deterministic cancellation tests");
    propsObj->setProperty ("analysis_delay_ms", juce::var (delayObj));

    schemaObj->setProperty ("properties", juce::var (propsObj));
    desc.inputSchema = juce::var (schemaObj);

    auto* defaults = new juce::DynamicObject();
    defaults->setProperty ("target_peak_db", -1.0);
    defaults->setProperty ("analysis_delay_ms", 0);
    desc.defaultParams = juce::var (defaults);

    return desc;
}

juce::Result NormalizeSelectedClipsTool::preparePlan (const ToolExecutionContext& context,
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

        auto* audioClip = dynamic_cast<te::AudioClipBase*> (clip);
        auto* waveClip = dynamic_cast<te::WaveAudioClip*> (clip);

        if (audioClip == nullptr || waveClip == nullptr)
            continue;

        const auto sourceFile = waveClip->getSourceFileReference().getFile();
        if (! sourceFile.existsAsFile())
            continue;

        ClipPlanInput input;
        input.clipID = clip->itemID;
        input.clipName = clip->getName();
        input.sourceFile = sourceFile;
        input.trackIndex = findTrackIndexForClip (edit, *clip);
        input.currentGainDb = audioClip->getGainDB();

        if (input.trackIndex >= 0)
            clipsToProcess.push_back (std::move (input));
    }

    if (clipsToProcess.empty())
        return juce::Result::fail ("Selected clips are not analysable audio clips");

    const auto targetPeakDb = parseTargetPeakDb (params);
    const auto analysisDelayMs = parseAnalysisDelayMs (params);
    const auto cacheDirectory = context.projectCacheDirectory;
    const auto description = describe();

    outTask.jobName = "Plan: " + description.displayName;
    outTask.run = [clipsToProcess = std::move (clipsToProcess),
                   targetPeakDb, analysisDelayMs, cacheDirectory, params, description] (ProgressReporter& reporter)
    {
        ToolPlan plan;
        plan.toolName = description.name;
        plan.toolVersion = description.version;
        plan.planID = juce::Uuid().toString();
        plan.inputParams = params;

        const int total = (int) clipsToProcess.size();

        for (int i = 0; i < total; ++i)
        {
            if (reporter.isCancelled())
                return plan;

            const auto& clipInput = clipsToProcess[(size_t) i];
            sleepWithCancellation (reporter, analysisDelayMs);

            if (reporter.isCancelled())
                return plan;

            const auto peakGain = analysePeakGain (clipInput.sourceFile);
            if (peakGain <= 0.0f)
            {
                reporter.setProgress ((float) (i + 1) / (float) total,
                                      "Skipped silent clip: " + clipInput.clipName);
                continue;
            }

            const auto sourcePeakDb = juce::Decibels::gainToDecibels (peakGain, -120.0f);
            const auto gainDeltaDb = targetPeakDb - sourcePeakDb;
            const auto newGainDb = juce::jlimit (-60.0f, 24.0f, clipInput.currentGainDb + (float) gainDeltaDb);

            ToolDiffEntry change;
            change.kind = ToolDiffKind::parameterChanged;
            change.trackIndex = clipInput.trackIndex;
            change.clipID = clipInput.clipID;
            change.targetName = clipInput.clipName;
            change.parameterID = "clip.gain_db";
            change.beforeValue = clipInput.currentGainDb;
            change.afterValue = newGainDb;
            change.summary = "Set clip '" + clipInput.clipName + "' gain "
                             + juce::String (clipInput.currentGainDb, 2) + " dB -> "
                             + juce::String (newGainDb, 2) + " dB";

            plan.changes.add (change);

            reporter.setProgress ((float) (i + 1) / (float) total,
                                  "Analysed " + juce::String (i + 1) + " / " + juce::String (total));
        }

        plan.summary = "Normalize " + juce::String (plan.changes.size())
                       + " clip(s) to target peak " + juce::String (targetPeakDb, 1) + " dBFS";

        if (cacheDirectory != juce::File())
        {
            auto artifactDir = cacheDirectory.getChildFile ("tools").getChildFile (description.name);
            artifactDir.createDirectory();

            auto artifact = artifactDir.getChildFile ("plan_" + plan.planID + ".json");
            artifact.replaceWithText (juce::JSON::toString (toolPlanToJson (plan), true));
            plan.artifactFile = artifact;
        }

        return plan;
    };

    return juce::Result::ok();
}

juce::Result NormalizeSelectedClipsTool::apply (const ToolExecutionContext& context,
                                                const ToolPlan& plan)
{
    if (plan.toolName != describe().name)
        return juce::Result::fail ("Tool plan does not match normalize tool");

    if (plan.changes.isEmpty())
        return juce::Result::fail ("Tool plan has no changes to apply");

    int appliedCount = 0;
    const auto ok = context.editSession.performEdit ("Normalize Selected Clips", [&] (te::Edit& edit)
    {
        for (const auto& change : plan.changes)
        {
            if (change.kind != ToolDiffKind::parameterChanged || change.parameterID != "clip.gain_db")
                continue;

            auto* clip = te::findClipForID (edit, change.clipID);
            auto* audioClip = dynamic_cast<te::AudioClipBase*> (clip);
            if (audioClip == nullptr)
                continue;

            audioClip->setGainDB ((float) change.afterValue);
            ++appliedCount;
        }
    });

    if (! ok)
        return juce::Result::fail ("Failed to apply normalize changes");

    if (appliedCount <= 0)
        return juce::Result::fail ("No clips from the plan could be applied");

    return juce::Result::ok();
}

} // namespace waive
