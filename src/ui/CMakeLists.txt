set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(gigchain_ui STATIC)
qt_add_qml_module(gigchain_ui
    URI GigChain.Ui
    VERSION 1.0
    SOURCES
        cpp/DocumentController.h cpp/DocumentController.cpp
        cpp/SetlistModel.h cpp/SetlistModel.cpp
        cpp/ChannelModel.h cpp/ChannelModel.cpp
        cpp/SelectedChannel.h cpp/SelectedChannel.cpp
        cpp/PluginListModel.h cpp/PluginListModel.cpp
        cpp/EngineStatus.h cpp/EngineStatus.cpp
        cpp/Notifications.h cpp/Notifications.cpp
        cpp/SettingsController.h cpp/SettingsController.cpp
        cpp/StartupProgress.h cpp/StartupProgress.cpp
        cpp/StageQuips.h cpp/StageQuips.cpp
        cpp/FreezeWatchdog.h cpp/FreezeWatchdog.cpp
        cpp/CrashReports.h cpp/CrashReports.cpp
        cpp/BrandingInfo.h
        cpp/SettingsMigration.h cpp/SettingsMigration.cpp
        cpp/Session.h cpp/Session.cpp
        cpp/EditorService.h cpp/EditorService.cpp
        cpp/EffectWindows.h cpp/EffectWindows.cpp
        cpp/MasterBus.h cpp/MasterBus.cpp
        cpp/PluginEditorHost.h cpp/PluginEditorHost.cpp
        cpp/PluginIcons.h cpp/PluginIcons.cpp
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
        CardButton.qml
        ChartPanel.qml
        ChartView.qml
        MainArea.qml
        StartScreen.qml
        LoadingOverlay.qml
        DetailRow.qml
        Splash.qml
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
        NotificationWindow.qml
        ChannelZoneDialog.qml
        KnobDialog.qml
        StageButton.qml
        StageDivider.qml
        StagePanel.qml
        StageDialog.qml
        StageTabButton.qml
        StageTextField.qml
        StageSpinBox.qml
        StageSplitHandle.qml
        KeyboardView.qml
        WindowControls.qml
        WindowResizeEdges.qml
        SectionHeader.qml
    RESOURCES
        icons/adjustments-horizontal.svg
        icons/alert-octagon.svg
        icons/bell.svg
        icons/chevron-down.svg
        icons/chevron-left.svg
        icons/chevron-right.svg
        icons/chevron-up.svg
        icons/search.svg
        icons/info-circle.svg
        icons/metronome.svg
        icons/player-pause.svg
        icons/player-play.svg
        icons/player-stop.svg
        icons/player-skip-back.svg
        icons/redo.svg
        icons/star.svg
        icons/star-filled.svg
        icons/undo.svg
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
target_include_directories(gigchain_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp)
target_link_libraries(gigchain_ui PUBLIC
    gigchain::core gigchain::engine Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
target_link_libraries(gigchain_ui PRIVATE psapi dbghelp) # RAM readout; crash dumps (MiniDumpWriteDump)
gigchain_target_defaults(gigchain_ui)

# Qt's generated QML-to-C++ files (qmlcachegen) trip MSVC's code-generation
# warning C4702 "unreachable code" inside Qt headers; /external:W0 cannot
# silence code-generation warnings. Only those generated files get /wd4702;
# every hand-written file keeps /W4 /WX in full.
# Branding pictures (branding.cmake), always at qrc:/branding/...
set_source_files_properties(${PRODUCT_SPLASH_IMAGE} PROPERTIES QT_RESOURCE_ALIAS splash.png)
qt_add_resources(gigchain_ui "branding" PREFIX "/branding" FILES ${PRODUCT_SPLASH_IMAGE})

get_target_property(_gigchain_ui_sources gigchain_ui SOURCES)
foreach(_source IN LISTS _gigchain_ui_sources)
    if(_source MATCHES "/[.]rcc/qmlcache/")
        set_source_files_properties(${_source} TARGET_DIRECTORY gigchain_ui PROPERTIES COMPILE_OPTIONS "/wd4702")
    endif()
endforeach()
