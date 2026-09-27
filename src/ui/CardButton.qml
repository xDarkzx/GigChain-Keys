import QtQuick
import QtQuick.Controls

// A small round icon button on a card (favourite star, details): a raised
// disc with an outline, lit when active.
Rectangle {
    id: button

    property string iconSource
    property string activeIconSource: iconSource // e.g. a filled star when on
    property bool active: false
    property string tip
    signal clicked()

    implicitWidth: 26
    implicitHeight: 26
    radius: 13
    border.color: area.containsMouse || button.active ? Theme.outline : "transparent"
    gradient: Gradient {
        GradientStop { position: 0.0; color: area.containsMouse ? Theme.buttonHoverTop : (button.active ? Theme.buttonTop : "transparent") }
        GradientStop { position: 1.0; color: area.containsMouse ? Theme.buttonHoverBottom : (button.active ? Theme.buttonBottom : "transparent") }
    }

    Image {
        anchors.centerIn: parent
        source: button.active ? button.activeIconSource : button.iconSource
        sourceSize: Qt.size(16, 16)
        opacity: button.active || area.containsMouse ? 1.0 : 0.7
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: button.clicked()
    }
    ToolTip.visible: area.containsMouse && button.tip !== ""
    ToolTip.text: button.tip
    ToolTip.delay: 500
}
