#include "StemSeparationTool.h"

#include <algorithm>
#include <map>
#include <tracktion_engine/tracktion_engine.h>

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
struct ClipPlanInput
{
    te::EditItemID clipID;
    juce::String clipName;
    juce::File sourceFile;
    int trackIndex = -1;
    double clipStartSeconds = 0.0;
    double clipEndSeconds = 0.0;
};

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

bool writeSeparatedStems (const juce::File& inputFile,
                          const juce::File& lowStemFile,
                          const juce::File& highStemFile,
                          waive::ProgressReporter& reporter)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (inputFile));
    if (reader == nullptr || reader->numChannels <= 0 || reader->lengthInSamples <= 0)
        return false;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::OutputStream> lowStream (lowStemFile.createOutputStream());
    std::unique_ptr<juce::OutputStream> highStream (highStemFile.createOutputStream());
    if (lowStream == nullptr || highStream == nullptr)
        return false;

    auto lowOptions = juce::AudioFormatWriterOptions()
                          .withSampleRate (reader->sampleRate)
                          .withNumChannels ((int) reader->numChannels)
                          .withBitsPerSample (16);

    auto highOptions = juce::AudioFormatWriterOptions()
                           .withSampleRate (reader->sampleRate)
                           .withNumChannels ((int) reader->numChannels)
                           .withBitsPerSample (16);

    auto lowWriter = wavFormat.createWriterFor (lowStream, lowOptions);
    auto highWriter = wavFormat.createWriterFor (highStream, highOptions);
    if (lowWriter == nullptr || highWriter == nullptr)
        return false;

    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> input ((int) reader->numChannels, blockSize);
    juce::AudioBuffer<float> low ((int) reader->numChannels, blockSize);
    juce::AudioBuffer<float> high ((int) reader->numChannels, blockSize);

    std::vector<float> lowState ((size_t) reader->numChannels, 0.0f);
    constexpr float smoothing = 0.03f;

    int64 samplePosition = 0;
    while (samplePosition < reader->lengthInSamples)
    {
        if (reporter.isCancelled())
            return false;

        const auto samplesThisBlock = (int) std::min<int64> ((int64) blockSize,
                                                             reader->lengthInSamples - samplePosition);
        if (! reader->read (&input, 0, samplesThisBlock, samplePosition, true, true))
            return false;

        for (int channel = 0; channel < input.getNumChannels(); ++channel)
        {
            auto* inData = input.getWritePointer (channel);
            auto* lowData = low.getWritePointer (channel);
            auto* highData = high.getWritePointer (channel);

            auto channelState = lowState[(size_t) channel];
            for (int sample = 0; sample < samplesThisBlock; ++sample)
            {
                const auto x = inData[sample];
                channelState += (x - channelState) * smoothing;
                lowData[sample] = channelState;
                highData[sample] = x - channelState;
            }

            lowState[(size_t) channel] = channelState;
        }

        if (! lowWriter->writeFromAudioSampleBuffer (low, 0, samplesThisBlock))
            return false;
        if (! highWriter->writeFromAudioSampleBuffer (high, 0, samplesThisBlock))
            return false;

        samplePosition += samplesThisBlock;
    }

    return true;
}

void writePlanArtifact (const juce::File& cacheDirectory,
                        const juce::String& toolName,
                        const juce::var& artifactPayload,
                        waive::ToolPlan& plan)
{
    if (cacheDirectory == juce::File())
        return;

    auto artifactDirectory = cacheDirectory.getChildFile ("tools")
                                           .getChildFile (toolName)
                                           .getChildFile ("plan_" + plan.planID);
    artifactDirectory.createDirectory();

    auto planFile = artifactDirectory.getChildFile ("plan.json");
    auto payloadFile = artifactDirectory.getChildFile ("payload.json");

    (void) planFile.replaceWithText (juce::JSON::toString (waive::toolPlanToJson (plan), true));
    (void) payloadFile.replaceWithText (juce::JSON::toString (artifactPayload, true));
    plan.artifactFile = payloadFile;
}

juce::String sanitiseStemBaseName (const juce::String& clipName)
{
    auto name = clipName.trim().replaceCharacter (' ', '_');
    name = name.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    if (name.isEmpty())
        name = "clip";
    return name;
}

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

ToolDescription StemSeparationTool::describe() const
{
    ToolDescription desc;
    desc.name = "stem_separation";
    desc.displayName = "Stem Separation (Model)";
    desc.version = "1.0.0";
    desc.description = "Generate low/high stems from selected clips. Requires installed stem-separator model.";

    auto* schemaObj = new juce::DynamicObject();
    schemaObj->setProperty ("type", "object");

    auto* propsObj = new juce::DynamicObject();

    auto* modelVersionObj = new juce::DynamicObject();
    modelVersionObj->setProperty ("type", "string");
    modelVersionObj->setProperty ("default", "");
    modelVersionObj->setProperty ("description", "Optional installed model version. Empty resolves pinned/latest.");
    propsObj->setProperty ("model_version", juce::var (modelVersionObj));

    auto* delayObj = new juce::DynamicObject();
    delayObj->setProperty ("type", "integer");
    delayObj->setProperty ("minimum", 0);
    delayObj->setProperty ("default", 0);
    delayObj->setProperty ("description", "Extra deterministic delay per clip for cancellation tests.");
    propsObj->setProperty ("analysis_delay_ms", juce::var (delayObj));

    schemaObj->setProperty ("properties", juce::var (propsObj));
    desc.inputSchema = juce::var (schemaObj);

    auto* defaults = new juce::DynamicObject();
    defaults->setProperty ("model_version", "");
    defaults->setProperty ("analysis_delay_ms", 0);
    desc.defaultParams = juce::var (defaults);

    desc.modelRequirement = "stem_separator";

    return desc;
}

juce::Result StemSeparationTool::preparePlan (const ToolExecutionContext& context,
                                              const juce::var& params,
                                              ToolPlanTask& outTask)
{
    const auto requestedModelVersion = parseRequestedModelVersion (params);
    auto resolvedModel = context.modelManager.resolveInstalledModel ("stem_separator", requestedModelVersion);
    if (! resolvedModel.has_value())
    {
        return juce::Result::fail ("Model 'stem_separator' is not installed. "
                                   "Install it via ModelManager before planning.");
    }

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
        return juce::Result::fail ("Selected clips are not analysable wave clips");

    const auto analysisDelayMs = parseAnalysisDelayMs (params);
    const auto cacheDirectory = context.projectCacheDirectory;
    const auto description = describe();
    const auto modelInfo = *resolvedModel;

    outTask.jobName = "Plan: " + description.displayName;
    outTask.run = [clipsToProcess = std::move (clipsToProcess),
                   analysisDelayMs,
                   cacheDirectory,
                   params,
                   description,
                   modelInfo] (ProgressReporter& reporter)
    {
        ToolPlan plan;
        plan.toolName = description.name;
        plan.toolVersion = description.version;
        plan.planID = juce::Uuid().toString();
        plan.inputParams = params;

        auto* payloadRoot = new juce::DynamicObject();
        auto* modelObj = new juce::DynamicObject();
        modelObj->setProperty ("model_id", modelInfo.modelID);
        modelObj->setProperty ("model_version", modelInfo.version);
        modelObj->setProperty ("model_path", modelInfo.installDirectory.getFullPathName());
        payloadRoot->setProperty ("model", juce::var (modelObj));

        juce::Array<juce::var> outputItems;
        const auto planOutputDirectory = cacheDirectory.getChildFile ("tools")
                                                     .getChildFile (description.name)
                                                     .getChildFile ("plan_" + plan.planID);
        planOutputDirectory.createDirectory();

        bool emittedTrackAdds = false;
        const int total = (int) clipsToProcess.size();
        for (int i = 0; i < total; ++i)
        {
            if (reporter.isCancelled())
                return plan;

            sleepWithCancellation (reporter, analysisDelayMs);
            if (reporter.isCancelled())
                return plan;

            const auto& clipInput = clipsToProcess[(size_t) i];
            const auto baseName = sanitiseStemBaseName (clipInput.clipName);
            auto lowStemFile = planOutputDirectory.getChildFile (baseName + "_low.wav");
            auto highStemFile = planOutputDirectory.getChildFile (baseName + "_high.wav");

            if (! writeSeparatedStems (clipInput.sourceFile, lowStemFile, highStemFile, reporter))
                continue;

            auto* outputObj = new juce::DynamicObject();
            outputObj->setProperty ("source_clip_id", (juce::int64) clipInput.clipID.getRawID());
            outputObj->setProperty ("source_track_index", clipInput.trackIndex);
            outputObj->setProperty ("source_clip_name", clipInput.clipName);
            outputObj->setProperty ("clip_start_seconds", clipInput.clipStartSeconds);
            outputObj->setProperty ("clip_end_seconds", clipInput.clipEndSeconds);

            juce::Array<juce::var> stemItems;
            auto* lowStemObj = new juce::DynamicObject();
            lowStemObj->setProperty ("stem_name", "low");
            lowStemObj->setProperty ("file", lowStemFile.getFullPathName());
            stemItems.add (juce::var (lowStemObj));

            auto* highStemObj = new juce::DynamicObject();
            highStemObj->setProperty ("stem_name", "high");
            highStemObj->setProperty ("file", highStemFile.getFullPathName());
            stemItems.add (juce::var (highStemObj));

            outputObj->setProperty ("stems", stemItems);
            outputItems.add (juce::var (outputObj));

            if (! emittedTrackAdds)
            {
                ToolDiffEntry lowTrack;
                lowTrack.kind = ToolDiffKind::trackAdded;
                lowTrack.parameterID = "track.name";
                lowTrack.targetName = "Stem Low";
                lowTrack.afterText = "Stem Low";
                lowTrack.summary = "Create destination track 'Stem Low'";
                plan.changes.add (lowTrack);

                ToolDiffEntry highTrack;
                highTrack.kind = ToolDiffKind::trackAdded;
                highTrack.parameterID = "track.name";
                highTrack.targetName = "Stem High";
                highTrack.afterText = "Stem High";
                highTrack.summary = "Create destination track 'Stem High'";
                plan.changes.add (highTrack);

                emittedTrackAdds = true;
            }

            ToolDiffEntry lowInsert;
            lowInsert.kind = ToolDiffKind::clipInserted;
            lowInsert.trackIndex = clipInput.trackIndex;
            lowInsert.clipID = clipInput.clipID;
            lowInsert.targetName = clipInput.clipName + " [low]";
            lowInsert.parameterID = "clip.file_path";
            lowInsert.beforeValue = clipInput.clipStartSeconds;
            lowInsert.afterValue = clipInput.clipEndSeconds;
            lowInsert.afterText = lowStemFile.getFullPathName();
            lowInsert.summary = "Insert low stem for clip '" + clipInput.clipName + "'";
            plan.changes.add (lowInsert);

            ToolDiffEntry highInsert;
            highInsert.kind = ToolDiffKind::clipInserted;
            highInsert.trackIndex = clipInput.trackIndex;
            highInsert.clipID = clipInput.clipID;
            highInsert.targetName = clipInput.clipName + " [high]";
            highInsert.parameterID = "clip.file_path";
            highInsert.beforeValue = clipInput.clipStartSeconds;
            highInsert.afterValue = clipInput.clipEndSeconds;
            highInsert.afterText = highStemFile.getFullPathName();
            highInsert.summary = "Insert high stem for clip '" + clipInput.clipName + "'";
            plan.changes.add (highInsert);

            reporter.setProgress ((float) (i + 1) / (float) total,
                                  "Separated clip " + juce::String (i + 1) + " / "
                                  + juce::String (total));
        }

        payloadRoot->setProperty ("outputs", outputItems);
        plan.summary = "Generate stems for " + juce::String (outputItems.size())
                       + " clip(s) using model " + modelInfo.version;

        writePlanArtifact (cacheDirectory, description.name, juce::var (payloadRoot), plan);
        return plan;
    };

    return juce::Result::ok();
}

juce::Result StemSeparationTool::apply (const ToolExecutionContext& context,
                                        const ToolPlan& plan)
{
    if (plan.toolName != describe().name)
        return juce::Result::fail ("Tool plan does not match stem separation tool");

    if (plan.artifactFile == juce::File() || ! plan.artifactFile.existsAsFile())
        return juce::Result::fail ("Stem plan artifact payload is missing");

    const auto parsedPayload = juce::JSON::parse (plan.artifactFile);
    auto* payloadObj = parsedPayload.getDynamicObject();
    if (payloadObj == nullptr || ! payloadObj->hasProperty ("outputs"))
        return juce::Result::fail ("Stem plan artifact payload is invalid");

    const auto outputArray = *payloadObj->getProperty ("outputs").getArray();
    if (outputArray.isEmpty())
        return juce::Result::fail ("Stem plan contains no generated outputs");

    int appliedCount = 0;
    const auto ok = context.editSession.performEdit ("Stem Separation", [&] (te::Edit& edit)
    {
        auto* lowTrack = findOrCreateTrackByName (edit, "Stem Low");
        auto* highTrack = findOrCreateTrackByName (edit, "Stem High");
        if (lowTrack == nullptr || highTrack == nullptr)
            return;

        for (const auto& outputVar : outputArray)
        {
            auto* outputObj = outputVar.getDynamicObject();
            if (outputObj == nullptr)
                continue;

            const auto clipName = outputObj->getProperty ("source_clip_name").toString();
            const auto clipStartSeconds = (double) outputObj->getProperty ("clip_start_seconds");
            const auto clipEndSeconds = (double) outputObj->getProperty ("clip_end_seconds");
            const auto stemsVar = outputObj->getProperty ("stems");
            auto* stemsArray = stemsVar.getArray();
            if (stemsArray == nullptr)
                continue;

            for (const auto& stemVar : *stemsArray)
            {
                auto* stemObj = stemVar.getDynamicObject();
                if (stemObj == nullptr)
                    continue;

                const auto stemName = stemObj->getProperty ("stem_name").toString();
                const auto stemFile = juce::File (stemObj->getProperty ("file").toString());
                if (! stemFile.existsAsFile())
                    continue;

                auto* destinationTrack = stemName == "low" ? lowTrack : highTrack;
                if (destinationTrack == nullptr)
                    continue;

                auto insertedClip = destinationTrack->insertWaveClip (
                    clipName + " [" + stemName + "]",
                    stemFile,
                    { { te::TimePosition::fromSeconds (clipStartSeconds),
                        te::TimePosition::fromSeconds (clipEndSeconds) },
                      te::TimeDuration() },
                    false);

                if (insertedClip != nullptr)
                    ++appliedCount;
            }
        }
    });

    if (! ok)
        return juce::Result::fail ("Failed to apply stem-separation changes");

    if (appliedCount <= 0)
        return juce::Result::fail ("No stem clips from the plan could be inserted");

    return juce::Result::ok();
}

} // namespace waive
