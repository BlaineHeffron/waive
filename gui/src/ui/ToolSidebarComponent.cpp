#include "ToolSidebarComponent.h"

#include <mutex>

#include "ToolRegistry.h"
#include "Tool.h"
#include "ToolDiff.h"
#include "ModelManager.h"
#include "EditSession.h"
#include "ProjectManager.h"
#include "SessionComponent.h"
#include "WaiveLookAndFeel.h"
#include "WaiveFonts.h"
#include "WaiveSpacing.h"

namespace
{
struct PlanState
{
    std::mutex mutex;
    std::optional<waive::ToolPlan> plan;
};
}

//==============================================================================
class ToolSidebarComponent::ModelManagerSection : public juce::Component
{
public:
    ModelManagerSection (waive::ModelManager& mgr, ToolSidebarComponent& parent)
        : modelManager (mgr), sidebarParent (parent)
    {
        addAndMakeVisible (titleLabel);
        titleLabel.setText ("Models", juce::dontSendNotification);
        titleLabel.setFont (juce::Font (14.0f, juce::Font::bold));

        addAndMakeVisible (usageLabel);
        addAndMakeVisible (settingsButton);
        settingsButton.setButtonText ("Settings");
        settingsButton.onClick = [this] { showSettingsDialog(); };

        refreshModelList();
    }

    void refreshModelList()
    {
        modelEntries.clear();

        auto available = modelManager.getAvailableModels();
        auto installed = modelManager.getInstalledModels();

        for (const auto& catalog : available)
        {
            auto entry = std::make_unique<ModelEntry>();
            entry->modelID = catalog.modelID;
            entry->displayName = catalog.displayName;
            entry->defaultVersion = catalog.defaultVersion;
            entry->installSizeBytes = catalog.installSizeBytes;

            auto it = std::find_if (installed.begin(), installed.end(),
                                    [&] (const auto& inst) { return inst.modelID == catalog.modelID; });
            if (it != installed.end())
            {
                entry->installedVersion = it->version;
                entry->isPinned = it->pinned;
            }

            entry->nameLabel.setText (catalog.displayName, juce::dontSendNotification);

            juce::String statusText;
            if (entry->installedVersion.isNotEmpty())
            {
                statusText = "v" + entry->installedVersion;
                if (entry->isPinned)
                    statusText += " (pinned)";
            }
            else
            {
                statusText = "Not installed";
            }
            entry->statusLabel.setText (statusText, juce::dontSendNotification);

            entry->installButton.setButtonText (entry->installedVersion.isEmpty() ? "Install" : "Uninstall");
            entry->installButton.onClick = [this, id = catalog.modelID, installed = !entry->installedVersion.isEmpty()]
            {
                if (installed)
                    uninstallModel (id);
                else
                    installModel (id);
            };

            entry->pinButton.setButtonText (entry->isPinned ? "Unpin" : "Pin");
            entry->pinButton.setEnabled (entry->installedVersion.isNotEmpty());
            entry->pinButton.onClick = [this, id = catalog.modelID, pinned = entry->isPinned]
            {
                if (pinned)
                    unpinModel (id);
                else
                    pinModel (id);
            };

            addAndMakeVisible (entry->nameLabel);
            addAndMakeVisible (entry->statusLabel);
            addAndMakeVisible (entry->installButton);
            addAndMakeVisible (entry->pinButton);

            modelEntries.push_back (std::move (entry));
        }

        updateUsageLabel();
        resized();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (waive::Spacing::xs);

        auto headerRow = bounds.removeFromTop (20);
        titleLabel.setBounds (headerRow.removeFromLeft (60));
        settingsButton.setBounds (headerRow.removeFromRight (70));

        bounds.removeFromTop (waive::Spacing::xxs);
        usageLabel.setBounds (bounds.removeFromTop (waive::Spacing::lg));
        bounds.removeFromTop (waive::Spacing::xs);

        for (auto& entry : modelEntries)
        {
            auto row = bounds.removeFromTop (24);
            entry->nameLabel.setBounds (row.removeFromLeft (120));
            entry->statusLabel.setBounds (row.removeFromLeft (100));
            row.removeFromLeft (waive::Spacing::xs);
            entry->pinButton.setBounds (row.removeFromLeft (50));
            row.removeFromLeft (waive::Spacing::xs);
            entry->installButton.setBounds (row);
        }
    }

    int getIdealHeight() const
    {
        return 20 + 2 + 16 + 4 + (int) modelEntries.size() * 24 + 8;
    }

private:
    struct ModelEntry
    {
        juce::String modelID;
        juce::String displayName;
        juce::String defaultVersion;
        juce::String installedVersion;
        int64 installSizeBytes = 0;
        bool isPinned = false;

        juce::Label nameLabel;
        juce::Label statusLabel;
        juce::TextButton installButton;
        juce::TextButton pinButton;
    };

    void installModel (const juce::String& modelID)
    {
        auto result = modelManager.installModel (modelID);
        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Install Failed",
                                                     result.getErrorMessage());
        }
        refreshModelList();
        sidebarParent.updateButtonStates();
    }

    void uninstallModel (const juce::String& modelID)
    {
        auto result = modelManager.uninstallModel (modelID);
        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Uninstall Failed",
                                                     result.getErrorMessage());
        }
        refreshModelList();
        sidebarParent.updateButtonStates();
    }

    void pinModel (const juce::String& modelID)
    {
        auto installedVersion = juce::String();
        auto installed = modelManager.getInstalledModels();
        auto it = std::find_if (installed.begin(), installed.end(),
                                [&] (const auto& m) { return m.modelID == modelID; });
        if (it != installed.end())
            installedVersion = it->version;

        auto result = modelManager.pinModelVersion (modelID, installedVersion);
        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Pin Failed",
                                                     result.getErrorMessage());
        }
        refreshModelList();
    }

    void unpinModel (const juce::String& modelID)
    {
        auto result = modelManager.unpinModelVersion (modelID);
        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                     "Unpin Failed",
                                                     result.getErrorMessage());
        }
        refreshModelList();
    }

    void updateUsageLabel()
    {
        auto usage = modelManager.getStorageUsageBytes();
        auto quota = modelManager.getQuotaBytes();

        auto usageMB = usage / (1024.0 * 1024.0);
        auto quotaMB = quota / (1024.0 * 1024.0);

        usageLabel.setText (juce::String::formatted ("Storage: %.1f / %.1f MB", usageMB, quotaMB),
                            juce::dontSendNotification);
    }

    void showSettingsDialog()
    {
        auto dialog = std::make_unique<juce::AlertWindow> ("Model Settings", "Configure storage directory and quota", juce::AlertWindow::NoIcon);

        dialog->addTextEditor ("directory", modelManager.getStorageDirectory().getFullPathName(), "Storage Directory:");
        dialog->addTextEditor ("quota", juce::String (modelManager.getQuotaBytes() / (1024 * 1024)), "Quota (MB):");

        dialog->addButton ("OK", 1);
        dialog->addButton ("Cancel", 0);

        auto& mgr = modelManager;
        auto* self = this;
        dialog->enterModalState (true, juce::ModalCallbackFunction::create ([self, &mgr, dialogPtr = dialog.get()] (int result)
        {
            if (result == 1)
            {
                auto dirPath = dialogPtr->getTextEditorContents ("directory");
                auto quotaMB = dialogPtr->getTextEditorContents ("quota").getLargeIntValue();

                if (dirPath.isNotEmpty())
                {
                    juce::File newDir (dirPath);
                    mgr.setStorageDirectory (newDir);
                }

                if (quotaMB > 0)
                {
                    auto quotaResult = mgr.setQuotaBytes (quotaMB * 1024 * 1024);
                    if (quotaResult.failed())
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                                 "Quota Update Failed",
                                                                 quotaResult.getErrorMessage());
                    }
                }

                self->updateUsageLabel();
                self->refreshModelList();
            }
        }), true);

        dialog.release();
    }

    waive::ModelManager& modelManager;
    ToolSidebarComponent& sidebarParent;

    juce::Label titleLabel;
    juce::Label usageLabel;
    juce::TextButton settingsButton;
    std::vector<std::unique_ptr<ModelEntry>> modelEntries;
};

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

    toolCombo.setTooltip ("Select Tool");
    planButton.setTooltip ("Plan Tool Execution");
    applyButton.setTooltip ("Apply Planned Changes");
    rejectButton.setTooltip ("Reject Plan");
    cancelButton.setTooltip ("Cancel Running Tool");

    toolCombo.setTitle ("Tool Selector");
    toolCombo.setDescription ("Select AI tool from list");
    planButton.setTitle ("Plan");
    planButton.setDescription ("Generate execution plan for selected tool");
    applyButton.setTitle ("Apply");
    applyButton.setDescription ("Apply planned changes to project");
    rejectButton.setTitle ("Reject");
    rejectButton.setDescription ("Reject planned changes");
    cancelButton.setTitle ("Cancel");
    cancelButton.setDescription ("Cancel running tool execution");

    toolCombo.setWantsKeyboardFocus (true);
    planButton.setWantsKeyboardFocus (true);
    applyButton.setWantsKeyboardFocus (true);
    rejectButton.setWantsKeyboardFocus (true);
    cancelButton.setWantsKeyboardFocus (true);

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

    toolCombo.onChange = [this] {
        loadDefaultParamsForSelectedTool();
        lastPlanError.clear();
        updateButtonStates();
    };
    planButton.onClick = [this] { runPlan(); };
    applyButton.onClick = [this] { applyPlan(); };
    rejectButton.onClick = [this] { rejectPlan(); };
    cancelButton.onClick = [this] { cancelRunningPlan(); };

    modelManagerSection = std::make_unique<ModelManagerSection> (modelMgr, *this);
    addAndMakeVisible (modelManagerSection.get());

    populateToolList();
    updateButtonStates();

    jobQueue.addListener (this);
}

ToolSidebarComponent::~ToolSidebarComponent()
{
    jobQueue.removeListener (this);
}

void ToolSidebarComponent::paint (juce::Graphics& g)
{
    if (toolCombo.getSelectedId() == 0)
    {
        auto bounds = getLocalBounds();
        g.setFont (waive::Fonts::body());
        if (auto* pal = waive::getWaivePalette (*this))
            g.setColour (pal->textMuted);
        else
            g.setColour (juce::Colours::grey);
        g.drawText ("Select a tool from the dropdown above", bounds, juce::Justification::centred, true);
    }
}

void ToolSidebarComponent::resized()
{
    auto bounds = getLocalBounds().reduced (waive::Spacing::sm);

    // Model manager section at top
    if (modelManagerSection != nullptr)
    {
        auto modelSectionHeight = juce::jlimit (120, 240, modelManagerSection->getIdealHeight());
        modelManagerSection->setBounds (bounds.removeFromTop (modelSectionHeight));
        bounds.removeFromTop (6);
    }

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
        lastPlanError = prepResult.getErrorMessage();
        setStatusText (lastPlanError);
        updateButtonStates();
        return;
    }

    lastPlanError.clear();

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
    bool canPlan = !planRunning;

    if (canPlan)
    {
        auto toolName = getSelectedToolName();
        auto requiredModel = getToolModelRequirement (toolName);

        if (requiredModel.isNotEmpty())
        {
            auto resolved = modelManager.resolveInstalledModel (requiredModel);
            if (!resolved.has_value())
            {
                canPlan = false;
                if (lastPlanError.isEmpty())
                {
                    setStatusText ("Model '" + requiredModel + "' required. Install it via Model Manager.");
                }
            }
        }
    }

    planButton.setEnabled (canPlan);
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

juce::String ToolSidebarComponent::getToolModelRequirement (const juce::String& toolName) const
{
    if (toolToRequiredModel.count (toolName) > 0)
        return toolToRequiredModel[toolName];

    auto* tool = toolRegistry.findTool (toolName);
    if (tool == nullptr)
        return {};

    const auto desc = tool->describe();
    toolToRequiredModel[toolName] = desc.modelRequirement;

    return toolToRequiredModel[toolName];
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
            // CRITICAL: Explicitly call loadDefaultParamsForSelectedTool to ensure schema rebuild.
            // toolCombo.onChange may not fire reliably in test context.
            loadDefaultParamsForSelectedTool();
            // Run message loop to allow schemaForm fields to fully initialize after buildFromSchema().
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
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

juce::String ToolSidebarComponent::getStatusTextForTesting() const
{
    return statusLabel.getText();
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

juce::String ToolSidebarComponent::getToolModelRequirementForTesting (const juce::String& toolName) const
{
    return getToolModelRequirement (toolName);
}
