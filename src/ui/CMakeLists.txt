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
        cpp/LoopController.h cpp/LoopController.cpp
        cpp/Notifications.h cpp/Notifications.cpp
        cpp/SettingsController.h cpp/SettingsController.cpp
        cpp/KeyboardTeaching.h cpp/KeyboardTeaching.cpp
        cpp/PanelController.h cpp/PanelController.cpp
        cpp/InputPermission.h cpp/InputPermission.cpp
        cpp/PracticeController.h cpp/PracticeController.cpp
        cpp/WarmupController.h cpp/WarmupController.cpp
        cpp/HelpLibrary.h cpp/HelpLibrary.cpp
        cpp/StartupProgress.h cpp/StartupProgress.cpp
        cpp/StageQuips.h cpp/StageQuips.cpp
        cpp/FreezeWatchdog.h cpp/FreezeWatchdog.cpp
        cpp/CrashReports.h cpp/CrashReports.cpp
        cpp/BrandingInfo.h
        cpp/SettingsMigration.h cpp/SettingsMigration.cpp
        cpp/Session.h cpp/Session.cpp
        cpp/SingleInstance.h cpp/SingleInstance.cpp
        cpp/EditorService.h cpp/EditorService.cpp
        cpp/EffectWindows.h cpp/EffectWindows.cpp
        cpp/InstrumentWindows.h cpp/InstrumentWindows.cpp
        cpp/MasterBus.h cpp/MasterBus.cpp
        cpp/MixerLayout.h cpp/MixerLayout.cpp
        cpp/SustainPadsController.h cpp/SustainPadsController.cpp
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
        PayloadDropArea.qml
        PluginBrowser.qml
        PluginArea.qml
        InstrumentWindow.qml
        InstrumentWindowList.qml
        MixerWindow.qml
        ChannelStrip.qml
        Mixer.qml
        PanKnob.qml
        KnobFace.qml
        LcdPanel.qml
        StripSection.qml
        SendKnob.qml
        BusEffectList.qml
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
        ExternalGearDialog.qml
        StemsDialog.qml
        KnobDialog.qml
        KnobLearnMenu.qml
        WarmupBar.qml
        WarmupCard.qml
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
        ChartSegment.qml
        LiveChartLine.qml
        ChordDragSource.qml
        PracticeView.qml
        ChordDiagram.qml
        FlowBar.qml
        CountInOverlay.qml
        FreeInstruments.qml
        FreeInstrumentsDialog.qml
        HelpWindow.qml
        AboutDialog.qml
        LooperCell.qml
        LoopsPill.qml
        LoopControlsDialog.qml
        PanelBar.qml
        SawStackPanel.qml
        PianoProPanel.qml
        PadsProPanel.qml
        SoundFontPanel.qml
        PadXY.qml
        SustainPadsBar.qml
        SustainPadsWindow.qml
        SustainPadsStrip.qml
        BuiltinPanel.qml
        BuiltinKnob.qml
        BuiltinCard.qml
        BuiltinPill.qml
        BuiltinLayerRow.qml
        SidechainCard.qml
        EnhanceButton.qml
        BuiltinFxWindow.qml
    RESOURCES
        icons/adjustments-horizontal.svg
        icons/layout-grid.svg
        icons/alert-octagon.svg
        icons/bell.svg
        icons/chevron-down.svg
        icons/chevron-left.svg
        icons/chevron-right.svg
        icons/chevron-up.svg
        icons/flag.svg
        icons/search.svg
        icons/settings.svg
        icons/info-circle.svg
        icons/metronome.svg
        icons/player-pause.svg
        icons/player-play.svg
        icons/player-stop.svg
        icons/loop.svg
        icons/player-skip-back.svg
        icons/redo.svg
        icons/stack-2.svg
        icons/stack-single.svg
        icons/star.svg
        icons/star-filled.svg
        icons/undo.svg
        icons/guitar-pick.svg
        icons/keyboard.svg
        icons/link.svg
        icons/pin.svg
        icons/app-window.svg
        icons/layout-bottombar.svg
        icons/layout-sidebar.svg
        icons/layout-sidebar-right.svg
        icons/microphone.svg
        icons/music.svg
        icons/piano.svg
        icons/plus.svg
        icons/folder.svg
        icons/ripple.svg
        icons/vinyl.svg
        icons/volume.svg
        icons/wave-saw-tool.svg
        icons/wave-sine.svg
        icons/wave-square.svg
        icons/x.svg
        # Pads Pro's backgrounds, one for each pad (tools/make-pad-art.py).
        padart/01-juno.jpg
        padart/02-supersaw.jpg
        padart/03-moog.jpg
        padart/04-flute.jpg
        padart/05-fmbells.jpg
        padart/06-tapechoir.jpg
        padart/07-violins.jpg
        padart/08-tonewheel.jpg
        padart/09-angelic.jpg
        padart/10-piano.jpg
        padart/11-epiano.jpg
        padart/12-ebow.jpg
        padart/13-stringmachine.jpg
        padart/14-pipeorgan.jpg
        padart/15-harmonium.jpg
        padart/16-twin.jpg
        padart/17-vector.jpg
        padart/18-bowl.jpg
        padart/19-nebula.jpg
        padart/20-wind.jpg
        padart/21-ocean.jpg
        padart/22-forest.jpg
        padart/23-cave.jpg
        sawart/01-supersaw-lead.jpg
        sawart/02-trance-lead.jpg
        sawart/03-anthem-lead.jpg
        sawart/04-big-room-lead.jpg
        sawart/05-melodic-dubstep-lead.jpg
        sawart/06-hoover-lead.jpg
        sawart/07-silk-lead.jpg
        sawart/08-supersaw-chords.jpg
        sawart/09-future-bass-chords.jpg
        sawart/10-deep-house-chords.jpg
        sawart/11-gated-trance-chords.jpg
        sawart/12-synthwave-chords.jpg
        sawart/13-uplifting-chords.jpg
        sawart/14-supersaw-pluck.jpg
        sawart/15-future-pluck.jpg
        sawart/16-trance-pluck.jpg
        sawart/17-tropical-pluck.jpg
        sawart/18-rave-stab.jpg
        sawart/19-supersaw-pad.jpg
        sawart/20-dream-pad.jpg
        sawart/21-analog-pad.jpg
        sawart/22-swell-pad.jpg
        sawart/23-air-pad.jpg
        sawart/24-reese-bass.jpg
        sawart/25-saw-bass.jpg
        sawart/26-octave-bass.jpg
        sawart/27-hoover-bass.jpg
        sawart/28-pluck-bass.jpg
        pianoart/01-grand-piano.jpg
        pianoart/02-bright-grand.jpg
        pianoart/03-ballad-grand.jpg
        pianoart/04-upright-piano.jpg
        pianoart/05-felt-piano.jpg
        pianoart/06-electric-piano.jpg
        pianoart/07-suitcase-rhodes.jpg
        pianoart/08-wurlitzer.jpg
        pianoart/09-electric-grand.jpg
        pianoart/10-pianet.jpg
        pianoart/11-dx-piano.jpg
        pianoart/12-lofi-keys.jpg
        pianoart/13-poly-keys.jpg
        pianoart/14-poly-strings.jpg
        pianoart/15-poly-brass.jpg
        pianoart/16-house-piano.jpg
        pianoart/17-big-house-piano.jpg
        pianoart/18-prog-house-piano.jpg
        pianoart/19-emotional-piano.jpg
)
target_include_directories(gigchain_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp)
target_link_libraries(gigchain_ui PUBLIC
    gigchain::core gigchain::engine Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2)
gigchain_target_defaults(gigchain_ui)

# Qt's generated QML-to-C++ files (qmlcachegen) trip MSVC's code-generation
# warning C4702 "unreachable code" inside Qt headers; /external:W0 cannot
# silence code-generation warnings. Only those generated files get /wd4702;
# every hand-written file keeps /W4 /WX in full.
# Branding pictures (branding.cmake), always at qrc:/branding/...
set_source_files_properties(${PRODUCT_SPLASH_IMAGE} PROPERTIES QT_RESOURCE_ALIAS splash.png)
qt_add_resources(gigchain_ui "branding" PREFIX "/branding" FILES ${PRODUCT_SPLASH_IMAGE})

# The user guide (Help > User guide, HelpLibrary), at :/help/<page>.md.
file(GLOB _gigchain_help_pages CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/docs/help/*.md")
qt_add_resources(gigchain_ui "help" PREFIX "/help" BASE "${PROJECT_SOURCE_DIR}/docs/help" FILES ${_gigchain_help_pages})

if(MSVC)
    get_target_property(_gigchain_ui_sources gigchain_ui SOURCES)
    foreach(_source IN LISTS _gigchain_ui_sources)
        if(_source MATCHES "/[.]rcc/qmlcache/")
            set_source_files_properties(${_source} TARGET_DIRECTORY gigchain_ui PROPERTIES COMPILE_OPTIONS "/wd4702")
        endif()
    endforeach()
endif()
