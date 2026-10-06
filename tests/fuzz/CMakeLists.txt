# Fuzzers (libFuzzer, with AddressSanitizer): each feeds generated input to
# one reader of outside data and checks it never crashes, hangs or breaks
# its promises. Built by the "fuzz" preset only.
#   ctest --preset fuzz        replays every corpus (a regression test)
#   tools\fuzz.ps1             fuzzes for a while and keeps what it finds
#
# corpus/<name>/ holds the seeds and every input found that crashed once
# (kept, so the crash stays fixed).
function(gigchain_add_fuzzer name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_LIBS})
    gigchain_target_defaults(${name})
    if(MSVC)
        target_compile_options(${name} PRIVATE /fsanitize=fuzzer)
        target_link_options(${name} PRIVATE /fsanitize=fuzzer)
    else() # Clang (libFuzzer is Clang's; the linux-fuzz preset)
        target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    endif()
    # On Linux without address randomisation: Clang 15's sanitizer runtime
    # crashes at random at start-up on kernels randomising with more than 28
    # bits (vm.mmap_rnd_bits = 32 in WSL's 6.6 kernel; fixed in LLVM 18).
    # Measured: a crash every few runs with it, none in 80 runs without.
    set(_launcher "")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_launcher setarch ${CMAKE_SYSTEM_PROCESSOR} -R)
    endif()
    add_test(NAME ${name}
             COMMAND ${_launcher} $<TARGET_FILE:${name}> -runs=0 ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endfunction()

gigchain_add_fuzzer(fuzz_setlist_json SOURCES fuzz_setlist_json.cpp LIBS gigchain::core)
gigchain_add_fuzzer(fuzz_chart SOURCES fuzz_chart.cpp LIBS gigchain::core)
gigchain_add_fuzzer(fuzz_plugin_state SOURCES fuzz_plugin_state.cpp LIBS gigchain::engine vst3_host_support)
target_include_directories(fuzz_plugin_state PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/internal)
gigchain_add_fuzzer(fuzz_plugin_files SOURCES fuzz_plugin_files.cpp LIBS gigchain::engine)
target_include_directories(fuzz_plugin_files PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/internal)
gigchain_add_fuzzer(fuzz_midi SOURCES fuzz_midi.cpp LIBS gigchain::engine)
target_include_directories(fuzz_midi PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/internal)
