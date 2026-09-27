import QtQuick
import QtQuick.Controls

// A tab: the chosen one is raised and underlined in blue; the others sit
// back, recessed into the bar.
TabButton {
    id: tab

    implicitHeight: 34
    focusPolicy: Qt.NoFocus
    font.pixelSize: Theme.fontSize
    hoverEnabled: true

    contentItem: Text {
        text: tab.text
        font.pixelSize: tab.font.pixelSize
        font.bold: tab.checked
        color: tab.checked ? Theme.text : (tab.hovered ? Theme.text : Theme.textDim)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        border.color: Theme.outline
        gradient: Gradient {
            GradientStop { position: 0.0; color: tab.checked ? Theme.buttonHoverTop : (tab.hovered ? Theme.buttonTop : Theme.barBottom) }
            GradientStop { position: 1.0; color: tab.checked ? Theme.buttonBottom : Theme.panelBottom }
        }
        // Lit top edge on the chosen tab.
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; visible: tab.checked; color: Theme.bevelLight }
        // The chosen tab's blue underline.
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 1
            height: 2
            visible: tab.checked
            color: Theme.accent
        }
    }
}
