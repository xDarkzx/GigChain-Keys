option(GIGCHAIN_ASAN "Build our targets with AddressSanitizer" OFF)
option(GIGCHAIN_FUZZ "Build the fuzzers (tests/fuzz); needs GIGCHAIN_ASAN" OFF)
if(GIGCHAIN_FUZZ AND NOT GIGCHAIN_ASAN)
    message(FATAL_ERROR "GIGCHAIN_FUZZ needs GIGCHAIN_ASAN: use the \"fuzz\" preset")
endif()

# Adds AddressSanitizer flags to one of our own targets when GIGCHAIN_ASAN is
# ON, and with GIGCHAIN_FUZZ the coverage the fuzzers steer by (what
# /fsanitize=fuzzer adds, without its main()).
function(gigchain_sanitize target)
    if(GIGCHAIN_ASAN AND MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target} PRIVATE /DEBUG /INCREMENTAL:NO)
    endif()
    if(GIGCHAIN_ASAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(GIGCHAIN_FUZZ AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=fuzzer-no-link)
    endif()
    if(GIGCHAIN_FUZZ AND MSVC)
        target_compile_options(${target} PRIVATE /fsanitize-coverage=inline-8bit-counters /fsanitize-coverage=edge
                                                 /fsanitize-coverage=trace-cmp /fsanitize-coverage=trace-div)
    endif()
endfunction()
