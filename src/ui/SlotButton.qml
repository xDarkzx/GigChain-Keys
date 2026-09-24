import QtQuick

// A mixer slot (instrument or effect), styled like Logic's plug-in slots.
Rectangle {
    id: slot

    property string text
    property color accentColor: Theme.accentBlue
    property bool primary: false // the instrument slot
    property bool empty: false   // the "+" slot
    signal clicked()

    implicitHeight: primary ? 22 : 18
    radius: 3
    color: hover.hovered ? Theme.slotHover : (empty ? "transparent" : Theme.slotBackground)
    border.color: empty ? Theme.stripBorder : "transparent"

    Rectangle {
        // accent bar on the left, like Logic's active slot
        visible: !slot.empty
        width: 3
        height: parent.height - 6
        anchors.verticalCenter: parent.verticalCenter
        x: 2
        radius: 1.5
        color: slot.primary ? slot.accentColor : Theme.accentBlue
    }

    Text {
        anchors.fill: parent
        anchors.leftMargin: slot.empty ? 2 : 8
        anchors.rightMargin: 3
        text: slot.text
        color: slot.empty ? Theme.textDim : Theme.text
        font.pixelSize: slot.primary ? Theme.smallFontSize : 10
        font.bold: slot.primary
        elide: Text.ElideRight
        horizontalAlignment: slot.empty ? Text.AlignHCenter : Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: slot.clicked() }
}
