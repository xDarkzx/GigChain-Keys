import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Logic-style mixer along the bottom: one strip per channel of the
// current patch, then the master strip. Shown in Edit and Perform mode.
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
                height: Math.min(ListView.view.height, Theme.stripHeight)
                doc: mixer.doc
                pluginModel: mixer.pluginModel
            }
            footer: Item {
                width: Theme.stripWidth + 8
                height: Math.min(strips.height, Theme.stripHeight)
                // Add a channel: pick an instrument (grouped by maker)
                EffectSlot {
                    id: newChannelSlot
                    x: 4
                    width: Theme.stripWidth
                    height: parent.height
                    text: ""
                    onClicked: newChannelPicker.popup(newChannelSlot, newChannelSlot.width / 2, newChannelSlot.height / 2)
                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: 18
                        text: qsTr("Instrument")
                        color: Theme.textDim
                        font.pixelSize: Theme.smallFontSize
                    }
                }
                InstrumentPickerMenu {
                    id: newChannelPicker
                    pluginModel: mixer.pluginModel
                    onPicked: (pluginId, name) => mixer.doc.addChannel(pluginId, name)
                }
            }
        }

        // Master strip
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: Math.min(strips.height, Theme.stripHeight)
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
