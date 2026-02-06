#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace waive
{

enum class ToolDiffKind
{
    trackAdded,
    trackRemoved,
    trackRenamed,
    clipInserted,
    clipMoved,
    clipTrimmed,
    clipFadeChanged,
    parameterChanged,
    automationChanged
};

struct ToolDiffEntry
{
    ToolDiffKind kind = ToolDiffKind::parameterChanged;
    juce::String summary;

    int trackIndex = -1;
    te::EditItemID clipID;
    juce::String targetName;
    juce::String parameterID;
    juce::String beforeText;
    juce::String afterText;

    double beforeValue = 0.0;
    double afterValue = 0.0;
};

struct ToolPlan
{
    juce::String toolName;
    juce::String toolVersion;
    juce::String planID;
    juce::var inputParams;
    juce::String summary;
    juce::Array<ToolDiffEntry> changes;
    juce::File artifactFile;
};

juce::String toString (ToolDiffKind kind);
juce::String summariseToolPlan (const ToolPlan& plan);
juce::var toolPlanToJson (const ToolPlan& plan);

} // namespace waive
