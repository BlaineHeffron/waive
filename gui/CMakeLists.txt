# ── Waive GUI ───────────────────────────────────────────────────────────────

juce_add_gui_app(Waive
    PRODUCT_NAME "Waive"
    COMPANY_NAME "Waive"
    NEEDS_WEB_BROWSER FALSE
)

target_sources(Waive PRIVATE
    src/Main.cpp
    src/MainComponent.h
    src/MainComponent.cpp

    # Theme
    src/theme/WaiveColours.h
    src/theme/WaiveColours.cpp
    src/theme/WaiveFonts.h
    src/theme/WaiveSpacing.h
    src/theme/WaiveLookAndFeel.h
    src/theme/WaiveLookAndFeel.cpp

    # UI components
    src/ui/SessionComponent.h
    src/ui/SessionComponent.cpp
    src/ui/ConsoleComponent.h
    src/ui/ConsoleComponent.cpp
    src/ui/ToolLogComponent.h
    src/ui/ToolLogComponent.cpp
    src/ui/LibraryComponent.h
    src/ui/LibraryComponent.cpp
    src/ui/PluginBrowserComponent.h
    src/ui/PluginBrowserComponent.cpp
    src/ui/SchemaFormComponent.h
    src/ui/SchemaFormComponent.cpp
    src/ui/ToolSidebarComponent.h
    src/ui/ToolSidebarComponent.cpp
    src/ui/TimelineComponent.h
    src/ui/TimelineComponent.cpp
    src/ui/TrackLaneComponent.h
    src/ui/TrackLaneComponent.cpp
    src/ui/ClipComponent.h
    src/ui/ClipComponent.cpp
    src/ui/PlayheadComponent.h
    src/ui/PlayheadComponent.cpp
    src/ui/TimeRulerComponent.h
    src/ui/TimeRulerComponent.cpp
    src/ui/ClipEditActions.h
    src/ui/ClipEditActions.cpp
    src/ui/SelectionManager.h
    src/ui/SelectionManager.cpp
    src/ui/MixerComponent.h
    src/ui/MixerComponent.cpp
    src/ui/MixerChannelStrip.h
    src/ui/MixerChannelStrip.cpp

    # Edit layer
    src/edit/EditSession.h
    src/edit/EditSession.cpp
    src/edit/UndoableCommandHandler.h
    src/edit/UndoableCommandHandler.cpp
    src/edit/ProjectManager.h
    src/edit/ProjectManager.cpp
    src/edit/AutoSaveManager.h
    src/edit/AutoSaveManager.cpp

    # Tools
    src/tools/JobQueue.h
    src/tools/JobQueue.cpp
    src/tools/ModelManager.h
    src/tools/ModelManager.cpp
    src/tools/AudioAnalysis.h
    src/tools/AudioAnalysis.cpp
    src/tools/AudioAnalysisCache.h
    src/tools/AudioAnalysisCache.cpp
    src/tools/ClipTrackIndexMap.h
    src/tools/ClipTrackIndexMap.cpp
    src/tools/Tool.h
    src/tools/ToolDiff.h
    src/tools/ToolDiff.cpp
    src/tools/ToolRegistry.h
    src/tools/ToolRegistry.cpp
    src/tools/ExternalToolManifest.h
    src/tools/ExternalToolManifest.cpp
    src/tools/ExternalToolRunner.h
    src/tools/ExternalToolRunner.cpp
    src/tools/ExternalTool.h
    src/tools/ExternalTool.cpp
    src/tools/NormalizeSelectedClipsTool.h
    src/tools/NormalizeSelectedClipsTool.cpp
    src/tools/RenameTracksFromClipsTool.h
    src/tools/RenameTracksFromClipsTool.cpp
    src/tools/GainStageSelectedTracksTool.h
    src/tools/GainStageSelectedTracksTool.cpp
    src/tools/DetectSilenceAndCutRegionsTool.h
    src/tools/DetectSilenceAndCutRegionsTool.cpp
    src/tools/AlignClipsByTransientTool.h
    src/tools/AlignClipsByTransientTool.cpp
    src/tools/StemSeparationTool.h
    src/tools/StemSeparationTool.cpp
    src/tools/AutoMixSuggestionsTool.h
    src/tools/AutoMixSuggestionsTool.cpp

    # AI
    src/ai/AiSettings.h
    src/ai/AiSettings.cpp
    src/ai/AiProvider.h
    src/ai/AiProvider.cpp
    src/ai/AiToolSchema.h
    src/ai/AiToolSchema.cpp
    src/ai/AiAgent.h
    src/ai/AiAgent.cpp
    src/ai/ChatHistorySerializer.h
    src/ai/ChatHistorySerializer.cpp
    src/ai/ChatPanelComponent.h
    src/ai/ChatPanelComponent.cpp

    # Utilities
    src/util/CommandHelpers.h
    src/util/CommandHelpers.cpp
    src/util/PathSanitizer.h
    src/util/PathSanitizer.cpp
    src/util/ScreenshotCapture.h
    src/util/ScreenshotCapture.cpp

    # Reuse the command handler for the in-app console.
    ../engine/src/CommandHandler.h
    ../engine/src/CommandHandler.cpp
)

target_compile_features(Waive PRIVATE cxx_std_20)

juce_generate_juce_header(Waive)

# CURL is not used directly by Waive but is required by JUCE's network/URL functionality
find_package(CURL REQUIRED)

target_include_directories(Waive PRIVATE
    ../engine/src
    src
    src/ui
    src/edit
    src/tools
    src/util
    src/theme
    src/ai
)

target_link_libraries(Waive PRIVATE
    tracktion::tracktion_engine
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    CURL::libcurl
)

# Some sandbox/dev environments don't have GTK dev headers installed.
# JUCE can build without GTK (falls back to X11 where available).
target_compile_definitions(Waive PRIVATE
    JUCE_USE_GTK=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

if(WAIVE_PROFILE_TOOLS)
    target_compile_definitions(Waive PRIVATE WAIVE_PROFILE_TOOLS=1)
endif()
