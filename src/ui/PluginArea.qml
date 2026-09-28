import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The main area: the selected channel's own plugin window, as big as it can
// be (the toolbar already names the song and patch). The plugin does the
// heavy lifting here.
Rectangle {
    id: area

    required property DocumentController doc
    required property EditorService editorService
    property bool suspended: false

    color: Theme.background

    PayloadDropArea {
        anchors.fill: parent
        keys: ["instrument"]
        onPayloadDropped: (payload) => area.doc.addChannel(payload.pluginId, payload.name)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Shown only where no plugin window covers the area.
            Label {
                anchors.centerIn: parent
                visible: !editorHost.hasEditor
                text: editorHost.emptyReason
                color: Theme.textDim
                font.pixelSize: Theme.headerFontSize
            }

            PluginEditorHost {
                id: editorHost
                objectName: "pluginEditorHost"
                anchors.fill: parent
                service: area.editorService
                suspended: area.suspended
            }

        }
    }
}
