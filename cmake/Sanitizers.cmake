option(OPENSTAGE_ASAN "Build OpenStage targets with AddressSanitizer" OFF)

# Adds AddressSanitizer flags to one of our own targets when OPENSTAGE_ASAN is ON.
function(openstage_sanitize target)
    if(OPENSTAGE_ASAN AND MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target} PRIVATE /DEBUG /INCREMENTAL:NO)
    endif()
endfunction()
