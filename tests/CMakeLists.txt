# ── Waive Core Tests ─────────────────────────────────────────────────────────

juce_add_console_app(WaiveCoreTests
    PRODUCT_NAME "Waive Core Tests"
    COMPANY_NAME "Waive"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

target_sources(WaiveCoreTests PRIVATE
    WaiveCoreTests.cpp

    # Reuse app edit/actions implementation under test.
    ../gui/src/edit/EditSession.h
    ../gui/src/edit/EditSession.cpp
    ../gui/src/ui/ClipEditActions.h
    ../gui/src/ui/ClipEditActions.cpp
)

target_compile_features(WaiveCoreTests PRIVATE cxx_std_20)
juce_generate_juce_header(WaiveCoreTests)
find_package(CURL REQUIRED)

target_include_directories(WaiveCoreTests PRIVATE
    ../gui/src/edit
    ../gui/src/ui
)

target_link_libraries(WaiveCoreTests PRIVATE
    tracktion::tracktion_engine
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    CURL::libcurl
)

target_compile_definitions(WaiveCoreTests PRIVATE
    JUCE_WEB_BROWSER=0
)

add_test(NAME WaiveCoreTests COMMAND WaiveCoreTests)

juce_add_console_app(WaiveUiTests
    PRODUCT_NAME "Waive UI Tests"
    COMPANY_NAME "Waive"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

target_sources(WaiveUiTests PRIVATE
    WaiveUiTests.cpp

    # Theme
    ../gui/src/theme/WaiveColours.h
    ../gui/src/theme/WaiveColours.cpp
    ../gui/src/theme/WaiveFonts.h
    ../gui/src/theme/WaiveLookAndFeel.h
    ../gui/src/theme/WaiveLookAndFeel.cpp

    # GUI and edit layer under test.
    ../gui/src/MainComponent.h
    ../gui/src/MainComponent.cpp
    ../gui/src/ui/SessionComponent.h
    ../gui/src/ui/SessionComponent.cpp
    ../gui/src/ui/ConsoleComponent.h
    ../gui/src/ui/ConsoleComponent.cpp
    ../gui/src/ui/ToolLogComponent.h
    ../gui/src/ui/ToolLogComponent.cpp
    ../gui/src/ui/LibraryComponent.h
    ../gui/src/ui/LibraryComponent.cpp
    ../gui/src/ui/PluginBrowserComponent.h
    ../gui/src/ui/PluginBrowserComponent.cpp
    ../gui/src/ui/SchemaFormComponent.h
    ../gui/src/ui/SchemaFormComponent.cpp
    ../gui/src/ui/ToolSidebarComponent.h
    ../gui/src/ui/ToolSidebarComponent.cpp
    ../gui/src/ui/TimelineComponent.h
    ../gui/src/ui/TimelineComponent.cpp
    ../gui/src/ui/TrackLaneComponent.h
    ../gui/src/ui/TrackLaneComponent.cpp
    ../gui/src/ui/ClipComponent.h
    ../gui/src/ui/ClipComponent.cpp
    ../gui/src/ui/PlayheadComponent.h
    ../gui/src/ui/PlayheadComponent.cpp
    ../gui/src/ui/TimeRulerComponent.h
    ../gui/src/ui/TimeRulerComponent.cpp
    ../gui/src/ui/ClipEditActions.h
    ../gui/src/ui/ClipEditActions.cpp
    ../gui/src/ui/SelectionManager.h
    ../gui/src/ui/SelectionManager.cpp
    ../gui/src/ui/MixerComponent.h
    ../gui/src/ui/MixerComponent.cpp
    ../gui/src/ui/MixerChannelStrip.h
    ../gui/src/ui/MixerChannelStrip.cpp
    ../gui/src/edit/EditSession.h
    ../gui/src/edit/EditSession.cpp
    ../gui/src/edit/UndoableCommandHandler.h
    ../gui/src/edit/UndoableCommandHandler.cpp
    ../gui/src/edit/ProjectManager.h
    ../gui/src/edit/ProjectManager.cpp
    ../gui/src/tools/JobQueue.h
    ../gui/src/tools/JobQueue.cpp
    ../gui/src/tools/ModelManager.h
    ../gui/src/tools/ModelManager.cpp
    ../gui/src/tools/AudioAnalysis.h
    ../gui/src/tools/AudioAnalysis.cpp
    ../gui/src/tools/Tool.h
    ../gui/src/tools/ToolDiff.h
    ../gui/src/tools/ToolDiff.cpp
    ../gui/src/tools/ToolRegistry.h
    ../gui/src/tools/ToolRegistry.cpp
    ../gui/src/tools/NormalizeSelectedClipsTool.h
    ../gui/src/tools/NormalizeSelectedClipsTool.cpp
    ../gui/src/tools/RenameTracksFromClipsTool.h
    ../gui/src/tools/RenameTracksFromClipsTool.cpp
    ../gui/src/tools/GainStageSelectedTracksTool.h
    ../gui/src/tools/GainStageSelectedTracksTool.cpp
    ../gui/src/tools/DetectSilenceAndCutRegionsTool.h
    ../gui/src/tools/DetectSilenceAndCutRegionsTool.cpp
    ../gui/src/tools/AlignClipsByTransientTool.h
    ../gui/src/tools/AlignClipsByTransientTool.cpp
    ../gui/src/tools/StemSeparationTool.h
    ../gui/src/tools/StemSeparationTool.cpp
    ../gui/src/tools/AutoMixSuggestionsTool.h
    ../gui/src/tools/AutoMixSuggestionsTool.cpp
    ../gui/src/util/CommandHelpers.h
    ../gui/src/util/CommandHelpers.cpp

    ../engine/src/CommandHandler.h
    ../engine/src/CommandHandler.cpp
)

target_compile_features(WaiveUiTests PRIVATE cxx_std_20)
juce_generate_juce_header(WaiveUiTests)

target_include_directories(WaiveUiTests PRIVATE
    ../engine/src
    ../gui/src
    ../gui/src/ui
    ../gui/src/edit
    ../gui/src/tools
    ../gui/src/util
    ../gui/src/theme
)

target_link_libraries(WaiveUiTests PRIVATE
    tracktion::tracktion_engine
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    CURL::libcurl
)

target_compile_definitions(WaiveUiTests PRIVATE
    JUCE_USE_GTK=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

add_test(NAME WaiveUiTests COMMAND WaiveUiTests)
