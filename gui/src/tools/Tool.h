#pragma once

#include <JuceHeader.h>
#include <functional>

#include "ToolDiff.h"

class EditSession;
class ProjectManager;
class SessionComponent;
namespace waive { class ModelManager; }

namespace waive
{

class ProgressReporter;

struct ToolDescription
{
    juce::String name;
    juce::String displayName;
    juce::String version;
    juce::String description;
    juce::var inputSchema;
    juce::var defaultParams;
    juce::String modelRequirement;
};

struct ToolExecutionContext
{
    EditSession& editSession;
    ProjectManager& projectManager;
    SessionComponent& sessionComponent;
    ModelManager& modelManager;
    juce::File projectCacheDirectory;
};

struct ToolPlanTask
{
    juce::String jobName;
    std::function<ToolPlan (ProgressReporter&)> run;
};

class Tool
{
public:
    virtual ~Tool() = default;

    virtual ToolDescription describe() const = 0;
    virtual juce::Result preparePlan (const ToolExecutionContext& context,
                                      const juce::var& params,
                                      ToolPlanTask& outTask) = 0;
    virtual juce::Result apply (const ToolExecutionContext& context,
                                const ToolPlan& plan) = 0;
};

} // namespace waive
