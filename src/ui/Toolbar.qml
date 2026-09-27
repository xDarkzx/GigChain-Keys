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

    signal toggleKeyboard()
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
    background: StagePanel { bar: true }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
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

        Label {
            text: bar.doc.hasPatch ? bar.doc.currentSongName + "  ·  " + bar.doc.currentPatchName : ""
            color: Theme.text
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Backing track of the song: rewind, play/pause, where it is.
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
                enabled: bar.engineStatus.trackLoaded
                tip: qsTr("Back to the start of the backing track")
                onClicked: bar.engineStatus.rewindTrack()
            }
            StageButton {
                objectName: "trackPlayButton"
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
            text: qsTr("Settings")
            tip: qsTr("Audio, MIDI, pedals and plugins (Ctrl+,)")
            onClicked: bar.settingsRequested()
        }
    }
}
