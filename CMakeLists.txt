cmake_minimum_required(VERSION 3.22)

project(sp303
    VERSION 0.1.0
    DESCRIPTION "SP-303-style sampler engine and plugin"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------
option(SP303_BUILD_TESTS  "Build core unit tests"                      ON)
option(SP303_BUILD_PLUGIN "Build the JUCE plugin (downloads JUCE)"     OFF)
option(SP303_RT_SANITIZE  "Enable real-time safety checks (debug)"     OFF)
option(SP303_WERROR       "Treat warnings as errors"                   OFF)

# ----------------------------------------------------------------------------
# Layer 0 - pure C++ DSP core. No JUCE, no plugin SDK, no dependencies.
# This is what the tests and the Python null-test harness exercise.
# ----------------------------------------------------------------------------
add_subdirectory(core)

if(SP303_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()

# ----------------------------------------------------------------------------
# Layers 1-4 - JUCE plugin wrapper. Optional: requires network on first
# configure to fetch JUCE.
# ----------------------------------------------------------------------------
if(SP303_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()

message(STATUS "")
message(STATUS "sp303 ${PROJECT_VERSION}")
message(STATUS "  build type ....... ${CMAKE_BUILD_TYPE}")
message(STATUS "  tests ............ ${SP303_BUILD_TESTS}")
message(STATUS "  plugin ........... ${SP303_BUILD_PLUGIN}")
message(STATUS "  rt sanitize ...... ${SP303_RT_SANITIZE}")
message(STATUS "")
