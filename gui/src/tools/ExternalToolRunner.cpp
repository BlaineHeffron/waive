#include "ExternalToolRunner.h"
#include "JobQueue.h"

namespace waive
{

namespace
{
juce::String drainChildProcessOutput (juce::ChildProcess& process)
{
    juce::MemoryOutputStream stream;
    char buffer[4096];
    for (;;)
    {
        const auto bytesRead = process.readProcessOutput (buffer, sizeof (buffer));
        if (bytesRead <= 0)
            break;

        stream.write (buffer, (size_t) bytesRead);
    }

    return stream.toString();
}

juce::var parseToolResultPayload (const juce::String& text)
{
    const auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return {};

    auto parsed = juce::JSON::parse (trimmed);
    if (parsed.isVoid())
    {
        const auto lastLine = trimmed.fromLastOccurrenceOf ("\n", false, false).trim();
        if (lastLine != trimmed)
            parsed = juce::JSON::parse (lastLine);
    }

    return parsed;
}

juce::String extractToolMessage (const juce::var& resultData)
{
    if (auto* resultObj = resultData.getDynamicObject())
    {
        if (resultObj->hasProperty ("success"))
        {
            const auto succeeded = static_cast<bool> (resultObj->getProperty ("success"));
            if (! succeeded && resultObj->hasProperty ("message"))
                return resultObj->getProperty ("message").toString();
        }

        if (resultObj->hasProperty ("message"))
            return resultObj->getProperty ("message").toString();

        if (resultObj->hasProperty ("status"))
        {
            auto status = resultObj->getProperty ("status").toString();
            if (status.isNotEmpty())
                return status;
        }
    }

    return {};
}

bool toolReportedSuccess (const juce::var& resultData)
{
    if (auto* resultObj = resultData.getDynamicObject())
    {
        if (resultObj->hasProperty ("success"))
            return static_cast<bool> (resultObj->getProperty ("success"));

        if (resultObj->hasProperty ("status"))
            return resultObj->getProperty ("status").toString().equalsIgnoreCase ("ok");

        return true;
    }

    return false;
}

juce::File findFirstExistingFile (const juce::Array<juce::var>* files, const juce::String& preferredExtension)
{
    if (files == nullptr)
        return {};

    for (const auto& value : *files)
    {
        auto candidate = juce::File (value.toString());
        if (candidate.existsAsFile() && candidate.hasFileExtension (preferredExtension))
            return candidate;
    }

    for (const auto& value : *files)
    {
        auto candidate = juce::File (value.toString());
        if (candidate.existsAsFile())
            return candidate;
    }

    return {};
}

void populateOutputPathsFromResult (ExternalToolOutput& output, const juce::File& outputDir)
{
    if (auto* resultObj = output.resultData.getDynamicObject())
    {
        if (output.message.isEmpty())
            output.message = extractToolMessage (output.resultData);

        if (resultObj->hasProperty ("output_files"))
        {
            if (auto outputFile = findFirstExistingFile (resultObj->getProperty ("output_files").getArray(), ".wav");
                outputFile != juce::File())
            {
                output.outputAudioFile = outputFile;
                return;
            }
        }
    }

    auto outputAudioFile = outputDir.getChildFile ("output.wav");
    if (outputAudioFile.existsAsFile())
        output.outputAudioFile = outputAudioFile;
}
}

ExternalToolRunner::ExternalToolRunner()
{
    toolsDirs.push_back (getToolsDirectory());
}

juce::String ExternalToolRunner::resolveManifestArgument (const ExternalToolManifest& manifest,
                                                          const juce::String& argument) const
{
    if (argument.isEmpty() || juce::File::isAbsolutePath (argument))
        return argument;

    return manifest.baseDirectory.getChildFile (argument).getFullPathName();
}

juce::File ExternalToolRunner::getToolsDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Waive")
               .getChildFile ("tools");
}

void ExternalToolRunner::addToolsDirectory (const juce::File& dir)
{
    // Check for duplicates
    for (const auto& existing : toolsDirs)
    {
        if (existing.getFullPathName() == dir.getFullPathName())
            return;  // Already exists, skip
    }
    toolsDirs.push_back (dir);
}

ExternalToolOutput ExternalToolRunner::run (const ExternalToolManifest& manifest,
                                            const juce::var& params,
                                            const juce::File& inputAudioFile,
                                            ProgressReporter& reporter)
{
    ExternalToolOutput output;

    // Create unique temp directory
    auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("waive_tool_" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    output.temporaryDirectory = tempDir;

    const auto cleanupTempDirectory = [&output]
    {
        if (output.temporaryDirectory != juce::File() && output.temporaryDirectory.exists())
            (void) output.temporaryDirectory.deleteRecursively();

        output.temporaryDirectory = juce::File();
    };

    if (! tempDir.createDirectory())
    {
        output.message = "Failed to create temp directory";
        cleanupTempDirectory();
        return output;
    }

    auto inputDir = tempDir.getChildFile ("input");
    auto outputDir = tempDir.getChildFile ("output");
    if (! inputDir.createDirectory() || ! outputDir.createDirectory())
    {
        output.message = "Failed to create input/output directories";
        cleanupTempDirectory();
        return output;
    }

    // Write params.json
    auto paramsFile = inputDir.getChildFile ("params.json");
    if (! paramsFile.replaceWithText (juce::JSON::toString (params)))
    {
        output.message = "Failed to write params.json";
        cleanupTempDirectory();
        return output;
    }

    // Copy input audio if needed
    if (inputAudioFile.existsAsFile() && manifest.acceptsAudioInput)
    {
        auto destInputFile = inputDir.getChildFile ("input.wav");
        if (! inputAudioFile.copyFileTo (destInputFile))
        {
            output.message = "Failed to copy input audio";
            cleanupTempDirectory();
            return output;
        }
    }

    // Build command line
    juce::StringArray cmdLine;
    cmdLine.add (manifest.executable);
    for (const auto& argument : manifest.arguments)
        cmdLine.add (resolveManifestArgument (manifest, argument));
    cmdLine.add ("--input-dir");
    cmdLine.add (inputDir.getFullPathName());
    cmdLine.add ("--output-dir");
    cmdLine.add (outputDir.getFullPathName());

    // Launch child process
    juce::ChildProcess process;
    bool started = process.start (cmdLine, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);

    if (! started)
    {
        output.message = "Failed to launch external tool: " + manifest.executable;
        cleanupTempDirectory();
        return output;
    }

    // Poll for completion with timeout
    auto startTime = juce::Time::getMillisecondCounter();
    int exitCode = -1;
    bool processCompleted = false;

    while (true)
    {
        output.stdOut << drainChildProcessOutput (process);

        bool stillRunning = process.isRunning();

        if (! stillRunning)
        {
            output.stdOut << drainChildProcessOutput (process);
            exitCode = (int) process.getExitCode();
            processCompleted = true;
            break;
        }

        if (reporter.isCancelled())
        {
            process.kill();
            output.message = "External tool execution cancelled";
            cleanupTempDirectory();
            return output;
        }

        auto elapsed = juce::Time::getMillisecondCounter() - startTime;
        // Use signed comparison to avoid overflow issues
        if ((int64) elapsed > (int64) manifest.timeoutMs)
        {
            process.kill();
            output.message = "External tool execution timed out after " + juce::String (manifest.timeoutMs) + "ms";
            cleanupTempDirectory();
            return output;
        }

        juce::Thread::sleep (500);
    }

    if (! processCompleted)
    {
        output.message = "Process state unknown";
        cleanupTempDirectory();
        return output;
    }

    if (exitCode != 0)
    {
        auto failureOutput = output.stdOut.trim();
        if (failureOutput.isEmpty())
            failureOutput = process.readAllProcessOutput().trim();

        output.stdErr = failureOutput;
        output.message = "External tool failed with exit code " + juce::String (exitCode);
        if (failureOutput.isNotEmpty())
            output.message << ": " << failureOutput;
        cleanupTempDirectory();
        return output;
    }

    // Read result.json
    auto resultFile = outputDir.getChildFile ("result.json");
    if (resultFile.existsAsFile())
    {
        auto resultText = resultFile.loadFileAsString();
        output.resultData = juce::JSON::parse (resultText);
    }
    else
    {
        output.resultData = parseToolResultPayload (output.stdOut);
    }

    populateOutputPathsFromResult (output, outputDir);
    output.success = toolReportedSuccess (output.resultData)
                     || output.outputAudioFile.existsAsFile();
    if (output.message.isEmpty() && ! output.success)
        output.message = output.resultData.isObject()
                           ? "External tool reported a failure"
                           : "External tool did not return a valid result";
    else if (output.message.isEmpty())
        output.message = "External tool executed successfully";

    return output;
}

} // namespace waive
