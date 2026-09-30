# Steinberg VST3 SDK (MIT since 3.8), fetched from GitHub.
#
# EXCLUDE_FROM_ALL: only the targets we link get built (the SDK always adds its
# validator and samples). SYSTEM: SDK headers never trip our /W4 /WX.
include(FetchContent)

set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.8.0_build_66
    GIT_SHALLOW TRUE
    GIT_SUBMODULES base cmake pluginterfaces public.sdk
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(vst3sdk)

# sdk_hosting has the module's common part (module.cpp) but not the system's
# module loader or PlugProvider; hosts compile these themselves (as the SDK
# samples and Muse do). They are written for C++17 (path::u8string()
# returning std::string), so they build as their own library.
set(VST3_HOSTING_DIR ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting)
add_library(vst3_host_support STATIC
    ${VST3_HOSTING_DIR}/plugprovider.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)
# The module loader for this system (Steinberg's own, one per system).
if(WIN32)
    target_sources(vst3_host_support PRIVATE ${VST3_HOSTING_DIR}/module_win32.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(vst3_host_support PRIVATE ${VST3_HOSTING_DIR}/module_linux.cpp)
    target_link_libraries(vst3_host_support PUBLIC ${CMAKE_DL_LIBS})
elseif(APPLE)
    # As Steinberg's editorhost sample: the Mac loader is Objective-C++ and
    # refuses to build without ARC ("#error this file needs to be compiled
    # with automatic reference counting enabled"). (.mm files build with the
    # C++ compiler, which Clang reads as Objective-C++ by their extension, as
    # the SDK's own sdk_common builds its .mm files.)
    target_sources(vst3_host_support PRIVATE ${VST3_HOSTING_DIR}/module_mac.mm)
    set_source_files_properties(${VST3_HOSTING_DIR}/module_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    # Cocoa (AppKit, Foundation) for module_mac.mm and sdk_common's
    # threadchecker_mac.mm / systemclipboard_mac.mm; CoreFoundation for CFBundle.
    target_link_libraries(vst3_host_support PUBLIC "-framework Cocoa" "-framework CoreFoundation")
endif()
set_target_properties(vst3_host_support PROPERTIES CXX_STANDARD 17 AUTOMOC OFF POSITION_INDEPENDENT_CODE ON)
target_link_libraries(vst3_host_support PUBLIC sdk_hosting)
