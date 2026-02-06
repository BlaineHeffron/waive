cmake_minimum_required(VERSION 3.22)
project(Waive VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch JUCE ──────────────────────────────────────────────────────────────
include(FetchContent)

# CI / sandbox environments may not have network access. If dependencies are already
# populated in the build tree, don't attempt to update them.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

FetchContent_Declare(
    tracktion_engine
    GIT_REPOSITORY https://github.com/Tracktion/tracktion_engine.git
    GIT_TAG        develop
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(tracktion_engine)

# ── Engine target ───────────────────────────────────────────────────────────
add_subdirectory(engine)

# ── GUI target ──────────────────────────────────────────────────────────────
add_subdirectory(gui)
