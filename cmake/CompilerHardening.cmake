include(Sanitizers)

# Warnings-as-errors and exploit mitigations for OpenStage's own code.
# Third-party headers come in through imported targets, which CMake treats as
# SYSTEM includes, so /WX never fires on Qt or tl-expected.
function(gigchain_harden target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /WX /permissive- /sdl /utf-8 /Zc:__cplusplus /external:W0)
        target_link_options(${target} PRIVATE
            /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /INCREMENTAL:NO)
        if(NOT GIGCHAIN_ASAN)
            target_compile_options(${target} PRIVATE /guard:cf)
            target_link_options(${target} PRIVATE /guard:cf)
        endif()
    endif()
endfunction()

# Call on every OpenStage target.
function(gigchain_target_defaults target)
    gigchain_harden(${target})
    gigchain_sanitize(${target})
endfunction()
