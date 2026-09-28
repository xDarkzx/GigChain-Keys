import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Covers the window while plugins load, with what is loading and how far.
Popup {
    id: overlay

    required property StartupProgress progress

    objectName: "loadingOverlay"
    visible: progress.active
    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: 380
    padding: 18
    Overlay.modal: Rectangle { color: "#88000000" }
    background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 8 }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Label {
            text: overlay.progress.step
            font.bold: true
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 4
            radius: 2
            color: Theme.border
            Rectangle {
                height: parent.height
                radius: 2
                color: Theme.accentBlue
                width: parent.width * Math.max(0, overlay.progress.progress) // fills from the left
            }
        }
        Label {
            Layout.fillWidth: true
            text: overlay.progress.detail
            color: Theme.textDim
            font.pixelSize: Theme.smallFontSize
            elide: Text.ElideRight
        }
    }
}
