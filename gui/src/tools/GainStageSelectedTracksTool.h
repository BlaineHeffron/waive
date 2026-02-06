#pragma once

#include "Tool.h"

namespace waive
{

class GainStageSelectedTracksTool : public Tool
{
public:
    ToolDescription describe() const override;
    juce::Result preparePlan (const ToolExecutionContext& context,
                              const juce::var& params,
                              ToolPlanTask& outTask) override;
    juce::Result apply (const ToolExecutionContext& context,
                        const ToolPlan& plan) override;
};

} // namespace waive
