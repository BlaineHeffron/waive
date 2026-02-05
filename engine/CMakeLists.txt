# ── Waive Engine ────────────────────────────────────────────────────────────

juce_add_console_app(WaiveEngine
    PRODUCT_NAME "Waive Engine"
    COMPANY_NAME "Waive"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

target_sources(WaiveEngine PRIVATE
    src/Main.cpp
    src/CommandServer.h
    src/CommandServer.cpp
    src/CommandHandler.h
    src/CommandHandler.cpp
)

target_compile_features(WaiveEngine PRIVATE cxx_std_20)

juce_generate_juce_header(WaiveEngine)

find_package(CURL REQUIRED)

target_link_libraries(WaiveEngine PRIVATE
    tracktion::tracktion_engine
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    CURL::libcurl
)

target_compile_definitions(WaiveEngine PRIVATE
    JUCE_GUI_EXTRA=0
    JUCE_WEB_BROWSER=0
)
