#include "ToolRegistry.h"
#include "AlignClipsByTransientTool.h"
#include "AutoMixSuggestionsTool.h"
#include "DetectSilenceAndCutRegionsTool.h"
#include "GainStageSelectedTracksTool.h"
#include "NormalizeSelectedClipsTool.h"
#include "RenameTracksFromClipsTool.h"
#include "StemSeparationTool.h"

namespace waive
{

ToolRegistry::ToolRegistry()
{
    registerTool (std::make_unique<NormalizeSelectedClipsTool>());
    registerTool (std::make_unique<RenameTracksFromClipsTool>());
    registerTool (std::make_unique<GainStageSelectedTracksTool>());
    registerTool (std::make_unique<DetectSilenceAndCutRegionsTool>());
    registerTool (std::make_unique<AlignClipsByTransientTool>());
    registerTool (std::make_unique<StemSeparationTool>());
    registerTool (std::make_unique<AutoMixSuggestionsTool>());
}

void ToolRegistry::registerTool (std::unique_ptr<Tool> tool)
{
    if (tool == nullptr)
        return;

    tools.push_back (std::move (tool));
}

Tool* ToolRegistry::findTool (const juce::String& name)
{
    for (auto& tool : tools)
        if (tool != nullptr && tool->describe().name == name)
            return tool.get();

    return nullptr;
}

const Tool* ToolRegistry::findTool (const juce::String& name) const
{
    for (const auto& tool : tools)
        if (tool != nullptr && tool->describe().name == name)
            return tool.get();

    return nullptr;
}

} // namespace waive
