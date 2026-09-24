set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

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
        cpp/SettingsController.h cpp/SettingsController.cpp
        cpp/Session.h cpp/Session.cpp
        cpp/EditorService.h cpp/EditorService.cpp
        cpp/PluginEditorHost.h cpp/PluginEditorHost.cpp
        cpp/PluginIcons.h cpp/PluginIcons.cpp
        cpp/EditorPlacement.h cpp/EditorPlacement.cpp
        cpp/OfficialArtwork.h cpp/OfficialArtwork.cpp
    QML_FILES
        Theme.qml
        Main.qml
        Toolbar.qml
        SidePanel.qml
        SetlistView.qml
        DragSource.qml
        PluginBrowser.qml
        PluginArea.qml
        ChannelStrip.qml
        Mixer.qml
        PanKnob.qml
        VolumeFader.qml
        StageMenu.qml
        StageComboBox.qml
        StatBox.qml
        SettingsDialog.qml
        SettingsSection.qml
        SettingsRow.qml
        StageMenuItem.qml
        EffectPickerMenu.qml
        PluginGroupMenu.qml
        InstrumentPickerMenu.qml
        EffectSlot.qml
        Readout.qml
        ToggleChip.qml
        PerformView.qml
    RESOURCES
        icons/adjustments-horizontal.svg
        icons/bell.svg
        icons/guitar-pick.svg
        icons/keyboard.svg
        icons/microphone.svg
        icons/music.svg
        icons/piano.svg
        icons/plus.svg
        icons/ripple.svg
        icons/vinyl.svg
        icons/volume.svg
        icons/wave-saw-tool.svg
        icons/wave-sine.svg
        icons/wave-square.svg
        icons/x.svg
)
target_include_directories(openstage_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp)
target_link_libraries(openstage_ui PUBLIC
    openstage::core openstage::engine Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
target_link_libraries(openstage_ui PRIVATE psapi) # GetProcessMemoryInfo (RAM readout)
openstage_target_defaults(openstage_ui)

# Qt's generated QML-to-C++ files (qmlcachegen) trip MSVC's code-generation
# warning C4702 "unreachable code" inside Qt headers; /external:W0 cannot
# silence code-generation warnings. Only those generated files get /wd4702;
# every hand-written file keeps /W4 /WX in full.
get_target_property(_openstage_ui_sources openstage_ui SOURCES)
foreach(_source IN LISTS _openstage_ui_sources)
    if(_source MATCHES "/[.]rcc/qmlcache/")
        set_source_files_properties(${_source} TARGET_DIRECTORY openstage_ui PROPERTIES COMPILE_OPTIONS "/wd4702")
    endif()
endforeach()
