option(GIGCHAIN_ASAN "Build our targets with AddressSanitizer" OFF)

# Adds AddressSanitizer flags to one of our own targets when GIGCHAIN_ASAN is ON.
function(gigchain_sanitize target)
    if(GIGCHAIN_ASAN AND MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target} PRIVATE /DEBUG /INCREMENTAL:NO)
    endif()
endfunction()
