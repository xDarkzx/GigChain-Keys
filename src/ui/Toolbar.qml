pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The top bar: file and mode on the left, undo, where we are in the middle,
// the song's backing track, tempo and click, then the rig's state.
ToolBar {
    id: bar

    required property DocumentController doc
    required property EngineStatus engineStatus
    required property bool performMode
    required property bool practiceMode
    required property bool sidePanelOpen
    required property bool mixerOpen
    required property bool keyboardOpen
    required property LoopController loops

    signal toggleKeyboard()
    signal loopControlsRequested()
    signal toggleMode()
    signal editRequested()
    signal practiceRequested()
    signal toggleSidePanel()
    signal toggleMixer()
    signal newRequested()
    signal openRequested()
    signal openRecentRequested(string path)
    signal saveRequested()
    signal saveAsRequested()
    signal settingsRequested()
    signal helpRequested(string topic) // "": the page for what is on screen
    signal aboutRequested()
    signal freeInstrumentsRequested()

    implicitHeight: 48
    // This bar is the window's title bar: drag it to move the window,
    // double-click it to maximise or restore (there is no Windows frame).
    background: StagePanel {
        bar: true
        DragHandler {
            target: null
            onActiveChanged: if (active) bar.Window.window.startSystemMove()
        }
        TapHandler {
            onDoubleTapped: {
                const w = bar.Window.window
                if (w.visibility === Window.Maximized) w.showNormal()
                else if (w.visibility !== Window.FullScreen) w.showMaximized()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.spacing

        StageButton {
            iconSource: bar.sidePanelOpen ? "icons/chevron-left.svg" : "icons/chevron-right.svg"
            tip: bar.sidePanelOpen ? qsTr("Hide the side panel") : qsTr("Show the side panel")
            onClicked: bar.toggleSidePanel()
        }
        StageButton {
            text: qsTr("File")
            visible: !bar.performMode
            onClicked: fileMenu.popup(0, height)
            StageMenu {
                id: fileMenu
                StageMenuItem { text: qsTr("New"); onTriggered: bar.newRequested() }
                StageMenuItem { text: qsTr("Open…"); onTriggered: bar.openRequested() }
                StageMenu {
                    id: recentMenu
                    title: qsTr("Recent setlists")
                    enabled: bar.doc.recentFiles.length > 0
                    Instantiator {
                        model: bar.doc.recentFiles
                        delegate: StageMenuItem {
                            required property string modelData
                            text: modelData.split(/[\\/]/).pop()
                            ToolTip.visible: hovered
                            ToolTip.text: modelData
                            onTriggered: bar.openRecentRequested(modelData)
                        }
                        onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => recentMenu.removeItem(object)
                    }
                }
                StageMenuItem { text: qsTr("Save"); onTriggered: bar.saveRequested() }
                StageMenuItem { text: qsTr("Save As…"); onTriggered: bar.saveAsRequested() }
            }
        }

        // The loop pedal's menu: everything beyond the buttons over the mixer.
        StageButton {
            objectName: "loopsMenuButton"
            text: qsTr("Loops")
            iconSource: "icons/loop.svg"
            onClicked: loopsMenu.popup(0, height)
            StageMenu {
                id: loopsMenu
                objectName: "loopsMenu"
                StageMenuItem {
                    objectName: "loopsShowStrip"
                    text: qsTr("Show the looper strip")
                    checkable: true
                    checked: bar.loops.stripVisible
                    onTriggered: bar.loops.stripVisible = !bar.loops.stripVisible
                }
                StageMenuItem {
                    objectName: "loopsSync"
                    text: qsTr("Loops follow the tempo (bars)")
                    checkable: true
                    checked: bar.doc.songLoopSync
                    enabled: bar.doc.hasPatch
                    onTriggered: bar.doc.setSongLoopSync(bar.doc.songIndex, !bar.doc.songLoopSync)
                }
                // How long a loop is: it closes by itself at the end.
                StageMenu {
                    id: loopLengthMenu
                    objectName: "loopLengthMenu"
                    title: bar.doc.songLoopBars === 1 ? qsTr("Loop length: 1 bar")
                           : bar.doc.songLoopBars > 1 ? qsTr("Loop length: %1 bars").arg(bar.doc.songLoopBars)
                           : qsTr("Loop length: open")
                    enabled: bar.doc.hasPatch && bar.doc.songLoopSync
                    Instantiator {
                        model: [1, 2, 4, 8, 16, 0]
                        delegate: StageMenuItem {
                            id: lengthItem
                            required property int modelData
                            text: lengthItem.modelData === 1 ? qsTr("1 bar")
                                  : lengthItem.modelData > 1 ? qsTr("%1 bars").arg(lengthItem.modelData)
                                  : qsTr("Open: closes where I stop it")
                            checkable: true
                            checked: bar.doc.songLoopBars === lengthItem.modelData
                            onTriggered: bar.doc.setSongLoopBars(bar.doc.songIndex, lengthItem.modelData)
                        }
                        onObjectAdded: (index, object) => loopLengthMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => loopLengthMenu.removeItem(object)
                    }
                }
                StageMenuItem {
                    text: qsTr("Free loops: the first sets the tempo")
                    checkable: true
                    checked: bar.loops.tempoFromFirstLoop
                    enabled: !bar.doc.songLoopSync
                    onTriggered: bar.loops.tempoFromFirstLoop = !bar.loops.tempoFromFirstLoop
                }
                MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
                StageMenuItem {
                    objectName: "loopsStopAll"
                    text: qsTr("Stop all loops")
                    enabled: bar.loops.loopCount > 0
                    onTriggered: bar.loops.stopAll()
                }
                StageMenuItem {
                    objectName: "loopsClearAll"
                    text: qsTr("Clear all loops")
                    enabled: bar.loops.loopCount > 0
                    onTriggered: bar.loops.clearAll()
                }
                StageMenuItem {
                    text: qsTr("Undo last layer (selected channel)")
                    enabled: bar.doc.selectedChannel >= 0
                    onTriggered: bar.loops.undo(bar.doc.selectedChannel)
                }
                MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
                StageMenuItem {
                    objectName: "loopsLearn"
                    text: qsTr("Keyboard controls…")
                    onTriggered: bar.loopControlsRequested()
                }
            }
        }
        // What the loops are doing, while the looper strip is out of sight.
        LoopsPill {
            objectName: "loopsPill"
            loops: bar.loops
            visible: bar.loops.loopCount > 0 && (!bar.mixerOpen || !bar.loops.stripVisible)
        }

        StageDivider { vertical: true; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 8 }

        // Edit / Perform / Practice: one segmented switch.
        Row {
            spacing: -1
            StageButton {
                objectName: "editButton"
                text: qsTr("Edit")
                checked: !bar.performMode && !bar.practiceMode
                onClicked: bar.editRequested()
            }
            StageButton {
                objectName: "performButton"
                text: qsTr("Perform")
                checked: bar.performMode
                onClicked: if (!bar.performMode) bar.toggleMode()
            }
            StageButton {
                objectName: "practiceButton"
                text: qsTr("Practice")
                checked: bar.practiceMode
                tip: qsTr("Practise the song: its chords fall onto a keyboard")
                onClicked: bar.practiceRequested()
            }
        }

        StageButton {
            objectName: "panicButton"
            text: qsTr("Panic")
            iconSource: "icons/alert-octagon.svg"
            tone: "danger"
            tip: qsTr("Stop every sound now (stuck notes, runaway effects)")
            onClicked: bar.engineStatus.panic()
        }
        // Recording the performance: what the audience hears, to a WAV.
        StageButton {
            objectName: "recordButton"
            text: bar.engineStatus.recording ? qsTr("● Stop rec") : qsTr("● Rec")
            tone: bar.engineStatus.recording ? "danger" : ""
            checked: bar.engineStatus.recording
            tip: bar.engineStatus.recording ? qsTr("Stop recording (it is saved in your Music folder)")
                                            : qsTr("Record what the audience hears to a WAV file in your Music folder")
            onClicked: bar.engineStatus.toggleRecording()
        }

        StageDivider { vertical: true; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 8; visible: !bar.performMode }

        StageButton {
            objectName: "undoButton"
            iconSource: "icons/undo.svg"
            visible: !bar.performMode
            enabled: bar.doc.canUndo
            tip: qsTr("Undo (%1)").arg(Theme.keys("Ctrl+Z"))
            onClicked: bar.doc.undo()
        }
        StageButton {
            objectName: "redoButton"
            iconSource: "icons/redo.svg"
            visible: !bar.performMode
            enabled: bar.doc.canRedo
            tip: qsTr("Redo (%1)").arg(Theme.keys("Ctrl+Shift+Z"))
            onClicked: bar.doc.redo()
        }

        // The window's title, as MainStage's display: an LCD in the middle of
        // the bar with the song and patch playing, the setlist (a dot while
        // unsaved) above them.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            LcdPanel {
                id: titleLcd
                anchors.centerIn: parent
                width: Math.min(parent.width - 4, 460)
                height: 40
                visible: bar.doc.hasSetlist && width > 80
                Text {
                    id: setlistLine
                    objectName: "windowSetlist"
                    x: 10
                    y: 3
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: (bar.doc.dirty ? "● " : "") + bar.doc.displayName
                    color: Theme.lcdTextDim
                    font.pixelSize: 10
                }
                Text {
                    objectName: "windowTitle"
                    x: 10
                    anchors.top: setlistLine.bottom
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    textFormat: Text.StyledText
                    function escaped(s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
                    text: bar.doc.hasPatch ? "<b>" + escaped(bar.doc.currentSongName) + "</b>  <font color='" + Theme.lcdAccent + "'>"
                                             + escaped(bar.doc.currentPatchName) + "</font>" : ""
                    color: Theme.lcdText
                    font.pixelSize: 15
                }
            }
        }

        // A song with sections: Play/Stop its count (the backing track goes
        // with it), and where it is.
        Row {
            objectName: "songTransport"
            visible: bar.doc.canPlaySong
            spacing: -1
            StageButton {
                objectName: "songPlayButton"
                iconSource: bar.engineStatus.songPlaying ? "icons/player-stop.svg" : "icons/player-play.svg"
                checked: bar.engineStatus.songPlaying
                tip: bar.engineStatus.songPlaying ? qsTr("Stop the song (Space)")
                                                  : qsTr("Play the song at its tempo along its flow (Space): its instruments change by themselves at each part. "
                                                         + "N next part, Shift+N repeat it, H hold it, Shift+Space stop at its end")
                onClicked: bar.engineStatus.songPlaying ? bar.doc.stopSong() : bar.doc.playSong()
            }
            LcdPanel {
                width: 160
                height: Theme.controlHeight
                readonly property var section: bar.engineStatus.songSection >= 0
                                               && bar.engineStatus.songSection < bar.doc.currentSections.length
                                               ? bar.doc.currentSections[bar.engineStatus.songSection] : null
                Text {
                    objectName: "songWhere"
                    anchors.fill: parent
                    anchors.margins: 6
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    // (A song without section titles: one part, the whole song.)
                    text: parent.section === null && bar.engineStatus.songPlaying
                            ? qsTr("Song · %1/%2").arg(bar.engineStatus.songBar).arg(bar.engineStatus.songBars)
                              + (bar.engineStatus.songQueued !== "" ? "  " + bar.engineStatus.songQueued : "")
                          : parent.section === null ? (bar.doc.canPlaySong ? qsTr("Song") : "")
                          : bar.engineStatus.songCountingIn ? qsTr("Count-in…")
                          : bar.engineStatus.songPlaying ? qsTr("%1 · %2/%3").arg(parent.section.name).arg(bar.engineStatus.songBar).arg(bar.engineStatus.songBars)
                                                           + (bar.engineStatus.songQueued !== "" ? "  " + bar.engineStatus.songQueued : "")
                          : parent.section.name
                    color: bar.engineStatus.songPlaying ? Theme.chord : Theme.lcdText
                    font.pixelSize: Theme.smallFontSize
                    font.bold: true
                    font.family: "Consolas"
                }
            }
        }

        // Backing track of the song: rewind, play/pause, where it is (a song
        // with sections plays it from its own Play).
        Row {
            id: transport
            visible: bar.doc.songBackingTrack !== "" || bar.doc.songStems.length > 0
            spacing: -1
            function clock(seconds) {
                const s = Math.max(0, Math.floor(seconds))
                return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
            }
            StageButton {
                iconSource: "icons/player-skip-back.svg"
                visible: !bar.doc.canPlaySong
                enabled: bar.engineStatus.trackLoaded
                tip: qsTr("Back to the start of the backing track")
                onClicked: bar.engineStatus.rewindTrack()
            }
            StageButton {
                objectName: "trackPlayButton"
                visible: !bar.doc.canPlaySong
                iconSource: bar.engineStatus.trackPlaying ? "icons/player-pause.svg" : "icons/player-play.svg"
                checked: bar.engineStatus.trackPlaying
                enabled: bar.engineStatus.trackLoaded
                tip: qsTr("Play or pause the backing track (%1)").arg(bar.doc.songBackingTrack)
                onClicked: bar.engineStatus.playPauseTrack()
            }
            // Where it is: a recessed display.
            LcdPanel {
                width: 96
                height: Theme.controlHeight
                Text {
                    anchors.centerIn: parent
                    text: bar.engineStatus.trackLoading ? qsTr("Reading…")
                                                        : transport.clock(bar.engineStatus.trackPosition) + " / "
                                                          + transport.clock(bar.engineStatus.trackLength)
                    color: Theme.lcdText
                    font.pixelSize: Theme.smallFontSize
                    font.family: "Consolas"
                }
            }
            // Markers: jump to a marked place; mark where it is now; the song's stems.
            StageButton {
                id: markerButton
                objectName: "markerButton"
                iconSource: "icons/flag.svg"
                enabled: bar.engineStatus.trackLoaded
                tip: bar.doc.songMarkers.length > 0 ? qsTr("Markers: jump to a marked place in the track")
                                                    : qsTr("Mark places in the track to jump to (Verse 2, Outro…)")
                onClicked: markerMenu.popup(markerButton, 0, markerButton.height)
            }
            StageMenu {
                id: markerMenu
                objectName: "markerMenu"
                Instantiator {
                    model: bar.doc.songMarkers
                    delegate: StageMenuItem {
                        required property var modelData
                        text: modelData.name + "  " + transport.clock(modelData.seconds)
                        onTriggered: bar.engineStatus.seekTrack(modelData.seconds)
                    }
                    onObjectAdded: (i, object) => markerMenu.insertItem(i, object)
                    onObjectRemoved: (i, object) => markerMenu.removeItem(object)
                }
                MenuSeparator {
                    visible: bar.doc.songMarkers.length > 0
                    contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder }
                }
                StageMenuItem {
                    text: qsTr("Add Marker at %1…").arg(transport.clock(bar.engineStatus.trackPosition))
                    onTriggered: {
                        markerName.at = bar.engineStatus.trackPosition
                        markerName.text = qsTr("Marker %1").arg(bar.doc.songMarkers.length + 1)
                        markerNameDialog.open()
                    }
                }
                StageMenu {
                    id: removeMarkerMenu
                    title: qsTr("Remove Marker")
                    enabled: bar.doc.songMarkers.length > 0
                    Instantiator {
                        model: bar.doc.songMarkers
                        delegate: StageMenuItem {
                            required property var modelData
                            required property int index
                            text: modelData.name + "  " + transport.clock(modelData.seconds)
                            onTriggered: bar.doc.removeSongMarker(bar.doc.songIndex, index)
                        }
                        onObjectAdded: (i, object) => removeMarkerMenu.insertItem(i, object)
                        onObjectRemoved: (i, object) => removeMarkerMenu.removeItem(object)
                    }
                }
            }
            StageDialog {
                id: markerNameDialog
                objectName: "markerNameDialog"
                title: qsTr("Marker at %1").arg(transport.clock(markerName.at))
                width: 320
                onOpened: {
                    markerName.forceActiveFocus()
                    markerName.selectAll()
                }
                function add() {
                    if (bar.doc.addSongMarker(bar.doc.songIndex, markerName.text, markerName.at)) close()
                }
                ColumnLayout {
                    width: parent.width
                    spacing: 10
                    StageTextField {
                        id: markerName
                        objectName: "markerName"
                        property real at: 0
                        Layout.fillWidth: true
                        Layout.margins: 20
                        Layout.bottomMargin: 0
                        placeholderText: qsTr("Chorus 2, Outro…")
                        onAccepted: markerNameDialog.add()
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Layout.margins: 20
                        Layout.topMargin: 0
                        StageButton { text: qsTr("Cancel"); onClicked: markerNameDialog.close() }
                        StageButton { text: qsTr("Add"); onClicked: markerNameDialog.add() }
                    }
                }
            }
        }

        // Tempo: the number (click to type one), TAP it in, and the click.
        Row {
            spacing: -1
            // A recessed display; click it, type a new tempo, Enter (Esc keeps it).
            LcdPanel {
                id: tempoField
                objectName: "tempoField"
                width: 74
                height: Theme.controlHeight
                border.color: tempoInput.visible ? Theme.accent : "#000000"
                readonly property string shown: bar.engineStatus.tempo.toFixed(bar.engineStatus.tempo % 1 === 0 ? 0 : 1)
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    visible: !tempoInput.visible
                    Text {
                        text: tempoField.shown
                        color: Theme.lcdText
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                        font.family: "Consolas"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("BPM")
                        color: Theme.lcdTextDim
                        font.pixelSize: Theme.tinyFontSize
                    }
                }
                TextInput {
                    id: tempoInput
                    anchors.fill: parent
                    anchors.margins: 3
                    visible: false
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.readoutText
                    selectionColor: Theme.accent
                    selectedTextColor: "white"
                    font.pixelSize: Theme.fontSize
                    font.family: "Consolas"
                    selectByMouse: true
                    validator: DoubleValidator { bottom: 20; top: 400; decimals: 1 }
                    onAccepted: {
                        visible = false
                        bar.engineStatus.setTempo(parseFloat(text))
                    }
                    Keys.onEscapePressed: visible = false
                    onActiveFocusChanged: if (!activeFocus) visible = false
                }
                HoverHandler { id: tempoHover; cursorShape: Qt.IBeamCursor }
                TapHandler {
                    enabled: !tempoInput.visible
                    onTapped: {
                        tempoInput.text = tempoField.shown
                        tempoInput.visible = true
                        tempoInput.forceActiveFocus()
                        tempoInput.selectAll()
                    }
                }
                ToolTip.visible: tempoHover.hovered && !tempoInput.visible
                ToolTip.text: qsTr("Tempo for arpeggiators, delays and the click: click to type one. Songs can have their own (right-click a song).")
            }
            StageButton {
                objectName: "tapButton"
                text: qsTr("Tap")
                tip: qsTr("Tap along: the tempo follows your taps")
                onPressed: bar.engineStatus.tapTempo()
            }
            StageButton {
                objectName: "clickButton"
                iconSource: "icons/metronome.svg"
                checkable: true
                checked: bar.engineStatus.clickOn
                tip: qsTr("A click on every beat")
                onClicked: bar.engineStatus.clickOn = checked
            }
        }

        StageDivider { vertical: true; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 8 }

        // The rig's state on one display: CPU, plugin memory, and the MIDI light.
        LcdPanel {
            implicitWidth: stats.implicitWidth + 16
            implicitHeight: Theme.controlHeight
        Row {
            id: stats
            anchors.centerIn: parent
            spacing: 10
        StatBox {
            objectName: "cpuBox"
            anchors.verticalCenter: parent.verticalCenter
            label: qsTr("CPU")
            value: Math.round(bar.engineStatus.cpuLoad * 100) + "%"
            widest: "100%"
            labelColor: Theme.lcdTextDim
            valueColor: bar.engineStatus.cpuLoad > 0.8 ? Theme.ledRed : Theme.lcdText
        }
        StatBox {
            objectName: "ramBox"
            anchors.verticalCenter: parent.verticalCenter
            // plugin RAM matters live: sample libraries can take gigabytes
            label: qsTr("RAM")
            value: bar.engineStatus.memoryMb >= 1024 ? (bar.engineStatus.memoryMb / 1024).toFixed(1) + " GB"
                                                     : bar.engineStatus.memoryMb + " MB"
            widest: "1023 MB"
            labelColor: Theme.lcdTextDim
            valueColor: Theme.lcdText
        }
        // MIDI light: an LED, lit (and glowing) while notes come in.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 9
                height: 9
                radius: 4.5
                border.color: "#000000"
                color: bar.engineStatus.midiActivity ? Theme.ledGreen : Theme.ledOff
                Rectangle {
                    visible: bar.engineStatus.midiActivity
                    anchors.centerIn: parent
                    width: 17; height: 17; radius: 8.5
                    color: "transparent"
                    border.color: Qt.rgba(0.26, 0.83, 0.42, 0.35)
                    border.width: 3
                }
            }
            Text { text: qsTr("MIDI"); color: Theme.lcdTextDim; font.pixelSize: Theme.tinyFontSize; anchors.verticalCenter: parent.verticalCenter }
        }
        }
        }

        StageDivider { vertical: true; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 8 }

        StageButton {
            objectName: "keyboardButton"
            visible: !bar.practiceMode // (Practice has its own)
            iconSource: "icons/keyboard.svg"
            checkable: true
            checked: bar.keyboardOpen
            tip: qsTr("Show or hide the keyboard: the keys light up as you play")
            onClicked: bar.toggleKeyboard()
        }
        // How the sound's instruments play: all together (layers), or the
        // selected strip alone. Lit while one at a time.
        StageButton {
            objectName: "playModeButton"
            visible: !bar.practiceMode
            readonly property bool one: bar.doc.playMode === 1
            iconSource: one ? "icons/stack-single.svg" : "icons/stack-2.svg"
            checked: one
            tip: one ? qsTr("One instrument at a time: only the selected strip plays (click a strip to switch). Click for all together")
                     : qsTr("All instruments together (layers): every strip plays on every note. Click for one at a time")
            onClicked: bar.doc.setPlayMode(one ? 0 : 1)
        }
        StageButton {
            iconSource: "icons/adjustments-horizontal.svg"
            visible: !bar.performMode && !bar.practiceMode
            checkable: true
            checked: bar.mixerOpen
            tip: qsTr("Show or hide the mixer")
            onClicked: bar.toggleMixer()
        }
        StageButton {
            objectName: "settingsButton"
            iconSource: "icons/settings.svg"
            tip: qsTr("Settings: audio, MIDI, pedals and plugins (%1)").arg(Theme.keys("Ctrl+,"))
            onClicked: bar.settingsRequested()
        }
        // Help: the user guide, the shortcuts, about the app.
        StageButton {
            objectName: "helpButton"
            iconSource: "icons/info-circle.svg"
            tip: qsTr("Help: the user guide (%1)").arg(Theme.keys("F1"))
            onClicked: helpMenu.popup(0, height)
            StageMenu {
                id: helpMenu
                StageMenuItem { text: qsTr("User guide"); onTriggered: bar.helpRequested("") }
                StageMenuItem { text: qsTr("Getting started"); onTriggered: bar.helpRequested("getting-started") }
                StageMenuItem { text: qsTr("How Practice works"); onTriggered: bar.helpRequested("practice") }
                StageMenuItem { text: qsTr("Keyboard shortcuts"); onTriggered: bar.helpRequested("shortcuts") }
                StageMenuItem { text: qsTr("Troubleshooting"); onTriggered: bar.helpRequested("troubleshooting") }
                MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
                StageMenuItem {
                    objectName: "freeInstrumentsItem"
                    text: qsTr("Get free instruments…")
                    onTriggered: bar.freeInstrumentsRequested()
                }
                StageMenuItem { text: qsTr("About %1").arg(Branding.name); onTriggered: bar.aboutRequested() }
            }
        }

        // Minimise, maximise, close: at the right end, as on Windows.
        WindowControls {
            visible: bar.Window.window !== null && bar.Window.window.visibility !== Window.FullScreen
            Layout.leftMargin: Theme.spacing
        }
    }
}
