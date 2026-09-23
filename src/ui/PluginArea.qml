import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The main area: current patch header, the selected channel's plugin (its own
// editor window arrives with sub-project 3), and a playable keyboard.
Rectangle {
    id: area

    required property DocumentController doc
    required property SelectedChannel selectedChannel
    required property EngineStatus engineStatus

    color: Theme.background

    DropArea {
        anchors.fill: parent
        keys: ["instrument"]
        function acceptDrop(payload) { area.doc.addChannel(payload.pluginId, payload.name) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Theme.panelRaised
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing * 2
                anchors.rightMargin: Theme.spacing * 2
                spacing: Theme.spacing * 2
                Label {
                    text: area.doc.hasPatch ? area.doc.currentPatchNumber : ""
                    color: Theme.accent
                    font.pixelSize: Theme.headerFontSize + 6
                    font.bold: true
                }
                Label {
                    text: area.doc.currentPatchName
                    font.pixelSize: Theme.headerFontSize
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: area.doc.currentSongName
                    color: Theme.textDim
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                objectName: "pluginEditorHost"
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 760)
                height: Math.min(parent.height - 48, 400)
                radius: Theme.radius * 2
                color: Theme.panel
                border.color: Theme.border

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spacing
                    width: parent.width - 48
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: Theme.headerFontSize + 4
                        font.bold: true
                        text: area.selectedChannel.valid
                              ? (area.selectedChannel.instrumentName || qsTr("No instrument"))
                              : qsTr("Drag an instrument here")
                    }
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.textDim
                        wrapMode: Text.WordWrap
                        text: area.selectedChannel.valid
                              ? (area.selectedChannel.effectNames.length > 0
                                 ? qsTr("Effects: %1").arg(area.selectedChannel.effectNames.join(" → "))
                                 : qsTr("No effects — drag one onto the channel strip"))
                                + "\n" + qsTr("The plugin's own window will appear here.")
                              : qsTr("Pick one from the Plugins tab on the left, or double-click it.")
                    }
                }
            }
        }

        OnScreenKeyboard {
            objectName: "onScreenKeyboard"
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            Layout.margins: Theme.spacing
            engineStatus: area.engineStatus
        }
    }
}
