import QtQuick
import QtQuick.Controls

// A small round glyph button on a card (favourite ★, details ⓘ).
Rectangle {
    id: button

    property string glyph
    property bool active: false
    property string tip
    signal clicked()

    implicitWidth: 24
    implicitHeight: 24
    radius: 12
    color: area.containsMouse ? Theme.slotHover : "transparent"

    Text {
        anchors.centerIn: parent
        text: button.glyph
        color: button.active ? Theme.accent : Theme.textDim
        font.pixelSize: 15
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
