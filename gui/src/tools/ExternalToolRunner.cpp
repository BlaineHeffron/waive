#include "ExternalToolRunner.h"
#include "JobQueue.h"

namespace waive
{

ExternalToolRunner::ExternalToolRunner()
{
    toolsDirs.push_back (getToolsDirectory());
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
    if (! tempDir.createDirectory())
    {
        output.message = "Failed to create temp directory";
        return output;
    }

    auto inputDir = tempDir.getChildFile ("input");
    auto outputDir = tempDir.getChildFile ("output");
    if (! inputDir.createDirectory() || ! outputDir.createDirectory())
    {
        output.message = "Failed to create input/output directories";
        return output;
    }

    // Write params.json
    auto paramsFile = inputDir.getChildFile ("params.json");
    if (! paramsFile.replaceWithText (juce::JSON::toString (params)))
    {
        output.message = "Failed to write params.json";
        return output;
    }

    // Copy input audio if needed
    if (inputAudioFile.existsAsFile() && manifest.acceptsAudioInput)
    {
        auto destInputFile = inputDir.getChildFile ("input.wav");
        if (! inputAudioFile.copyFileTo (destInputFile))
        {
            output.message = "Failed to copy input audio";
            return output;
        }
    }

    // Build command line
    juce::StringArray cmdLine;
    cmdLine.add (manifest.executable);
    cmdLine.addArray (manifest.arguments);
    cmdLine.add ("--input-dir");
    cmdLine.add (inputDir.getFullPathName());
    cmdLine.add ("--output-dir");
    cmdLine.add (outputDir.getFullPathName());

    // Launch child process
    juce::ChildProcess process;

    // Set working directory by spawning via shell (JUCE ChildProcess doesn't support cwd parameter)
    // We must use the manifest's base directory as the working directory
    auto savedCwd = juce::File::getCurrentWorkingDirectory();
    manifest.baseDirectory.setAsCurrentWorkingDirectory();

    bool started = process.start (cmdLine, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);

    // Restore original working directory
    savedCwd.setAsCurrentWorkingDirectory();

    if (! started)
    {
        output.message = "Failed to launch external tool: " + manifest.executable;
        return output;
    }

    // Poll for completion with timeout
    auto startTime = juce::Time::getMillisecondCounter();
    int exitCode = -1;
    bool processCompleted = false;

    while (true)
    {
        bool stillRunning = process.isRunning();

        if (! stillRunning)
        {
            exitCode = (int) process.getExitCode();
            processCompleted = true;
            break;
        }

        if (reporter.isCancelled())
        {
            process.kill();
            output.message = "External tool execution cancelled";
            return output;
        }

        auto elapsed = juce::Time::getMillisecondCounter() - startTime;
        // Use signed comparison to avoid overflow issues
        if ((int64) elapsed > (int64) manifest.timeoutMs)
        {
            process.kill();
            output.message = "External tool execution timed out after " + juce::String (manifest.timeoutMs) + "ms";
            return output;
        }

        juce::Thread::sleep (500);
    }

    if (! processCompleted)
    {
        output.message = "Process state unknown";
        return output;
    }

    if (exitCode != 0)
    {
        output.message = "External tool failed with exit code " + juce::String (exitCode);
        return output;
    }

    // Read result.json
    auto resultFile = outputDir.getChildFile ("result.json");
    if (resultFile.existsAsFile())
    {
        auto resultText = resultFile.loadFileAsString();
        output.resultData = juce::JSON::parse (resultText);

        // Extract message from result if present
        if (auto* resultObj = output.resultData.getDynamicObject())
        {
            if (resultObj->hasProperty ("message"))
                output.message = resultObj->getProperty ("message").toString();
        }
    }

    // Check for output audio
    auto outputAudioFile = outputDir.getChildFile ("output.wav");
    if (outputAudioFile.existsAsFile())
        output.outputAudioFile = outputAudioFile;

    output.success = true;
    if (output.message.isEmpty())
        output.message = "External tool executed successfully";

    return output;
}

} // namespace waive
