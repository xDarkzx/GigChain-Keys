include(Sanitizers)

# Warnings-as-errors and exploit mitigations for our own code.
# Third-party headers come in through imported targets, which CMake treats as
# SYSTEM includes, so /WX never fires on Qt or tl-expected.
function(gigchain_harden target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /WX /permissive- /sdl /utf-8 /Zc:__cplusplus /external:W0
            # "padded due to alignment": always on purpose here (cache-line
            # separation in the real-time queues).
            /wd4324)
        target_link_options(${target} PRIVATE
            /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /INCREMENTAL:NO)
        if(NOT GIGCHAIN_ASAN)
            target_compile_options(${target} PRIVATE /guard:cf)
            target_link_options(${target} PRIVATE /guard:cf)
        endif()
    else()
        # GCC and Clang: the same strictness everywhere, and a stack protector.
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            # Designated initializers leaving members at their defaults are
            # this code's style.
            -Wno-missing-field-initializers
            -fstack-protector-strong)
        if(NOT APPLE)
            # Linux: fortified library calls, stack-clash protection,
            # read-only relocations, no executable stack. (The Mac: Apple's
            # SDK fortifies library calls itself, and defining _FORTIFY_SOURCE
            # again is a redefinition error; its linker makes stacks
            # non-executable by default and has no -z options; stack-clash
            # protection is not offered for Apple targets.)
            target_compile_options(${target} PRIVATE
                -fstack-clash-protection
                $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
            target_link_options(${target} PRIVATE -Wl,-z,relro,-z,now -Wl,-z,noexecstack)
        endif()
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endfunction()

# Version details on an .exe (its Properties in Explorer), from branding.cmake.
function(gigchain_version_resource target description)
    if(WIN32)
        get_target_property(GC_FILE_NAME ${target} OUTPUT_NAME)
        set(GC_FILE_DESCRIPTION "${description}")
        configure_file(${PROJECT_SOURCE_DIR}/cmake/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/${target}-version.rc @ONLY)
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${target}-version.rc)
    endif()
endfunction()

# Call on every one of our targets.
function(gigchain_target_defaults target)
    gigchain_harden(${target})
    gigchain_sanitize(${target})
endfunction()
