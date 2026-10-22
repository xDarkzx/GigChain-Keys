pragma ComponentBehavior: Bound

import QtQuick

// A pill button of the app's own instruments' windows, lit when chosen.
Rectangle {
    id: pill

    required property var owner // the instrument's window (BuiltinPanel)
    required property string text
    property bool selected: false
    property color colour: owner.sidechainColour
    signal activated()
    implicitWidth: Math.max(label.implicitWidth + 16, 34)
    implicitHeight: 24
    radius: height / 2
    color: selected ? colour : (pillArea.containsMouse ? "#262b35" : "#1d212a")

    Text {
        id: label
        anchors.centerIn: parent
        text: pill.text
        color: pill.selected ? "#14161b" : pill.owner.text
        font.family: Theme.fontFamily
        font.pixelSize: 10
        font.weight: Font.Bold
    }
    MouseArea { id: pillArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pill.activated() }
}
