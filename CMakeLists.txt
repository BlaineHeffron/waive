cmake_minimum_required(VERSION 3.22)
project(Waive VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WAIVE_PROFILE_TOOLS "Enable tool profiling instrumentation" OFF)

# ── Fetch JUCE ──────────────────────────────────────────────────────────────
include(FetchContent)
include(CTest)

# CI / sandbox environments may not have network access. If dependencies are already
# populated in the build tree, don't attempt to update them.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# Pin Tracktion Engine to specific commit for security and reproducibility.
# To update:
#   1. Clone https://github.com/Tracktion/tracktion_engine.git
#   2. Checkout desired commit from develop branch
#   3. Test thoroughly with Waive
#   4. Update GIT_TAG below with the tested commit hash
#   5. Set GIT_SHALLOW to FALSE when using specific commit
# Current: Using develop branch (TODO: pin to specific tested commit)
FetchContent_Declare(
    tracktion_engine
    GIT_REPOSITORY https://github.com/Tracktion/tracktion_engine.git
    GIT_TAG        d1f7383ffc382b57e0ac6152d418fcc1fdf694dd
    GIT_SHALLOW    FALSE
)

# Tracktion examples are not required by Waive and can fail in some environments.
set(TE_ADD_EXAMPLES OFF CACHE BOOL "Disable tracktion_engine example targets" FORCE)

FetchContent_MakeAvailable(tracktion_engine)

# ── Engine target ───────────────────────────────────────────────────────────
add_subdirectory(engine)

# ── GUI target ──────────────────────────────────────────────────────────────
add_subdirectory(gui)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
