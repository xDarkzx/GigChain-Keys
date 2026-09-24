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

            // Plugins that cannot shrink (e.g. Arturia) scroll instead.
            ScrollBar {
                orientation: Qt.Horizontal
                policy: ScrollBar.AlwaysOn
                visible: editorHost.hasEditor && editorHost.scrollHorizontally
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: editorHost.scrollVertically ? 12 : 0
                anchors.bottom: parent.bottom
                height: 12
                size: editorHost.viewportWidth / Math.max(1, editorHost.contentWidth)
                position: editorHost.scrollX / Math.max(1, editorHost.contentWidth)
                onPositionChanged: if (pressed) editorHost.scrollX = position * editorHost.contentWidth
            }
            ScrollBar {
                orientation: Qt.Vertical
                policy: ScrollBar.AlwaysOn
                visible: editorHost.hasEditor && editorHost.scrollVertically
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.bottomMargin: editorHost.scrollHorizontally ? 12 : 0
                anchors.right: parent.right
                width: 12
                size: editorHost.viewportHeight / Math.max(1, editorHost.contentHeight)
                position: editorHost.scrollY / Math.max(1, editorHost.contentHeight)
                onPositionChanged: if (pressed) editorHost.scrollY = position * editorHost.contentHeight
            }
        }
    }
}
