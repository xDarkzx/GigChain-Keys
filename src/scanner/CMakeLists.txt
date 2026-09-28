# The plugin scanner: reads one plugin in a process of its own for the app's
# plugin scan (see main.cpp). Named after the app (branding.cmake).
qt_add_executable(gigchain_scanner main.cpp)
set_target_properties(gigchain_scanner PROPERTIES OUTPUT_NAME ${PRODUCT_EXECUTABLE}Scan)
target_include_directories(gigchain_scanner PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/internal)
target_link_libraries(gigchain_scanner PRIVATE gigchain::engine Qt6::Core)
target_compile_definitions(gigchain_scanner PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
gigchain_target_defaults(gigchain_scanner)
gigchain_version_resource(gigchain_scanner "${PRODUCT_NAME} plugin scanner")
