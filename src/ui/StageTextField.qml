import QtQuick
import QtQuick.Controls

// A text box: recessed into the panel (dark, shaded top edge), with a blue
// outline while typing. An optional icon on the left (e.g. search).
TextField {
    id: field

    property string iconSource: ""

    implicitHeight: Theme.controlHeight + 2
    leftPadding: iconSource !== "" ? 30 : 10
    rightPadding: 10
    color: Theme.text
    placeholderTextColor: Theme.textDim
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    font.pixelSize: Theme.fontSize
    verticalAlignment: TextInput.AlignVCenter

    background: Rectangle {
        radius: Theme.radiusSmall
        color: Theme.readoutBackground
        border.color: field.activeFocus ? Theme.accent : Theme.outline
        // Recessed: the shade falls on the top edge.
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelDark }
        Image {
            visible: field.iconSource !== ""
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            source: field.iconSource
            sourceSize: Qt.size(14, 14)
            opacity: 0.6
        }
    }
}
