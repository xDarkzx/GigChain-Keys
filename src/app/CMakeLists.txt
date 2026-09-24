qt_add_executable(gigchain_app WIN32 main.cpp)
# The .exe is named in branding.cmake.
set_target_properties(gigchain_app PROPERTIES OUTPUT_NAME ${PRODUCT_EXECUTABLE})
target_link_libraries(gigchain_app PRIVATE gigchain_ui gigchain_uiplugin Qt6::Gui Qt6::Quick Qt6::QuickControls2)
gigchain_target_defaults(gigchain_app)
