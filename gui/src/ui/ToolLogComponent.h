#pragma once

#include <JuceHeader.h>
#include "JobQueue.h"

namespace waive { class JobQueue; }

//==============================================================================
/** Log panel that subscribes to JobQueue events and shows active jobs + history. */
class ToolLogComponent : public juce::Component,
                         public waive::JobQueue::Listener
{
public:
    explicit ToolLogComponent (waive::JobQueue& queue);
    ~ToolLogComponent() override;

    void resized() override;

    // waive::JobQueue::Listener
    void jobEvent (const waive::JobEvent& event) override;

private:
    void appendLog (const juce::String& text);
    void appendErrorLog (const juce::String& text);
    void rebuildActiveJobRows();
    void updateActiveJobRow (size_t index);
    void layoutActiveJobRows();

    waive::JobQueue& jobQueue;

    // Active job tracking
    struct ActiveJob
    {
        int jobId = 0;
        juce::String name;
        float progress = 0.0f;
        juce::String message;
    };
    std::vector<ActiveJob> activeJobs;

    struct ActiveJobRow
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::ProgressBar> progressBar;
        std::unique_ptr<juce::TextButton> cancelButton;
        double progressValue = 0.0;
    };

    // UI
    juce::TextButton demoButton;
    juce::TextButton clearButton;
    juce::Label activeJobsLabel;
    std::vector<ActiveJobRow> activeJobRows;
    juce::Component activeJobsArea;
    juce::TextEditor logEditor;
};
