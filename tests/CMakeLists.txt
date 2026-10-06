# gigchain_add_test(<name> SOURCES <files...> LIBS <targets...>)
# Builds one Qt Test executable and registers it with CTest. Tests find the Qt
# DLLs through PATH and render QML off-screen.
function(gigchain_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    if(WIN32) # no Windows error dialog may wait for a click in an unattended test
        target_sources(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests/common/NoErrorDialogs.cpp)
    endif()
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests/common)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    gigchain_target_defaults(${name})
    # Passes only on Qt Test's own summary (see RunQtTest.cmake).
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DTEST_EXE=$<TARGET_FILE:${name}>
                     -DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/${name}.log -P ${PROJECT_SOURCE_DIR}/tests/RunQtTest.cmake)
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen")
endfunction()

# Renaming the product must stay a one-file change (branding.cmake): neither
# the brand nor an earlier name may be typed into the source.
set(_previous_names "")
foreach(_pair IN LISTS PRODUCT_PREVIOUS_SETTINGS)
    string(REPLACE "/" ";" _parts "${_pair}")
    list(APPEND _previous_names ${_parts})
endforeach()
list(REMOVE_DUPLICATES _previous_names)
string(REPLACE ";" "\;" _previous_names "${_previous_names}")
add_test(NAME no_hardcoded_product_name
         COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} "-DBRAND=${PRODUCT_BRAND}"
                 "-DOLD_NAMES=${_previous_names}" -P ${PROJECT_SOURCE_DIR}/tests/NoHardcodedName.cmake)

# No system code outside the platform files (cmake/CheckPlatformBoundary.cmake).
add_test(NAME platform_boundary
         COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src -P ${PROJECT_SOURCE_DIR}/cmake/CheckPlatformBoundary.cmake)
# The third-party notices name the files this system's app really carries.
add_test(NAME notices_name_this_systems_files
         COMMAND ${CMAKE_COMMAND} -DNOTICES=${CMAKE_BINARY_DIR}/installer/THIRD-PARTY-NOTICES.txt
                 -DSYSTEM=${CMAKE_SYSTEM_NAME} -P ${PROJECT_SOURCE_DIR}/cmake/CheckNotices.cmake)

add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(ui)
add_subdirectory(soak)
