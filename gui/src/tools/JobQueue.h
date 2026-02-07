#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace waive
{

//==============================================================================
struct JobDescriptor
{
    juce::String name;
    juce::String category;
};

enum class JobStatus
{
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled
};

struct JobEvent
{
    int jobId = 0;
    JobDescriptor descriptor;
    JobStatus status = JobStatus::Pending;
    float progress = 0.0f;
    juce::String message;
};

//==============================================================================
/** Passed to job functions so they can report progress and check for cancellation. */
class ProgressReporter
{
public:
    ProgressReporter (int jobId, std::atomic<bool>& cancelFlag,
                      std::function<void (int, float, const juce::String&)> progressCallback);

    void setProgress (float progress, const juce::String& message = {});
    bool isCancelled() const;

private:
    int jobId;
    std::atomic<bool>& cancelFlag;
    std::function<void (int, float, const juce::String&)> progressCallback;
};

//==============================================================================
/** App-wide background job system wrapping juce::ThreadPool. */
class JobQueue : private juce::Timer
{
public:
    explicit JobQueue (int numThreads = 2);
    ~JobQueue() override;

    using JobFunction = std::function<void (ProgressReporter&)>;
    using CompletionCallback = std::function<void (int jobId, JobStatus status)>;

    /** Submit a background job. Returns a job ID. onComplete is called on the message thread. */
    int submit (const JobDescriptor& descriptor, JobFunction jobFunction,
                CompletionCallback onComplete = nullptr);

    void cancelJob (int jobId);
    void cancelAll();

    //==============================================================================
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void jobEvent (const JobEvent& event) = 0;
    };

    void addListener (Listener* listener);
    void removeListener (Listener* listener);

private:
    void timerCallback() override;
    void notifyListeners (const JobEvent& event);

    struct JobInfo
    {
        int id = 0;
        JobDescriptor descriptor;
        std::atomic<bool> cancelFlag { false };
        std::atomic<float> lastProgress { 0.0f };
        juce::String lastMessage;
        std::mutex messageMutex;
        std::atomic<bool> hasUpdate { false };
        std::atomic<int> status { static_cast<int> (JobStatus::Pending) };
        CompletionCallback onComplete;
    };

    juce::ThreadPool threadPool;
    std::mutex jobsMutex;
    std::unordered_map<int, std::shared_ptr<JobInfo>> jobs;
    int64_t nextJobId = 1;

    juce::ListenerList<Listener> listeners;
};

} // namespace waive
