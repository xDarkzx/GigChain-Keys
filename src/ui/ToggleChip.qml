import QtQuick

// Mute / solo button: a small raised key, lit in its colour when on.
Rectangle {
    id: chip
    property string text
    property bool active: false
    property color activeColor: Theme.accent
    signal clicked()

    implicitHeight: 22
    radius: Theme.radiusSmall
    border.color: Theme.outline
    gradient: Gradient {
        GradientStop {
            position: 0.0
            color: chip.active ? Qt.lighter(chip.activeColor, 1.15) : (hover.hovered ? Theme.buttonHoverTop : Theme.buttonTop)
        }
        GradientStop {
            position: 1.0
            color: chip.active ? Qt.darker(chip.activeColor, 1.25) : (hover.hovered ? Theme.buttonHoverBottom : Theme.buttonBottom)
        }
    }
    // The lit top edge (a pressed-in key has none).
    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: chip.active ? "#50ffffff" : Theme.bevelLight }
    Text {
        anchors.centerIn: parent
        text: chip.text
        color: chip.active ? "#101114" : Theme.text
        font.pixelSize: Theme.smallFontSize
        font.bold: true
    }
    HoverHandler { id: hover }
    TapHandler { onTapped: chip.clicked() }
}
