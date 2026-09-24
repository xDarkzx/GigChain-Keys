import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One Logic-style channel strip.
Rectangle {
    id: strip

    required property int index
    required property string name
    required property string instrumentName
    required property var effectNames
    required property double volumeDb
    required property double pan
    required property bool mute
    required property bool solo
    required property real peak
    required property bool selected
    required property string icon
    required property bool officialIcon
    required property string color
    required property DocumentController doc
    required property PluginListModel pluginModel

    readonly property real peakDb: peak > 0 ? 20 * Math.log10(peak) : -200

    width: Theme.stripWidth
    radius: Theme.radius
    color: selected ? Theme.stripSelected : Theme.stripBackground
    border.color: selected ? Theme.accent : Theme.stripBorder
    border.width: selected ? 2 : 1

    TapHandler { onTapped: strip.doc.selectedChannel = strip.index }

    DropArea {
        anchors.fill: parent
        keys: ["effect"]
        function acceptDrop(payload) { strip.doc.addEffect(strip.index, payload.pluginId, payload.name) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 3

        // colour tag
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 3
            radius: 1.5
            color: strip.color
        }

        // instrument icon
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: 17
            color: Qt.darker(strip.color, 2.2)
            border.color: strip.color
            clip: true
            Image {
                anchors.centerIn: parent
                source: strip.icon
                // the maker's icon fills the circle; category icons sit inside it
                width: strip.officialIcon ? 32 : 20
                height: width
                sourceSize: Qt.size(64, 64)
                fillMode: Image.PreserveAspectCrop
                smooth: true
            }
        }

        // instrument slot
        SlotButton {
            Layout.fillWidth: true
            text: strip.instrumentName === "" ? qsTr("Instrument") : strip.instrumentName
            accentColor: strip.color
            primary: true
            onClicked: strip.doc.selectedChannel = strip.index
        }

        // effect slots
        Repeater {
            model: strip.effectNames
            delegate: SlotButton {
                id: effectSlot
                required property int index
                required property string modelData
                Layout.fillWidth: true
                text: modelData
                onClicked: effectMenu.popup()
                Menu {
                    id: effectMenu
                    MenuItem { text: qsTr("Remove %1").arg(effectSlot.modelData); onTriggered: strip.doc.removeEffect(strip.index, effectSlot.index) }
                }
            }
        }
        SlotButton {
            Layout.fillWidth: true
            text: "+"
            empty: true
            onClicked: addMenu.popup()
            Menu {
                id: addMenu
                Instantiator {
                    model: strip.pluginModel.effects()
                    delegate: MenuItem {
                        required property var modelData
                        text: modelData.name
                        onTriggered: strip.doc.addEffect(strip.index, modelData.pluginId, modelData.name)
                    }
                    onObjectAdded: (index, object) => addMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => addMenu.removeItem(object)
                }
            }
        }

        PanKnob {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 2
            value: strip.pan
            onPanMoved: (v) => strip.doc.setChannelPan(strip.index, v)
        }

        // volume and peak readouts
        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            Readout { Layout.fillWidth: true; text: strip.volumeDb.toFixed(1) }
            Readout {
                Layout.fillWidth: true
                text: strip.peakDb < -99 ? "-∞" : strip.peakDb.toFixed(1)
                alarm: strip.peakDb > 0
            }
        }

        VolumeFader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 90
            volumeDb: strip.volumeDb
            level: strip.peak
            onVolumeMoved: (db) => strip.doc.setChannelVolume(strip.index, db)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 3
            ToggleChip {
                Layout.fillWidth: true
                text: "M"
                active: strip.mute
                activeColor: Theme.muteColor
                onClicked: strip.doc.setChannelMute(strip.index, !strip.mute)
            }
            ToggleChip {
                Layout.fillWidth: true
                text: "S"
                active: strip.solo
                activeColor: Theme.soloColor
                onClicked: strip.doc.setChannelSolo(strip.index, !strip.solo)
            }
        }

        // name tag
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            radius: 3
            color: strip.color
            Text {
                anchors.fill: parent
                anchors.margins: 3
                text: strip.name
                color: "white"
                font.pixelSize: Theme.smallFontSize
                font.bold: true
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
