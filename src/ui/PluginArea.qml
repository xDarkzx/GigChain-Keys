import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The main area: the current patch header and, below it, the selected
// channel's own plugin window. The plugin does the heavy lifting here.
Rectangle {
    id: area

    required property DocumentController doc
    required property EditorService editorService
    property bool suspended: false

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
                    text: editorHost.hasEditor ? editorHost.title : ""
                    color: Theme.textDim
                }
            }
        }

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
