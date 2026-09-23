qt_add_executable(openstage WIN32 main.cpp)
target_link_libraries(openstage PRIVATE openstage_ui openstage_uiplugin Qt6::Gui Qt6::Quick Qt6::QuickControls2)
openstage_target_defaults(openstage)
