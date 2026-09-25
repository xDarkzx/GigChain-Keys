import QtQuick

// A small dark numeric readout box (volume / peak), as in Logic's strips.
// An editable one takes a typed value: click it, type (e.g. 0 or -6.5),
// Enter. Esc or clicking elsewhere leaves it as it was.
Rectangle {
    id: readout
    property string text
    property bool alarm: false
    property bool editable: false
    // A typed volume in dB, or NaN when it is not one ("-inf" / "off" = silent).
    signal volumeTyped(real volumeDb)

    function parseVolume(typed) {
        const cleaned = typed.trim().toLowerCase().replace(/db$/, "").replace("∞", "inf").trim()
        if (cleaned === "-inf" || cleaned === "inf" || cleaned === "off") return -96
        if (!/^[-+]?(\d+(\.\d*)?|\.\d+)$/.test(cleaned)) return NaN
        return Math.max(-96, Math.min(12, Number(cleaned)))
    }

    implicitHeight: 16
    clip: true // a long value never spills over its neighbours
    radius: 2
    color: alarm ? Theme.meterHigh : Theme.readoutBackground
    border.color: input.visible ? Theme.accentBlue : (hover.hovered && editable ? Theme.border : "transparent")

    Text {
        anchors.centerIn: parent
        visible: !input.visible
        text: readout.text
        color: readout.alarm ? "white" : Theme.readoutText
        font.pixelSize: 9
        font.family: "Consolas"
    }

    TextInput {
        id: input
        objectName: "readoutInput"
        anchors.fill: parent
        anchors.leftMargin: 2
        anchors.rightMargin: 2
        visible: false
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.readoutText
        selectionColor: Theme.accentBlue
        selectedTextColor: "white"
        font.pixelSize: 9
        font.family: "Consolas"
        selectByMouse: true
        onAccepted: {
            const db = readout.parseVolume(text)
            visible = false
            if (!isNaN(db)) readout.volumeTyped(db)
        }
        Keys.onEscapePressed: visible = false
        onActiveFocusChanged: if (!activeFocus) visible = false
    }

    HoverHandler { id: hover; cursorShape: readout.editable ? Qt.IBeamCursor : Qt.ArrowCursor }
    TapHandler {
        enabled: readout.editable && !input.visible
        onTapped: {
            input.text = readout.text
            input.visible = true
            input.forceActiveFocus()
            input.selectAll()
        }
    }
}
