import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The right-hand mixer: one strip per channel of the current patch.
Rectangle {
    id: mixer

    required property DocumentController doc
    required property ChannelModel channelModel
    required property PluginListModel pluginModel

    color: Theme.panel

    DropArea {
        anchors.fill: parent
        keys: ["instrument"]
        function acceptDrop(payload) { mixer.doc.addChannel(payload.pluginId, payload.name) }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("MIXER"); color: Theme.textDim; font.bold: true; Layout.fillWidth: true }
            ToolButton {
                text: qsTr("Remove")
                enabled: mixer.doc.selectedChannel >= 0
                focusPolicy: Qt.NoFocus
                onClicked: mixer.doc.removeChannel(mixer.doc.selectedChannel)
            }
        }

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
        }

        Label {
            visible: strips.count === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textDim
            text: qsTr("Drag an instrument here to add a channel")
        }
    }
}
