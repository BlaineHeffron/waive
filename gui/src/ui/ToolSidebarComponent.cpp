#include "ToolSidebarComponent.h"

#include <mutex>

#include "ToolRegistry.h"
#include "Tool.h"
#include "ToolDiff.h"
#include "ModelManager.h"
#include "EditSession.h"
#include "ProjectManager.h"
#include "SessionComponent.h"

namespace
{
struct PlanState
{
    std::mutex mutex;
    std::optional<waive::ToolPlan> plan;
};
}

//==============================================================================
ToolSidebarComponent::ToolSidebarComponent (waive::ToolRegistry& registry,
                                            EditSession& session,
                                            ProjectManager& projectMgr,
                                            SessionComponent& sessionComp,
                                            waive::ModelManager& modelMgr,
                                            waive::JobQueue& queue)
    : toolRegistry (registry),
      editSession (session),
      projectManager (projectMgr),
      sessionComponent (sessionComp),
      modelManager (modelMgr),
      jobQueue (queue)
{
    toolLabel.setJustificationType (juce::Justification::centredLeft);
    previewLabel.setJustificationType (juce::Justification::centredLeft);

    previewEditor.setMultiLine (true);
    previewEditor.setReadOnly (true);
    previewEditor.setReturnKeyStartsNewLine (true);
    previewEditor.setScrollbarsShown (true);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setText ("Idle", juce::dontSendNotification);

    schemaViewport.setViewedComponent (&schemaForm, false);
    schemaViewport.setScrollBarsShown (true, false);

    addAndMakeVisible (toolLabel);
    addAndMakeVisible (toolCombo);
    addAndMakeVisible (schemaViewport);
    addAndMakeVisible (planButton);
    addAndMakeVisible (applyButton);
    addAndMakeVisible (rejectButton);
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (previewLabel);
    addAndMakeVisible (previewEditor);

    toolCombo.onChange = [this] { loadDefaultParamsForSelectedTool(); };
    planButton.onClick = [this] { runPlan(); };
    applyButton.onClick = [this] { applyPlan(); };
    rejectButton.onClick = [this] { rejectPlan(); };
    cancelButton.onClick = [this] { cancelRunningPlan(); };

    populateToolList();
    updateButtonStates();

    jobQueue.addListener (this);
}

ToolSidebarComponent::~ToolSidebarComponent()
{
    jobQueue.removeListener (this);
}

void ToolSidebarComponent::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    auto toolRow = bounds.removeFromTop (24);
    toolLabel.setBounds (toolRow.removeFromLeft (36));
    toolCombo.setBounds (toolRow);

    bounds.removeFromTop (6);

    // Schema form in viewport
    auto schemaHeight = juce::jmin (schemaForm.getIdealHeight(), bounds.getHeight() / 3);
    schemaHeight = juce::jmax (schemaHeight, 60);
    schemaViewport.setBounds (bounds.removeFromTop (schemaHeight));
    schemaForm.setSize (schemaViewport.getWidth() - (schemaViewport.isVerticalScrollBarShown() ? 10 : 0),
                         juce::jmax (schemaForm.getIdealHeight(), schemaHeight));

    bounds.removeFromTop (6);
    auto actionRow = bounds.removeFromTop (26);
    int btnW = (actionRow.getWidth() - 12) / 4;
    planButton.setBounds (actionRow.removeFromLeft (btnW));
    actionRow.removeFromLeft (4);
    applyButton.setBounds (actionRow.removeFromLeft (btnW));
    actionRow.removeFromLeft (4);
    rejectButton.setBounds (actionRow.removeFromLeft (btnW));
    actionRow.removeFromLeft (4);
    cancelButton.setBounds (actionRow);

    bounds.removeFromTop (6);
    statusLabel.setBounds (bounds.removeFromTop (18));
    bounds.removeFromTop (4);
    previewLabel.setBounds (bounds.removeFromTop (18));
    bounds.removeFromTop (2);
    previewEditor.setBounds (bounds);
}

void ToolSidebarComponent::populateToolList()
{
    toolCombo.clear (juce::dontSendNotification);
    toolNamesByComboIndex.clear();

    int itemId = 1;
    for (const auto& tool : toolRegistry.getTools())
    {
        if (tool == nullptr)
            continue;

        const auto desc = tool->describe();
        toolCombo.addItem (desc.displayName.isNotEmpty() ? desc.displayName : desc.name, itemId++);
        toolNamesByComboIndex.add (desc.name);
    }

    if (toolCombo.getNumItems() > 0)
        toolCombo.setSelectedItemIndex (0, juce::sendNotification);
}

void ToolSidebarComponent::loadDefaultParamsForSelectedTool()
{
    auto toolName = getSelectedToolName();
    auto* tool = toolRegistry.findTool (toolName);
    if (tool == nullptr)
        return;

    const auto desc = tool->describe();
    schemaForm.buildFromSchema (desc.inputSchema, desc.defaultParams);

    // Resize form within viewport
    schemaForm.setSize (schemaViewport.getWidth() - (schemaViewport.isVerticalScrollBarShown() ? 10 : 0),
                         juce::jmax (schemaForm.getIdealHeight(), schemaViewport.getHeight()));
}

void ToolSidebarComponent::runPlan()
{
    if (planRunning)
        return;

    auto toolName = getSelectedToolName();
    auto* tool = toolRegistry.findTool (toolName);

    if (tool == nullptr)
    {
        setStatusText ("No tool selected");
        return;
    }

    auto params = schemaForm.getParams();

    waive::ToolPlanTask task;
    waive::ToolExecutionContext context {
        editSession,
        projectManager,
        sessionComponent,
        modelManager,
        resolveProjectCacheDirectory()
    };

    auto prepResult = tool->preparePlan (context, params, task);
    if (prepResult.failed())
    {
        setStatusText ("Plan failed: " + prepResult.getErrorMessage());
        return;
    }

    pendingPlan.reset();
    sessionComponent.clearToolPreview();
    previewEditor.clear();

    auto sharedPlan = std::make_shared<PlanState>();

    activePlanJobID = jobQueue.submit (
        { task.jobName, "tool_plan" },
        [taskFn = std::move (task.run), sharedPlan] (waive::ProgressReporter& reporter)
        {
            auto producedPlan = taskFn (reporter);
            std::lock_guard<std::mutex> lock (sharedPlan->mutex);
            sharedPlan->plan = std::move (producedPlan);
        },
        [this, sharedPlan] (int, waive::JobStatus status)
        {
            std::optional<waive::ToolPlan> result;
            {
                std::lock_guard<std::mutex> lock (sharedPlan->mutex);
                if (sharedPlan->plan.has_value())
                    result = std::move (sharedPlan->plan.value());
            }

            handlePlanCompletion (status, std::move (result));
        });

    planRunning = true;
    setStatusText ("Planning...");
    updateButtonStates();
}

void ToolSidebarComponent::applyPlan()
{
    if (planRunning || ! pendingPlan.has_value())
        return;

    const auto toolName = pendingPlan->toolName;
    auto* tool = toolRegistry.findTool (toolName);
    if (tool == nullptr)
    {
        setStatusText ("Cannot apply: tool not found");
        return;
    }

    waive::ToolExecutionContext context {
        editSession,
        projectManager,
        sessionComponent,
        modelManager,
        resolveProjectCacheDirectory()
    };

    auto result = tool->apply (context, *pendingPlan);
    if (result.failed())
    {
        setStatusText ("Apply failed: " + result.getErrorMessage());
        return;
    }

    setStatusText ("Applied " + juce::String (pendingPlan->changes.size()) + " change(s)");
    pendingPlan.reset();
    previewEditor.clear();
    sessionComponent.clearToolPreview();
    updateButtonStates();
}

void ToolSidebarComponent::rejectPlan()
{
    if (planRunning)
        return;

    pendingPlan.reset();
    previewEditor.clear();
    sessionComponent.clearToolPreview();
    setStatusText ("Plan rejected");
    updateButtonStates();
}

void ToolSidebarComponent::cancelRunningPlan()
{
    if (! planRunning || activePlanJobID <= 0)
        return;

    jobQueue.cancelJob (activePlanJobID);
}

juce::String ToolSidebarComponent::getSelectedToolName() const
{
    const int idx = toolCombo.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (idx, toolNamesByComboIndex.size()))
        return {};

    return toolNamesByComboIndex[idx];
}

void ToolSidebarComponent::updateButtonStates()
{
    planButton.setEnabled (! planRunning);
    applyButton.setEnabled (! planRunning && pendingPlan.has_value());
    rejectButton.setEnabled (! planRunning && pendingPlan.has_value());
    cancelButton.setEnabled (planRunning);
}

void ToolSidebarComponent::setStatusText (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void ToolSidebarComponent::handlePlanCompletion (waive::JobStatus status, std::optional<waive::ToolPlan> planResult)
{
    planRunning = false;
    activePlanJobID = 0;

    if (status == waive::JobStatus::Completed)
    {
        if (planResult.has_value() && ! planResult->changes.isEmpty())
        {
            pendingPlan = std::move (planResult);
            previewEditor.setText (waive::summariseToolPlan (*pendingPlan), false);
            lastPlanArtifact = pendingPlan->artifactFile;
            sessionComponent.applyToolPreviewDiff (pendingPlan->changes);
            setStatusText ("Plan ready");
        }
        else
        {
            pendingPlan.reset();
            sessionComponent.clearToolPreview();
            setStatusText ("Plan produced no changes");
        }
    }
    else if (status == waive::JobStatus::Cancelled)
    {
        pendingPlan.reset();
        previewEditor.clear();
        sessionComponent.clearToolPreview();
        setStatusText ("Plan cancelled");
    }
    else if (status == waive::JobStatus::Failed)
    {
        pendingPlan.reset();
        previewEditor.clear();
        sessionComponent.clearToolPreview();
        setStatusText ("Plan failed");
    }
    else
    {
        pendingPlan.reset();
        previewEditor.clear();
        sessionComponent.clearToolPreview();
        setStatusText ("Plan did not complete");
    }

    updateButtonStates();
}

juce::File ToolSidebarComponent::resolveProjectCacheDirectory() const
{
    auto currentProjectFile = projectManager.getCurrentFile();
    if (currentProjectFile != juce::File())
        return currentProjectFile.getParentDirectory()
                                 .getChildFile (".waive_cache")
                                 .getChildFile (currentProjectFile.getFileNameWithoutExtension());

    return juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("waive")
                      .getChildFile ("unsaved_project")
                      .getChildFile (projectManager.getProjectName());
}

void ToolSidebarComponent::jobEvent (const waive::JobEvent& event)
{
    if (! planRunning || event.jobId != activePlanJobID)
        return;

    if (event.status == waive::JobStatus::Running)
    {
        auto pct = juce::jlimit (0, 100, (int) std::round (event.progress * 100.0f));
        auto text = "Planning... " + juce::String (pct) + "%";
        if (event.message.isNotEmpty())
            text += " (" + event.message + ")";
        setStatusText (text);
    }
}

//==============================================================================
// Test helpers
//==============================================================================

void ToolSidebarComponent::selectToolForTesting (const juce::String& toolName)
{
    for (int i = 0; i < toolNamesByComboIndex.size(); ++i)
    {
        if (toolNamesByComboIndex[i] == toolName)
        {
            toolCombo.setSelectedItemIndex (i, juce::sendNotification);
            break;
        }
    }
}

void ToolSidebarComponent::setParamsForTesting (const juce::var& params)
{
    schemaForm.setParams (params);
}

bool ToolSidebarComponent::runPlanForTesting()
{
    if (planRunning)
        return false;

    runPlan();
    return planRunning;
}

bool ToolSidebarComponent::applyPlanForTesting()
{
    if (! pendingPlan.has_value() || planRunning)
        return false;

    applyPlan();
    return ! pendingPlan.has_value();
}

void ToolSidebarComponent::rejectPlanForTesting()
{
    rejectPlan();
}

void ToolSidebarComponent::cancelPlanForTesting()
{
    cancelRunningPlan();
}

bool ToolSidebarComponent::waitForIdleForTesting (int timeoutMs)
{
    int elapsedMs = 0;
    constexpr int stepMs = 20;

    while (planRunning && elapsedMs < timeoutMs)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (stepMs);
        juce::Thread::sleep (2);
        elapsedMs += stepMs;
    }

    return ! planRunning;
}

bool ToolSidebarComponent::hasPendingPlanForTesting() const
{
    return pendingPlan.has_value();
}

juce::String ToolSidebarComponent::getPreviewTextForTesting() const
{
    return previewEditor.getText();
}

juce::File ToolSidebarComponent::getLastPlanArtifactForTesting() const
{
    return lastPlanArtifact;
}

juce::Result ToolSidebarComponent::setModelStorageDirectoryForTesting (const juce::File& directory)
{
    modelManager.setStorageDirectory (directory);
    return juce::Result::ok();
}

juce::Result ToolSidebarComponent::setModelQuotaForTesting (int64 bytes)
{
    return modelManager.setQuotaBytes (bytes);
}

juce::Result ToolSidebarComponent::installModelForTesting (const juce::String& modelID,
                                                            const juce::String& version,
                                                            bool pinVersion)
{
    return modelManager.installModel (modelID, version, pinVersion);
}

juce::Result ToolSidebarComponent::uninstallModelForTesting (const juce::String& modelID,
                                                              const juce::String& version)
{
    return modelManager.uninstallModel (modelID, version);
}

juce::Result ToolSidebarComponent::pinModelVersionForTesting (const juce::String& modelID,
                                                               const juce::String& version)
{
    return modelManager.pinModelVersion (modelID, version);
}

juce::String ToolSidebarComponent::getPinnedModelVersionForTesting (const juce::String& modelID) const
{
    return modelManager.getPinnedVersion (modelID);
}

bool ToolSidebarComponent::isModelInstalledForTesting (const juce::String& modelID,
                                                        const juce::String& version) const
{
    return modelManager.isInstalled (modelID, version);
}
