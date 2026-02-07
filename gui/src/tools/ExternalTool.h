#pragma once

#include "Tool.h"
#include "ExternalToolManifest.h"

namespace waive
{

class ExternalToolRunner;

/** Adapts an ExternalToolManifest into the waive::Tool interface. */
class ExternalTool : public Tool
{
public:
    ExternalTool (const ExternalToolManifest& manifest, ExternalToolRunner& runner);

    ToolDescription describe() const override;
    juce::Result preparePlan (const ToolExecutionContext& context,
                              const juce::var& params,
                              ToolPlanTask& outTask) override;
    juce::Result apply (const ToolExecutionContext& context,
                        const ToolPlan& plan) override;

private:
    ExternalToolManifest manifest;
    ExternalToolRunner& runner;
};

} // namespace waive
