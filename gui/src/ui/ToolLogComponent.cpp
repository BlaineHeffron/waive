#include "ToolLogComponent.h"
#include "JobQueue.h"
#include "WaiveSpacing.h"
#include "WaiveLookAndFeel.h"

//==============================================================================
ToolLogComponent::ToolLogComponent (waive::JobQueue& queue)
    : jobQueue (queue)
{
    demoButton.setButtonText ("Run Demo");
    clearButton.setButtonText ("Clear");

    activeJobsLabel.setText ("Active Jobs", juce::dontSendNotification);
    activeJobsLabel.setJustificationType (juce::Justification::centredLeft);

    logEditor.setMultiLine (true);
    logEditor.setReadOnly (true);
    logEditor.setReturnKeyStartsNewLine (true);

    addAndMakeVisible (demoButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (activeJobsLabel);
    addAndMakeVisible (activeJobsArea);
    addAndMakeVisible (logEditor);

    demoButton.onClick = [this]
    {
        waive::JobDescriptor desc { "Demo Job", "demo" };
        jobQueue.submit (desc, [] (waive::ProgressReporter& reporter)
        {
            for (int i = 0; i < 10; ++i)
            {
                if (reporter.isCancelled())
                    return;

                juce::Thread::sleep (200);
                reporter.setProgress ((i + 1) / 10.0f,
                                      "Step " + juce::String (i + 1) + " / 10");
            }
        });
    };

    clearButton.onClick = [this]
    {
        logEditor.clear();
    };

    jobQueue.addListener (this);
}

ToolLogComponent::~ToolLogComponent()
{
    jobQueue.removeListener (this);
}

void ToolLogComponent::resized()
{
    auto bounds = getLocalBounds().reduced (waive::Spacing::md);

    auto buttonRow = bounds.removeFromTop (waive::Spacing::controlHeightLarge);
    demoButton.setBounds (buttonRow.removeFromLeft (100));
    buttonRow.removeFromLeft (waive::Spacing::sm);
    clearButton.setBounds (buttonRow.removeFromLeft (80));

    bounds.removeFromTop (waive::Spacing::sm);
    activeJobsLabel.setBounds (bounds.removeFromTop (waive::Spacing::xl));
    bounds.removeFromTop (waive::Spacing::xs);

    // Active jobs area — height depends on number of active jobs
    int activeHeight = juce::jmax (0, (int) activeJobs.size() * waive::Spacing::controlHeightLarge);
    activeJobsArea.setBounds (bounds.removeFromTop (activeHeight));

    bounds.removeFromTop (waive::Spacing::sm);
    logEditor.setBounds (bounds);
}

void ToolLogComponent::jobEvent (const waive::JobEvent& event)
{
    auto statusStr = [&]() -> juce::String
    {
        switch (event.status)
        {
            case waive::JobStatus::Pending:    return "PENDING";
            case waive::JobStatus::Running:    return "RUNNING";
            case waive::JobStatus::Completed:  return "COMPLETED";
            case waive::JobStatus::Failed:     return "FAILED";
            case waive::JobStatus::Cancelled:  return "CANCELLED";
        }
        return "UNKNOWN";
    }();

    auto timestamp = juce::Time::getCurrentTime().toString (false, true, true, true);

    if (event.status == waive::JobStatus::Running)
    {
        // Update or add to active jobs
        bool found = false;
        for (auto& aj : activeJobs)
        {
            if (aj.jobId == event.jobId)
            {
                aj.progress = event.progress;
                aj.message = event.message;
                found = true;
                break;
            }
        }
        if (! found)
        {
            activeJobs.push_back ({ event.jobId, event.descriptor.name,
                                    event.progress, event.message });
            appendLog ("[" + timestamp + "] " + event.descriptor.name
                       + " started\n");
        }
    }
    else if (event.status == waive::JobStatus::Completed
             || event.status == waive::JobStatus::Failed
             || event.status == waive::JobStatus::Cancelled)
    {
        // Remove from active
        activeJobs.erase (
            std::remove_if (activeJobs.begin(), activeJobs.end(),
                            [&] (auto& aj) { return aj.jobId == event.jobId; }),
            activeJobs.end());

        juce::String logEntry = "[" + timestamp + "] " + event.descriptor.name + " " + statusStr;
        if (event.message.isNotEmpty())
            logEntry += " - " + event.message;
        logEntry += "\n";

        if (event.status == waive::JobStatus::Failed)
            appendErrorLog (logEntry);
        else
            appendLog (logEntry);
    }

    updateActiveJobs();
}

void ToolLogComponent::appendLog (const juce::String& text)
{
    logEditor.moveCaretToEnd();
    logEditor.insertTextAtCaret (text);
    logEditor.moveCaretToEnd();
}

void ToolLogComponent::appendErrorLog (const juce::String& text)
{
    // Insert error text with visual distinction
    // Note: TextEditor doesn't support colored text directly, but the text will be distinct
    // when read. For now, we prefix with ERROR marker for visual scanning.
    juce::String errorText = "ERROR: " + text;
    logEditor.moveCaretToEnd();
    logEditor.insertTextAtCaret (errorText);
    logEditor.moveCaretToEnd();
}

void ToolLogComponent::updateActiveJobs()
{
    // Rebuild active jobs UI
    progressBars.clear();
    progressLabels.clear();
    cancelButtons.clear();
    progressValues.clear();
    activeJobsArea.removeAllChildren();

    progressValues.resize (activeJobs.size());

    for (size_t i = 0; i < activeJobs.size(); ++i)
    {
        auto& aj = activeJobs[i];
        progressValues[i] = aj.progress;

        auto label = std::make_unique<juce::Label>();
        auto labelText = aj.name;
        if (aj.message.isNotEmpty())
            labelText += " - " + aj.message;
        label->setText (labelText, juce::dontSendNotification);
        label->setJustificationType (juce::Justification::centredLeft);

        auto bar = std::make_unique<juce::ProgressBar> (progressValues[i]);

        auto cancel = std::make_unique<juce::TextButton> ("Cancel");
        int jobId = aj.jobId;
        cancel->onClick = [this, jobId] { jobQueue.cancelJob (jobId); };

        activeJobsArea.addAndMakeVisible (label.get());
        activeJobsArea.addAndMakeVisible (bar.get());
        activeJobsArea.addAndMakeVisible (cancel.get());

        progressLabels.push_back (std::move (label));
        progressBars.push_back (std::move (bar));
        cancelButtons.push_back (std::move (cancel));
    }

    // Layout active jobs within the area
    int y = 0;
    int areaWidth = activeJobsArea.getWidth();
    if (areaWidth <= 0)
        areaWidth = getWidth() - waive::Spacing::xl;

    for (size_t i = 0; i < activeJobs.size(); ++i)
    {
        auto row = juce::Rectangle<int> (0, y, areaWidth, waive::Spacing::controlHeightDefault);
        progressLabels[i]->setBounds (row.removeFromLeft (200));
        row.removeFromLeft (waive::Spacing::sm);
        cancelButtons[i]->setBounds (row.removeFromRight (70));
        row.removeFromRight (waive::Spacing::sm);
        progressBars[i]->setBounds (row);
        y += waive::Spacing::controlHeightLarge;
    }

    activeJobsArea.setSize (areaWidth, y);
    resized();
}
