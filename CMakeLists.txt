cmake_minimum_required(VERSION 3.24)
include(${CMAKE_CURRENT_SOURCE_DIR}/branding.cmake) # the product's name: change it there
project(GigChain VERSION ${PRODUCT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerHardening)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Multimedia Test)
find_package(tl-expected CONFIG REQUIRED)
find_package(RtAudio CONFIG REQUIRED)
find_package(RtMidi CONFIG REQUIRED)
# MSVC refuses to link ASan-instrumented objects with uninstrumented ones
# that use the same STL containers, so third-party code built here (the VST3
# SDK) is instrumented too.
if(GIGCHAIN_ASAN AND MSVC)
    add_compile_options(/fsanitize=address)
    add_link_options(/INCREMENTAL:NO)
endif()
include(Vst3Sdk)
qt_standard_project_setup(REQUIRES 6.8)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/engine)
add_subdirectory(src/scanner)
add_subdirectory(src/ui)
add_subdirectory(src/app)
add_subdirectory(tests)

# Throwaway experiments (engine spike). Off by default; use the "spike" preset.
option(GIGCHAIN_BUILD_SPIKES "Build throwaway spike programs" OFF)
if(GIGCHAIN_BUILD_SPIKES)
    add_subdirectory(spikes/engine_spike)
endif()
