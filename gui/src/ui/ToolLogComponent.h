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
    void updateActiveJobs();

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

    // UI
    juce::TextButton demoButton;
    juce::TextButton clearButton;
    juce::Label activeJobsLabel;
    std::vector<std::unique_ptr<juce::ProgressBar>> progressBars;
    std::vector<std::unique_ptr<juce::Label>> progressLabels;
    std::vector<std::unique_ptr<juce::TextButton>> cancelButtons;
    std::vector<double> progressValues; // ProgressBar needs double&
    juce::Component activeJobsArea;
    juce::TextEditor logEditor;
};
