import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One mixer strip: instrument, effect slots ("+" to add, drop an effect here),
// meter, fader, mute/solo and name. Click to select.
Rectangle {
    id: strip

    required property int index
    required property string name
    required property string instrumentName
    required property var effectNames
    required property double volumeDb
    required property bool mute
    required property bool solo
    required property real peak
    required property bool selected
    required property DocumentController doc
    required property PluginListModel pluginModel

    width: Theme.stripWidth
    radius: Theme.radius
    color: selected ? Theme.selection : Theme.panelRaised
    border.color: selected ? Theme.accent : Theme.border

    TapHandler { onTapped: strip.doc.selectedChannel = strip.index }

    DropArea {
        anchors.fill: parent
        keys: ["effect"]
        function acceptDrop(payload) { strip.doc.addEffect(strip.index, payload.pluginId, payload.name) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        Button {
            Layout.fillWidth: true
            text: strip.instrumentName === "" ? qsTr("(none)") : strip.instrumentName
            font.pixelSize: Theme.smallFontSize
            focusPolicy: Qt.NoFocus
            highlighted: true
            onClicked: strip.doc.selectedChannel = strip.index
            ToolTip.visible: hovered
            ToolTip.text: text
        }

        Repeater {
            model: strip.effectNames
            delegate: Button {
                required property int index
                required property string modelData
                Layout.fillWidth: true
                text: modelData
                font.pixelSize: Theme.smallFontSize
                focusPolicy: Qt.NoFocus
                onClicked: effectMenu.popup()
                Menu {
                    id: effectMenu
                    MenuItem { text: qsTr("Remove"); onTriggered: strip.doc.removeEffect(strip.index, index) }
                }
            }
        }

        Button {
            Layout.fillWidth: true
            text: "+"
            focusPolicy: Qt.NoFocus
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

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            LevelMeter {
                Layout.fillHeight: true
                Layout.preferredWidth: 8
                level: strip.peak
            }
            Slider {
                Layout.fillHeight: true
                Layout.fillWidth: true
                orientation: Qt.Vertical
                from: -60
                to: 12
                value: strip.volumeDb
                focusPolicy: Qt.NoFocus
                onMoved: strip.doc.setChannelVolume(strip.index, value)
            }
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: strip.volumeDb.toFixed(1) + " dB"
            font.pixelSize: Theme.smallFontSize
            color: Theme.textDim
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Button {
                Layout.fillWidth: true
                text: "M"
                highlighted: strip.mute
                focusPolicy: Qt.NoFocus
                onClicked: strip.doc.setChannelMute(strip.index, !strip.mute)
            }
            Button {
                Layout.fillWidth: true
                text: "S"
                highlighted: strip.solo
                focusPolicy: Qt.NoFocus
                onClicked: strip.doc.setChannelSolo(strip.index, !strip.solo)
            }
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: strip.name
            elide: Text.ElideRight
            font.bold: true
        }
    }
}
