qt_add_executable(openstage WIN32 main.cpp)
target_link_libraries(openstage PRIVATE gigchain_ui gigchain_uiplugin Qt6::Gui Qt6::Quick Qt6::QuickControls2)
gigchain_target_defaults(openstage)
