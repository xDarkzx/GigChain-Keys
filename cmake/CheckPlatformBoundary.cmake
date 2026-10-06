# Fails when system code appears outside the platform files: files named
# *_win.*, *_posix.*, *_linux.* and *_mac.* may use it; nothing else in src/
# may.
#   cmake -DSOURCE_DIR=<repo>/src -P CheckPlatformBoundary.cmake
if(NOT IS_DIRECTORY "${SOURCE_DIR}")
    message(FATAL_ERROR "SOURCE_DIR must be the source folder (-DSOURCE_DIR=<repo>/src before -P); got '${SOURCE_DIR}'")
endif()
file(GLOB_RECURSE sources "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.mm")
set(offenders "")
set(checked 0)
foreach(source IN LISTS sources)
    get_filename_component(stem "${source}" NAME_WE)
    if(stem MATCHES "_(win|posix|linux|mac)$")
        continue()
    endif()
    math(EXPR checked "${checked} + 1")
    file(STRINGS "${source}" lines REGEX
        "#include <(windows|winsock2|psapi|dbghelp|timeapi|crtdbg|unistd|dlfcn|signal|execinfo|pthread)\\.h>|#include <(sys|X11|mach)/|_WIN32|__linux__|__APPLE__|Q_OS_MAC|TARGET_OS_|_MSC_VER|[^A-Za-z_]HWND[^A-Za-z_]")
    if(lines)
        string(REPLACE "${SOURCE_DIR}/" "" relative "${source}")
        list(APPEND offenders "${relative}: ${lines}")
    endif()
endforeach()
if(offenders)
    list(JOIN offenders "\n  " report)
    message(FATAL_ERROR "System code outside the platform files:\n  ${report}")
endif()
message(STATUS "platform boundary: ${checked} files checked, no system code outside the platform files")
