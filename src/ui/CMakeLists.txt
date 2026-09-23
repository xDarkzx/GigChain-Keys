qt_add_library(openstage_ui STATIC)
qt_add_qml_module(openstage_ui
    URI OpenStage.Ui
    VERSION 1.0
    SOURCES
        cpp/DocumentController.h cpp/DocumentController.cpp
        cpp/SetlistModel.h cpp/SetlistModel.cpp
        cpp/ChannelModel.h cpp/ChannelModel.cpp
        cpp/SelectedChannel.h cpp/SelectedChannel.cpp
        cpp/PluginListModel.h cpp/PluginListModel.cpp
        cpp/EngineStatus.h cpp/EngineStatus.cpp
)
target_include_directories(openstage_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp)
target_link_libraries(openstage_ui PUBLIC openstage::core openstage::engine Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
openstage_target_defaults(openstage_ui)
