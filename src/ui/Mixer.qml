import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Logic-style mixer along the bottom: one strip per channel of the current
// patch, then the master strip.
Rectangle {
    id: mixer

    required property DocumentController doc
    required property ChannelModel channelModel
    required property PluginListModel pluginModel
    required property EngineStatus engineStatus

    color: Theme.mixerBackground

    DropArea {
        anchors.fill: parent
        keys: ["instrument"]
        function acceptDrop(payload) { mixer.doc.addChannel(payload.pluginId, payload.name) }
    }

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        ListView {
            id: strips
            objectName: "mixerStrips"
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            spacing: 4
            clip: true
            model: mixer.channelModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar {}
            delegate: ChannelStrip {
                height: ListView.view.height
                doc: mixer.doc
                pluginModel: mixer.pluginModel
            }
            footer: Item {
                width: addStrip.width + 8
                height: strips.height
                // Add a channel: pick an instrument
                SlotButton {
                    id: addStrip
                    x: 4
                    width: Theme.stripWidth
                    height: strips.height
                    empty: true
                    text: qsTr("+ Instrument")
                    onClicked: instrumentMenu.popup()
                    Menu {
                        id: instrumentMenu
                        Instantiator {
                            model: mixer.pluginModel.instruments()
                            delegate: MenuItem {
                                required property var modelData
                                text: modelData.name
                                onTriggered: mixer.doc.addChannel(modelData.pluginId, modelData.name)
                            }
                            onObjectAdded: (index, object) => instrumentMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => instrumentMenu.removeItem(object)
                        }
                    }
                }
            }
        }

        // Master strip
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.stripWidth
            radius: Theme.radius
            color: Theme.stripBackground
            border.color: Theme.stripBorder

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 3
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: Theme.text }
                Item { Layout.fillWidth: true; Layout.preferredHeight: 34
                    Image {
                        anchors.centerIn: parent
                        source: "icons/volume.svg"
                        sourceSize: Qt.size(22, 22)
                    }
                }
                Readout { Layout.fillWidth: true; text: mixer.engineStatus.masterVolumeDb.toFixed(1) }
                VolumeFader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    volumeDb: mixer.engineStatus.masterVolumeDb
                    onVolumeMoved: (db) => mixer.engineStatus.masterVolumeDb = db
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    radius: 3
                    color: Theme.panelRaised
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Master")
                        color: Theme.text
                        font.pixelSize: Theme.smallFontSize
                        font.bold: true
                    }
                }
            }
        }
    }
}
