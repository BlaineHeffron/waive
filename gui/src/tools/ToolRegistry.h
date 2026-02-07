#pragma once

#include <memory>
#include <vector>

#include "Tool.h"

namespace waive
{

class ExternalToolRunner;

class ToolRegistry
{
public:
    ToolRegistry();

    void registerTool (std::unique_ptr<Tool> tool);

    const std::vector<std::unique_ptr<Tool>>& getTools() const  { return tools; }

    Tool* findTool (const juce::String& name);
    const Tool* findTool (const juce::String& name) const;

    /** Scan directories and register any external tools found. */
    void scanAndRegisterExternalTools (ExternalToolRunner& runner);

private:
    std::vector<std::unique_ptr<Tool>> tools;
};

} // namespace waive

