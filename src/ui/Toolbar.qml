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

        Label {
            text: bar.doc.hasPatch ? bar.doc.currentSongName + "  ·  " + bar.doc.currentPatchName : ""
            color: Theme.textDim
            elide: Text.ElideRight
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
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
