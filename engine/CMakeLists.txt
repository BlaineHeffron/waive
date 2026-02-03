# ── Waive Engine ────────────────────────────────────────────────────────────

juce_add_console_app(WaiveEngine
    PRODUCT_NAME "Waive Engine"
    COMPANY_NAME "Waive"
)

target_sources(WaiveEngine PRIVATE
    src/Main.cpp
    src/CommandServer.h
    src/CommandServer.cpp
    src/CommandHandler.h
    src/CommandHandler.cpp
)

target_compile_features(WaiveEngine PRIVATE cxx_std_17)

target_link_libraries(WaiveEngine PRIVATE
    tracktion::tracktion_engine
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
