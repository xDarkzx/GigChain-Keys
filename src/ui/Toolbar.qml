import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: bar

    required property DocumentController doc
    required property EngineStatus engineStatus
    required property bool performMode
    required property bool sidePanelOpen
    required property bool mixerOpen

    signal toggleMode()
    signal toggleSidePanel()
    signal toggleMixer()
    signal newRequested()
    signal openRequested()
    signal openRecentRequested(string path)
    signal saveRequested()
    signal saveAsRequested()
    signal settingsRequested()

    background: Rectangle { color: Theme.panelRaised }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
        spacing: Theme.spacing

        ToolButton {
            text: bar.sidePanelOpen ? "◀" : "▶"
            focusPolicy: Qt.NoFocus
            onClicked: bar.toggleSidePanel()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Show or hide the side panel")
        }
        ToolButton {
            text: qsTr("File")
            visible: !bar.performMode
            focusPolicy: Qt.NoFocus
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

        Rectangle { width: 1; Layout.fillHeight: true; Layout.margins: 6; color: Theme.border }

        Button {
            text: qsTr("Edit")
            highlighted: !bar.performMode
            focusPolicy: Qt.NoFocus
            onClicked: if (bar.performMode) bar.toggleMode()
        }
        Button {
            objectName: "performButton"
            text: qsTr("Perform")
            highlighted: bar.performMode
            focusPolicy: Qt.NoFocus
            onClicked: if (!bar.performMode) bar.toggleMode()
        }

        Button {
            objectName: "panicButton"
            text: qsTr("Panic")
            focusPolicy: Qt.NoFocus
            palette.button: Theme.danger
            palette.buttonText: "white"
            onClicked: bar.engineStatus.panic()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Stop every sound now (stuck notes, runaway effects)")
        }

        ToolButton {
            objectName: "undoButton"
            text: "↶"
            visible: !bar.performMode
            enabled: bar.doc.canUndo
            focusPolicy: Qt.NoFocus
            onClicked: bar.doc.undo()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Undo (Ctrl+Z)")
        }
        ToolButton {
            objectName: "redoButton"
            text: "↷"
            visible: !bar.performMode
            enabled: bar.doc.canRedo
            focusPolicy: Qt.NoFocus
            onClicked: bar.doc.redo()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Redo (Ctrl+Shift+Z)")
        }

        Label {
            text: bar.doc.hasPatch ? bar.doc.currentSongName + "  ·  " + bar.doc.currentPatchName : ""
            color: Theme.textDim
            elide: Text.ElideRight
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Backing track of the song: rewind, play/pause, where it is.
        RowLayout {
            visible: bar.doc.songBackingTrack !== ""
            spacing: 2
            function clock(seconds) {
                const s = Math.max(0, Math.floor(seconds))
                return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
            }
            ToolButton {
                text: "⏮"
                focusPolicy: Qt.NoFocus
                enabled: bar.engineStatus.trackLoaded
                onClicked: bar.engineStatus.rewindTrack()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Back to the start of the backing track")
            }
            ToolButton {
                objectName: "trackPlayButton"
                text: bar.engineStatus.trackPlaying ? "❚❚" : "▶"
                focusPolicy: Qt.NoFocus
                enabled: bar.engineStatus.trackLoaded
                onClicked: bar.engineStatus.playPauseTrack()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Play or pause the backing track (%1)").arg(bar.doc.songBackingTrack)
            }
            Label {
                text: bar.engineStatus.trackLoading ? qsTr("Reading…")
                                                    : parent.clock(bar.engineStatus.trackPosition) + " / " + parent.clock(bar.engineStatus.trackLength)
                color: Theme.textDim
                font.pixelSize: Theme.smallFontSize
            }
        }

        // Tempo: the number (type a new one), TAP it in, and the click.
        RowLayout {
            spacing: 2
            // Shows the tempo; click it, type a new one, Enter (Esc keeps it).
            Rectangle {
                id: tempoField
                objectName: "tempoField"
                implicitWidth: 52
                implicitHeight: 26
                radius: Theme.radius
                color: Theme.readoutBackground
                border.color: tempoInput.visible ? Theme.accentBlue : (tempoHover.hovered ? Theme.border : "transparent")
                readonly property string shown: bar.engineStatus.tempo.toFixed(bar.engineStatus.tempo % 1 === 0 ? 0 : 1)
                Text {
                    anchors.centerIn: parent
                    visible: !tempoInput.visible
                    text: tempoField.shown
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                }
                TextInput {
                    id: tempoInput
                    anchors.fill: parent
                    anchors.margins: 3
                    visible: false
                    horizontalAlignment: TextInput.AlignHCenter
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.text
                    selectionColor: Theme.accentBlue
                    selectedTextColor: "white"
                    font.pixelSize: Theme.fontSize
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
            Label { text: qsTr("BPM"); color: Theme.textDim; font.pixelSize: Theme.smallFontSize }
            ToolButton {
                objectName: "tapButton"
                text: qsTr("TAP")
                focusPolicy: Qt.NoFocus
                onPressed: bar.engineStatus.tapTempo()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Tap along: the tempo follows your taps")
            }
            ToolButton {
                objectName: "clickButton"
                text: qsTr("Click")
                checkable: true
                checked: bar.engineStatus.clickOn
                focusPolicy: Qt.NoFocus
                onClicked: bar.engineStatus.clickOn = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("A click on every beat")
            }
        }

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
        Rectangle {
            width: 10
            height: 10
            radius: 5
            color: bar.engineStatus.midiActivity ? Theme.meterLow : Theme.border
        }
        Label { text: qsTr("MIDI"); color: Theme.textDim }
        ToolButton {
            text: qsTr("Mixer")
            visible: !bar.performMode
            checkable: true
            checked: bar.mixerOpen
            focusPolicy: Qt.NoFocus
            onClicked: bar.toggleMixer()
        }
        ToolButton {
            text: qsTr("Settings")
            focusPolicy: Qt.NoFocus
            onClicked: bar.settingsRequested()
        }
    }
}
