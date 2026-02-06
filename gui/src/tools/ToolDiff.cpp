#include "ToolDiff.h"

namespace waive
{

juce::String toString (ToolDiffKind kind)
{
    switch (kind)
    {
        case ToolDiffKind::trackAdded:       return "track_added";
        case ToolDiffKind::trackRemoved:     return "track_removed";
        case ToolDiffKind::trackRenamed:     return "track_renamed";
        case ToolDiffKind::clipInserted:     return "clip_inserted";
        case ToolDiffKind::clipMoved:        return "clip_moved";
        case ToolDiffKind::clipTrimmed:      return "clip_trimmed";
        case ToolDiffKind::clipFadeChanged:  return "clip_fade_changed";
        case ToolDiffKind::parameterChanged: return "parameter_changed";
        case ToolDiffKind::automationChanged:return "automation_changed";
    }

    return "unknown";
}

juce::String summariseToolPlan (const ToolPlan& plan)
{
    juce::String text;
    text << "Tool: " << plan.toolName;

    if (plan.toolVersion.isNotEmpty())
        text << " v" << plan.toolVersion;

    if (plan.planID.isNotEmpty())
        text << "\nPlan ID: " << plan.planID;

    if (plan.summary.isNotEmpty())
        text << "\nSummary: " << plan.summary;

    text << "\nChanges (" << plan.changes.size() << "):\n";
    for (int i = 0; i < plan.changes.size(); ++i)
    {
        const auto& change = plan.changes.getReference (i);
        text << "  " << juce::String (i + 1) << ". ";

        if (change.summary.isNotEmpty())
            text << change.summary;
        else
            text << toString (change.kind);

        text << "\n";
    }

    if (plan.artifactFile != juce::File())
        text << "\nArtifact: " << plan.artifactFile.getFullPathName();

    return text;
}

juce::var toolPlanToJson (const ToolPlan& plan)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("tool_name", plan.toolName);
    obj->setProperty ("tool_version", plan.toolVersion);
    obj->setProperty ("plan_id", plan.planID);
    obj->setProperty ("summary", plan.summary);
    obj->setProperty ("input_params", plan.inputParams);
    obj->setProperty ("artifact_file", plan.artifactFile.getFullPathName());

    juce::Array<juce::var> changes;
    for (const auto& change : plan.changes)
    {
        auto* changeObj = new juce::DynamicObject();
        changeObj->setProperty ("kind", toString (change.kind));
        changeObj->setProperty ("summary", change.summary);
        changeObj->setProperty ("track_index", change.trackIndex);
        changeObj->setProperty ("clip_id", (juce::int64) change.clipID.getRawID());
        changeObj->setProperty ("target_name", change.targetName);
        changeObj->setProperty ("parameter_id", change.parameterID);
        changeObj->setProperty ("before_text", change.beforeText);
        changeObj->setProperty ("after_text", change.afterText);
        changeObj->setProperty ("before_value", change.beforeValue);
        changeObj->setProperty ("after_value", change.afterValue);
        changes.add (juce::var (changeObj));
    }

    obj->setProperty ("changes", changes);
    return juce::var (obj);
}

} // namespace waive
