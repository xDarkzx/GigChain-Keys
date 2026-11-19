# The live test control (docs/testing/live-test-control.md). Built only when
# GIGCHAIN_DEV_CONTROL is ON (the dev presets): never part of a release.
add_library(gigchain_devcontrol STATIC
    DevMarker.h DevMarker.cpp
    DevProtocol.h DevProtocol.cpp
    DevControl.h DevControl.cpp DevLog.h
    Reflection.h Reflection.cpp
    AppCommands.cpp
    UiCommands.cpp
)
set_target_properties(gigchain_devcontrol PROPERTIES AUTOMOC ON)
target_include_directories(gigchain_devcontrol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    # (the engine's own parts it drives: the plugin guard; a developer build only)
    PRIVATE ${PROJECT_SOURCE_DIR}/src/engine/internal)
target_link_libraries(gigchain_devcontrol PUBLIC gigchain_ui gigchain::core gigchain::engine
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick)
gigchain_target_defaults(gigchain_devcontrol)
