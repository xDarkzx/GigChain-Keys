cmake_minimum_required(VERSION 3.24)
project(OpenStage VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerHardening)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Test)
find_package(tl-expected CONFIG REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/engine)
add_subdirectory(tests)

# Throwaway experiments (engine spike). Off by default; use the "spike" preset.
option(OPENSTAGE_BUILD_SPIKES "Build throwaway spike programs" OFF)
if(OPENSTAGE_BUILD_SPIKES)
    add_subdirectory(spikes/engine_spike)
endif()
