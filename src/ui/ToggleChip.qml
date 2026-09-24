import QtQuick

// Mute / solo button: grey when off, lit in its colour when on.
Rectangle {
    id: chip
    property string text
    property bool active: false
    property color activeColor: Theme.accent
    signal clicked()

    implicitHeight: 20
    radius: 3
    color: active ? activeColor : (hover.hovered ? Theme.slotHover : Theme.slotBackground)
    Text {
        anchors.centerIn: parent
        text: chip.text
        color: chip.active ? Theme.accentText : Theme.text
        font.pixelSize: Theme.smallFontSize
        font.bold: true
    }
    HoverHandler { id: hover }
    TapHandler { onTapped: chip.clicked() }
}
