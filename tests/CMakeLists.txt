# openstage_add_test(<name> SOURCES <files...> LIBS <targets...>)
# Builds one Qt Test executable and registers it with CTest. Tests find the Qt
# DLLs through PATH and render QML off-screen.
function(openstage_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests/common)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    openstage_target_defaults(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen")
endfunction()

add_subdirectory(core)
add_subdirectory(engine)
