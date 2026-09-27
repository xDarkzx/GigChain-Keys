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
    required property bool sidePanelOpen
    required property bool mixerOpen
    required property bool keyboardOpen
    required property LoopController loops

    signal toggleKeyboard()
    signal loopControlsRequested()
    signal toggleMode()
    signal toggleSidePanel()
    signal toggleMixer()
    signal newRequested()
    signal openRequested()
    signal openRecentRequested(string path)
    signal saveRequested()
    signal saveAsRequested()
    signal settingsRequested()

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

        // Edit / Perform: one segmented switch.
        Row {
            spacing: -1
            StageButton {
                text: qsTr("Edit")
                checked: !bar.performMode
                onClicked: if (bar.performMode) bar.toggleMode()
            }
            StageButton {
                objectName: "performButton"
                text: qsTr("Perform")
                checked: bar.performMode
                onClicked: if (!bar.performMode) bar.toggleMode()
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

        StageDivider { vertical: true; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 8; visible: !bar.performMode }

        StageButton {
            objectName: "undoButton"
            iconSource: "icons/undo.svg"
            visible: !bar.performMode
            enabled: bar.doc.canUndo
            tip: qsTr("Undo (Ctrl+Z)")
            onClicked: bar.doc.undo()
        }
        StageButton {
            objectName: "redoButton"
            iconSource: "icons/redo.svg"
            visible: !bar.performMode
            enabled: bar.doc.canRedo
            tip: qsTr("Redo (Ctrl+Shift+Z)")
            onClicked: bar.doc.redo()
        }

        // The window's title: the setlist (a dot while unsaved), then where we are.
        Text {
            objectName: "windowTitle"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            textFormat: Text.StyledText
            color: Theme.text
            font.pixelSize: Theme.fontSize
            function escaped(s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
            text: {
                const file = "<font color='" + Theme.textDim + "'>" + (bar.doc.dirty ? "● " : "") + escaped(bar.doc.displayName) + "</font>"
                const where = bar.doc.hasPatch ? "&nbsp;&nbsp;—&nbsp;&nbsp;<b>" + escaped(bar.doc.currentSongName) + "  ·  "
                                                 + escaped(bar.doc.currentPatchName) + "</b>" : ""
                return bar.doc.hasSetlist ? file + where : ""
            }
        }

        // A song with sections: Play/Stop its count (the backing track goes
        // with it), and where it is.
        Row {
            objectName: "songTransport"
            visible: bar.doc.currentSections.length > 0
            spacing: -1
            StageButton {
                objectName: "songPlayButton"
                iconSource: bar.engineStatus.songPlaying ? "icons/player-stop.svg" : "icons/player-play.svg"
                checked: bar.engineStatus.songPlaying
                tip: bar.engineStatus.songPlaying ? qsTr("Stop the song")
                                                  : qsTr("Play the song from the section lit in the chart: its instruments change by themselves at each section")
                onClicked: bar.engineStatus.songPlaying ? bar.doc.stopSong() : bar.doc.playSong()
            }
            Rectangle {
                width: 170
                height: Theme.controlHeight
                radius: Theme.radiusSmall
                color: Theme.readoutBackground
                border.color: Theme.outline
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
                    text: parent.section === null ? ""
                          : bar.engineStatus.songCountingIn ? qsTr("Count-in…")
                          : bar.engineStatus.songPlaying ? qsTr("%1 · %2/%3").arg(parent.section.name).arg(bar.engineStatus.songBar).arg(bar.engineStatus.songBars)
                          : parent.section.name
                    color: bar.engineStatus.songPlaying ? Theme.chord : Theme.readoutText
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
            visible: bar.doc.songBackingTrack !== ""
            spacing: -1
            function clock(seconds) {
                const s = Math.max(0, Math.floor(seconds))
                return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
            }
            StageButton {
                iconSource: "icons/player-skip-back.svg"
                visible: bar.doc.currentSections.length === 0
                enabled: bar.engineStatus.trackLoaded
                tip: qsTr("Back to the start of the backing track")
                onClicked: bar.engineStatus.rewindTrack()
            }
            StageButton {
                objectName: "trackPlayButton"
                visible: bar.doc.currentSections.length === 0
                iconSource: bar.engineStatus.trackPlaying ? "icons/player-pause.svg" : "icons/player-play.svg"
                checked: bar.engineStatus.trackPlaying
                enabled: bar.engineStatus.trackLoaded
                tip: qsTr("Play or pause the backing track (%1)").arg(bar.doc.songBackingTrack)
                onClicked: bar.engineStatus.playPauseTrack()
            }
            // Where it is: a recessed display.
            Rectangle {
                width: 96
                height: Theme.controlHeight
                radius: Theme.radiusSmall
                color: Theme.readoutBackground
                border.color: Theme.outline
                Text {
                    anchors.centerIn: parent
                    text: bar.engineStatus.trackLoading ? qsTr("Reading…")
                                                        : transport.clock(bar.engineStatus.trackPosition) + " / "
                                                          + transport.clock(bar.engineStatus.trackLength)
                    color: Theme.readoutText
                    font.pixelSize: Theme.smallFontSize
                    font.family: "Consolas"
                }
            }
        }

        // Tempo: the number (click to type one), TAP it in, and the click.
        Row {
            spacing: -1
            // A recessed display; click it, type a new tempo, Enter (Esc keeps it).
            Rectangle {
                id: tempoField
                objectName: "tempoField"
                width: 74
                height: Theme.controlHeight
                radius: Theme.radiusSmall
                color: Theme.readoutBackground
                border.color: tempoInput.visible ? Theme.accent : Theme.outline
                readonly property string shown: bar.engineStatus.tempo.toFixed(bar.engineStatus.tempo % 1 === 0 ? 0 : 1)
                Row {
                    anchors.centerIn: parent
                    spacing: 4
                    visible: !tempoInput.visible
                    Text {
                        text: tempoField.shown
                        color: Theme.readoutText
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                        font.family: "Consolas"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("BPM")
                        color: Theme.textDim
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

        StatBox {
            objectName: "cpuBox"
            label: qsTr("CPU")
            value: Math.round(bar.engineStatus.cpuLoad * 100) + "%"
            widest: "100%"
            valueColor: bar.engineStatus.cpuLoad > 0.8 ? Theme.danger : Theme.text
        }
        StatBox {
            objectName: "ramBox"
            // plugin RAM matters live: sample libraries can take gigabytes
            label: qsTr("RAM")
            value: bar.engineStatus.memoryMb >= 1024 ? (bar.engineStatus.memoryMb / 1024).toFixed(1) + " GB"
                                                     : bar.engineStatus.memoryMb + " MB"
            widest: "1023 MB"
        }
        // MIDI light: a lit LED set into the panel.
        Row {
            spacing: 6
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
                radius: 5
                border.color: Theme.outline
                color: bar.engineStatus.midiActivity ? Theme.meterLow : "#2a2e35"
            }
            Label { text: qsTr("MIDI"); color: Theme.textDim; anchors.verticalCenter: parent.verticalCenter }
        }

        StageDivider { vertical: true; Layout.fillHeight: true; Layout.topMargin: 8; Layout.bottomMargin: 8 }

        StageButton {
            objectName: "keyboardButton"
            text: qsTr("Keys")
            iconSource: "icons/keyboard.svg"
            checkable: true
            checked: bar.keyboardOpen
            tip: qsTr("Show or hide the keyboard: the keys light up as you play")
            onClicked: bar.toggleKeyboard()
        }
        StageButton {
            text: qsTr("Mixer")
            iconSource: "icons/adjustments-horizontal.svg"
            visible: !bar.performMode
            checkable: true
            checked: bar.mixerOpen
            onClicked: bar.toggleMixer()
        }
        StageButton {
            objectName: "settingsButton"
            text: qsTr("Settings")
            tip: qsTr("Audio, MIDI, pedals and plugins (Ctrl+,)")
            onClicked: bar.settingsRequested()
        }

        // Minimise, maximise, close: at the right end, as on Windows.
        WindowControls {
            visible: bar.Window.window !== null && bar.Window.window.visibility !== Window.FullScreen
            Layout.leftMargin: Theme.spacing
        }
    }
}
