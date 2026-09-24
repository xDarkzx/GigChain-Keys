import QtQuick
import QtQuick.Controls

// Logic-style pan knob: -1 (left) .. +1 (right). Drag up/down or scroll;
// double-click centres it.
Dial {
    id: knob

    signal panMoved(real value)

    from: -1
    to: 1
    stepSize: 0.02
    inputMode: Dial.Vertical
    focusPolicy: Qt.NoFocus
    implicitWidth: 34
    implicitHeight: 34

    onMoved: panMoved(value)

    background: Item {
        implicitWidth: 34
        implicitHeight: 34
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.knobFace
            border.color: Theme.knobRing
            border.width: 2
        }
        // The arc from centre to the current position, like Logic's pan ring.
        Canvas {
            id: arc
            anchors.fill: parent
            property real value: knob.value
            onValueChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const r = width / 2 - 2
                const top = -Math.PI / 2
                const angle = top + value * (Math.PI * 0.75)
                ctx.strokeStyle = Theme.accentBlue
                ctx.lineWidth = 3
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, r, Math.min(top, angle), Math.max(top, angle))
                ctx.stroke()
            }
        }
    }

    handle: Rectangle {
        x: knob.background.x + knob.background.width / 2 - width / 2
        y: knob.background.y + 5
        width: 3
        height: 10
        radius: 1.5
        color: Theme.text
        transform: Rotation {
            origin.x: 1.5
            origin.y: knob.background.height / 2 - 5
            angle: knob.value * 135
        }
    }

    TapHandler {
        onDoubleTapped: {
            knob.value = 0
            knob.panMoved(0)
        }
    }

    ToolTip.visible: hovered || pressed
    ToolTip.text: Math.abs(value) < 0.01 ? qsTr("Centre")
                                        : (value < 0 ? qsTr("L %1").arg(Math.round(-value * 64))
                                                     : qsTr("R %1").arg(Math.round(value * 64)))
}
