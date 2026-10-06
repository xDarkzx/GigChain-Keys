# What differs by system: one interface per piece (include/gigchain/platform),
# one file per system (<piece>_win.cpp, _posix.cpp, _linux.cpp), picked by
# gigchain_platform_sources (cmake/PlatformSources.cmake).
add_library(gigchain_platform STATIC PlatformLog.h PlatformLog.cpp)
add_library(gigchain::platform ALIAS gigchain_platform)
target_include_directories(gigchain_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gigchain_platform PUBLIC gigchain::core Qt6::Core Qt6::Gui)
if(WIN32)
    target_compile_definitions(gigchain_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    # MemoryUse_win: GetProcessMemoryInfo; Timing_win: timeBeginPeriod.
    # CrashHandler_win: MiniDumpWriteDump.
    target_link_libraries(gigchain_platform PRIVATE psapi winmm dbghelp)
endif()
gigchain_platform_sources(gigchain_platform PluginFolders MemoryUse Process Timing Windows CrashHandler InstanceLock Audio)
gigchain_target_defaults(gigchain_platform)
