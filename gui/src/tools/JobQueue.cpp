#include "JobQueue.h"

namespace waive
{

//==============================================================================
ProgressReporter::ProgressReporter (int id, std::atomic<bool>& cancel,
                                    std::function<void (int, float, const juce::String&)> cb)
    : jobId (id), cancelFlag (cancel), progressCallback (std::move (cb))
{
}

void ProgressReporter::setProgress (float progress, const juce::String& message)
{
    if (progressCallback)
        progressCallback (jobId, progress, message);
}

bool ProgressReporter::isCancelled() const
{
    return cancelFlag.load();
}

//==============================================================================
JobQueue::JobQueue (int numThreads)
    : threadPool (numThreads)
{
    startTimerHz (10);
}

JobQueue::~JobQueue()
{
    stopTimer();
    cancelAll();
    threadPool.removeAllJobs (true, 5000);
}

int JobQueue::submit (const JobDescriptor& descriptor, JobFunction jobFunction,
                      CompletionCallback onComplete)
{
    auto info = std::make_shared<JobInfo>();
    int id;

    {
        std::lock_guard<std::mutex> lock (jobsMutex);
        id = nextJobId++;
        info->id = id;
        info->descriptor = descriptor;
        info->onComplete = std::move (onComplete);
        jobs.push_back (info);
    }

    // Fire pending event
    notifyListeners ({ id, descriptor, JobStatus::Pending, 0.0f, "Queued" });

    auto capturedInfo = info;
    auto capturedFn = std::move (jobFunction);

    threadPool.addJob ([this, capturedInfo, capturedFn]
    {
        capturedInfo->status.store (static_cast<int> (JobStatus::Running));
        capturedInfo->hasUpdate.store (true);

        ProgressReporter reporter (
            capturedInfo->id,
            capturedInfo->cancelFlag,
            [capturedInfo] (int, float progress, const juce::String& message)
            {
                capturedInfo->lastProgress.store (progress);
                {
                    std::lock_guard<std::mutex> messageLock (capturedInfo->messageMutex);
                    capturedInfo->lastMessage = message;
                }
                capturedInfo->hasUpdate.store (true);
            });

        JobStatus finalStatus = JobStatus::Completed;

        try
        {
            capturedFn (reporter);

            if (capturedInfo->cancelFlag.load())
                finalStatus = JobStatus::Cancelled;
        }
        catch (const std::exception& e)
        {
            std::lock_guard<std::mutex> messageLock (capturedInfo->messageMutex);
            capturedInfo->lastMessage = e.what();
            finalStatus = JobStatus::Failed;
        }
        catch (...)
        {
            std::lock_guard<std::mutex> messageLock (capturedInfo->messageMutex);
            capturedInfo->lastMessage = "Unknown exception";
            finalStatus = JobStatus::Failed;
        }

        capturedInfo->status.store (static_cast<int> (finalStatus));
        capturedInfo->lastProgress.store (finalStatus == JobStatus::Completed ? 1.0f
                                                                              : capturedInfo->lastProgress.load());
        capturedInfo->hasUpdate.store (true);
    });

    return id;
}

void JobQueue::cancelJob (int jobId)
{
    std::lock_guard<std::mutex> lock (jobsMutex);
    for (auto& info : jobs)
    {
        if (info->id == jobId)
        {
            info->cancelFlag.store (true);
            break;
        }
    }
}

void JobQueue::cancelAll()
{
    std::lock_guard<std::mutex> lock (jobsMutex);
    for (auto& info : jobs)
        info->cancelFlag.store (true);
}

void JobQueue::addListener (Listener* listener)
{
    listeners.add (listener);
}

void JobQueue::removeListener (Listener* listener)
{
    listeners.remove (listener);
}

void JobQueue::timerCallback()
{
    // Coalesce progress updates — deliver on message thread at ~10Hz.
    std::vector<std::shared_ptr<JobInfo>> completedJobs;
    std::vector<std::shared_ptr<JobInfo>> updatedJobs;

    {
        std::lock_guard<std::mutex> lock (jobsMutex);

        // Collect jobs with updates and move completed jobs to local list
        for (auto& info : jobs)
        {
            if (! info->hasUpdate.exchange (false))
                continue;

            auto status = static_cast<JobStatus> (info->status.load());
            if (status == JobStatus::Completed || status == JobStatus::Failed || status == JobStatus::Cancelled)
                completedJobs.push_back (info);
            else
                updatedJobs.push_back (info);
        }

        // Remove completed jobs from active list while holding lock
        if (! completedJobs.empty())
        {
            jobs.erase (std::remove_if (jobs.begin(), jobs.end(),
                                        [&completedJobs] (const auto& j)
                                        {
                                            return std::find (completedJobs.begin(), completedJobs.end(), j) != completedJobs.end();
                                        }),
                        jobs.end());
        }
    }

    // Fire events for updated jobs
    for (auto& info : updatedJobs)
    {
        JobEvent event;
        event.jobId = info->id;
        event.descriptor = info->descriptor;
        event.status = static_cast<JobStatus> (info->status.load());
        event.progress = info->lastProgress.load();
        {
            std::lock_guard<std::mutex> messageLock (info->messageMutex);
            event.message = info->lastMessage;
        }

        notifyListeners (event);
    }

    // Fire completion events and callbacks
    for (auto& info : completedJobs)
    {
        auto status = static_cast<JobStatus> (info->status.load());

        JobEvent event;
        event.jobId = info->id;
        event.descriptor = info->descriptor;
        event.status = status;
        event.progress = info->lastProgress.load();
        {
            std::lock_guard<std::mutex> messageLock (info->messageMutex);
            event.message = info->lastMessage;
        }

        notifyListeners (event);

        if (info->onComplete)
            info->onComplete (info->id, status);
    }
}

void JobQueue::notifyListeners (const JobEvent& event)
{
    listeners.call ([&] (Listener& l) { l.jobEvent (event); });
}

} // namespace waive
