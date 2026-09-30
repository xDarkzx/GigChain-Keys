# The soak: a long gig on the real engine (see soak.cpp). Not part of ctest:
# it runs for minutes to hours; tools\soak.ps1 (Windows) and tools/soak.sh
# (Linux) run it.
add_executable(gigchain_soak soak.cpp)
target_link_libraries(gigchain_soak PRIVATE gigchain::engine Qt6::Core $<$<BOOL:${WIN32}>:psapi>)
target_include_directories(gigchain_soak PRIVATE ${PROJECT_SOURCE_DIR}/tests/common)
gigchain_target_defaults(gigchain_soak)
