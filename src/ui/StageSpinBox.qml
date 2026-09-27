import QtQuick
import QtQuick.Controls

// A number box: the value in a recessed display, with raised down / up
// buttons on either side.
SpinBox {
    id: box

    implicitHeight: Theme.controlHeight + 2
    implicitWidth: 210
    leftPadding: 32
    rightPadding: 32
    font.pixelSize: Theme.fontSize

    contentItem: TextInput {
        text: box.displayText
        font: box.font
        color: Theme.readoutText
        selectionColor: Theme.accent
        selectedTextColor: Theme.textOnAccent
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !box.editable
        validator: box.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.readoutBackground
        border.color: box.activeFocus ? Theme.accent : Theme.outline
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelDark }
    }

    down.indicator: Rectangle {
        x: 0
        width: 30
        height: box.height
        radius: Theme.radiusSmall
        border.color: Theme.outline
        opacity: box.value > box.from ? 1.0 : 0.45
        gradient: Gradient {
            GradientStop { position: 0.0; color: box.down.pressed ? Theme.buttonDownTop : (box.down.hovered ? Theme.buttonHoverTop : Theme.buttonTop) }
            GradientStop { position: 1.0; color: box.down.pressed ? Theme.buttonDownBottom : (box.down.hovered ? Theme.buttonHoverBottom : Theme.buttonBottom) }
        }
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelLight; visible: !box.down.pressed }
        Image {
            anchors.centerIn: parent
            source: "icons/chevron-down.svg"
            sourceSize: Qt.size(14, 14)
        }
    }

    up.indicator: Rectangle {
        x: box.width - width
        width: 30
        height: box.height
        radius: Theme.radiusSmall
        border.color: Theme.outline
        opacity: box.value < box.to ? 1.0 : 0.45
        gradient: Gradient {
            GradientStop { position: 0.0; color: box.up.pressed ? Theme.buttonDownTop : (box.up.hovered ? Theme.buttonHoverTop : Theme.buttonTop) }
            GradientStop { position: 1.0; color: box.up.pressed ? Theme.buttonDownBottom : (box.up.hovered ? Theme.buttonHoverBottom : Theme.buttonBottom) }
        }
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelLight; visible: !box.up.pressed }
        Image {
            anchors.centerIn: parent
            source: "icons/chevron-up.svg"
            sourceSize: Qt.size(14, 14)
        }
    }
}
