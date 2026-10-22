pragma ComponentBehavior: Bound

import QtQuick

// A card of the app's own instruments' windows: a flat panel with a coloured
// mark and its title. With `onId` (a parameter that switches it), the mark is
// a light: lit in the card's colour when on, grey when off (the card greyed
// too); a click on the light or the title switches it.
Rectangle {
    id: cardItem

    required property var owner // the instrument's window (BuiltinPanel)
    property string title
    property color colour: owner.soundColour
    property int onId: -1
    property string lightName
    readonly property bool switchable: onId >= 0
    readonly property bool lit: !switchable || owner.value(onId) >= 0.5
    default property alias content: body.data
    property alias headerRight: right.data
    color: owner.card
    radius: 10
    border.color: owner.cardEdge

    Item {
        id: titleRow
        objectName: cardItem.lightName
        readonly property bool lit: cardItem.lit
        anchors { left: parent.left; leftMargin: 12; top: parent.top; topMargin: 9 }
        width: mark.width + 8 + titleText.implicitWidth + 8
        height: 24
        Rectangle {
            id: mark
            anchors { left: parent.left; leftMargin: cardItem.switchable ? 0 : 2; verticalCenter: parent.verticalCenter }
            width: cardItem.switchable ? 12 : 4
            height: 12
            radius: cardItem.switchable ? 6 : 2
            color: cardItem.lit ? cardItem.colour : "#3a404c"
            border.color: cardItem.switchable && cardItem.lit ? Qt.lighter(cardItem.colour, 1.3) : "transparent"
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        Text {
            id: titleText
            anchors { left: mark.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
            text: cardItem.title
            color: cardItem.lit ? cardItem.owner.text : cardItem.owner.muted
            font.family: Theme.fontFamily
            font.pixelSize: 11
            font.weight: Font.Bold
            font.letterSpacing: 1.6
        }
        MouseArea {
            anchors.fill: parent
            enabled: cardItem.switchable
            cursorShape: Qt.PointingHandCursor
            onClicked: cardItem.owner.set(cardItem.onId, cardItem.lit ? 0 : 1)
        }
    }
    Item {
        id: right
        anchors { right: parent.right; rightMargin: 12; top: parent.top; topMargin: 8; left: parent.horizontalCenter }
        height: 26
        opacity: cardItem.lit ? 1 : 0.35
    }
    Item {
        id: body
        opacity: cardItem.lit ? 1 : 0.35
        Behavior on opacity { NumberAnimation { duration: 120 } }
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: parent.bottom
                  leftMargin: 14; rightMargin: 14; topMargin: 42; bottomMargin: 12 }
    }
}
