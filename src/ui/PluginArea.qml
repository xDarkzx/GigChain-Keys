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

    DropArea {
        anchors.fill: parent
        keys: ["instrument"]
        function acceptDrop(payload) { area.doc.addChannel(payload.pluginId, payload.name) }
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
