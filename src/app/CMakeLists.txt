qt_add_executable(gigchain_app WIN32 main.cpp)
# The .exe is named in branding.cmake.
set_target_properties(gigchain_app PROPERTIES OUTPUT_NAME ${PRODUCT_EXECUTABLE})
# Its icon, from branding.cmake.
if(WIN32)
    if(NOT EXISTS "${PRODUCT_ICON}")
        message(FATAL_ERROR "The app's icon ${PRODUCT_ICON} is missing: make it with tools/make-icon.ps1")
    endif()
    configure_file(app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    target_sources(gigchain_app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/app.rc APPEND PROPERTY OBJECT_DEPENDS "${PRODUCT_ICON}")
endif()
target_link_libraries(gigchain_app PRIVATE gigchain_ui gigchain_uiplugin Qt6::Gui Qt6::Quick Qt6::QuickControls2)
gigchain_target_defaults(gigchain_app)
# The app reads new plugins through the scanner, built next to it.
add_dependencies(gigchain_app gigchain_scanner)
