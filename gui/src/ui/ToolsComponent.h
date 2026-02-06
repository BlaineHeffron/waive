#pragma once

#include <JuceHeader.h>
#include <optional>

#include "JobQueue.h"
#include "ToolDiff.h"

class EditSession;
class ProjectManager;
class SessionComponent;

namespace waive { class ToolRegistry; }
namespace waive { class ModelManager; }

//==============================================================================
/** UI panel for running in-process tools with plan/preview/apply workflow. */
class ToolsComponent : public juce::Component,
                       public waive::JobQueue::Listener
{
public:
    ToolsComponent (waive::ToolRegistry& registry,
                    EditSession& session,
                    ProjectManager& projectMgr,
                    SessionComponent& sessionComp,
                    waive::ModelManager& modelMgr,
                    waive::JobQueue& queue);
    ~ToolsComponent() override;

    void resized() override;

    //==============================================================================
    // Test helpers for no-user UI coverage.
    void selectToolForTesting (const juce::String& toolName);
    void setParamsForTesting (const juce::var& params);
    bool runPlanForTesting();
    bool applyPlanForTesting();
    void rejectPlanForTesting();
    void cancelPlanForTesting();
    bool waitForIdleForTesting (int timeoutMs = 4000);
    bool hasPendingPlanForTesting() const;
    juce::String getPreviewTextForTesting() const;
    juce::File getLastPlanArtifactForTesting() const;
    juce::Result setModelStorageDirectoryForTesting (const juce::File& directory);
    juce::Result setModelQuotaForTesting (int64 bytes);
    juce::Result installModelForTesting (const juce::String& modelID,
                                         const juce::String& version = {},
                                         bool pinVersion = false);
    juce::Result uninstallModelForTesting (const juce::String& modelID,
                                           const juce::String& version = {});
    juce::Result pinModelVersionForTesting (const juce::String& modelID,
                                            const juce::String& version);
    juce::String getPinnedModelVersionForTesting (const juce::String& modelID) const;
    bool isModelInstalledForTesting (const juce::String& modelID,
                                     const juce::String& version = {}) const;

    //==============================================================================
    // waive::JobQueue::Listener
    void jobEvent (const waive::JobEvent& event) override;

private:
    void populateToolList();
    void loadDefaultParamsForSelectedTool();

    void runPlan();
    void applyPlan();
    void rejectPlan();
    void cancelRunningPlan();

    juce::Result parseParamsFromEditor (juce::var& outParams) const;
    juce::String getSelectedToolName() const;
    void updateButtonStates();
    void setStatusText (const juce::String& text);
    void handlePlanCompletion (waive::JobStatus status, std::optional<waive::ToolPlan> planResult);
    juce::File resolveProjectCacheDirectory() const;

    waive::ToolRegistry& toolRegistry;
    EditSession& editSession;
    ProjectManager& projectManager;
    SessionComponent& sessionComponent;
    waive::ModelManager& modelManager;
    waive::JobQueue& jobQueue;

    juce::StringArray toolNamesByComboIndex;
    std::optional<waive::ToolPlan> pendingPlan;
    juce::File lastPlanArtifact;

    int activePlanJobID = 0;
    bool planRunning = false;

    juce::Label toolLabel { {}, "Tool" };
    juce::ComboBox toolCombo;
    juce::Label paramsLabel { {}, "Params (JSON)" };
    juce::TextEditor paramsEditor;
    juce::TextButton planButton { "Plan" };
    juce::TextButton applyButton { "Apply" };
    juce::TextButton rejectButton { "Reject" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label statusLabel;
    juce::Label previewLabel { {}, "Preview" };
    juce::TextEditor previewEditor;
};
