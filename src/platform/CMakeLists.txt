# What differs by system: one interface per piece (include/gigchain/platform),
# one file per system (<piece>_win.cpp, _posix.cpp, _linux.cpp), picked by
# gigchain_platform_sources (docs/superpowers/specs/2026-09-30-cross-platform-linux-design.md).
add_library(gigchain_platform STATIC)
add_library(gigchain::platform ALIAS gigchain_platform)
target_include_directories(gigchain_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gigchain_platform PUBLIC gigchain::core Qt6::Core)
if(WIN32)
    target_compile_definitions(gigchain_platform PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(gigchain_platform PRIVATE psapi) # MemoryUse_win: GetProcessMemoryInfo
endif()
gigchain_platform_sources(gigchain_platform PluginFolders MemoryUse)
gigchain_target_defaults(gigchain_platform)
