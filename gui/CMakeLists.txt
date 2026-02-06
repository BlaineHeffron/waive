# ── Waive GUI ───────────────────────────────────────────────────────────────

juce_add_gui_app(Waive
    PRODUCT_NAME "Waive"
    COMPANY_NAME "Waive"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

target_sources(Waive PRIVATE
    src/Main.cpp
    src/MainComponent.h
    src/MainComponent.cpp

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

    # Tools
    src/tools/JobQueue.h
    src/tools/JobQueue.cpp

    # Utilities
    src/util/CommandHelpers.h
    src/util/CommandHelpers.cpp

    # Reuse the command handler for the in-app console.
    ../engine/src/CommandHandler.h
    ../engine/src/CommandHandler.cpp
)

target_compile_features(Waive PRIVATE cxx_std_20)

juce_generate_juce_header(Waive)

find_package(CURL REQUIRED)

target_include_directories(Waive PRIVATE
    ../engine/src
    src
    src/ui
    src/edit
    src/tools
    src/util
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
