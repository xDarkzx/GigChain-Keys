import QtQuick
import QtQuick.Controls

// Logic-style pan knob: -1 (left) .. +1 (right). Drag right or up to pan
// right, left or down to pan left; scroll; double-click centres it. `pan`
// is the channel's value; the knob follows it whenever it is not dragged.
Dial {
    id: knob

    property real pan: 0
    signal panMoved(real value)

    from: -1
    to: 1
    stepSize: 0.02
    focusPolicy: Qt.NoFocus
    implicitWidth: 34
    implicitHeight: 34

    Binding on value {
        value: knob.pan
        when: !drag.pressed
        restoreMode: Binding.RestoreNone
    }

    function moveTo(v) {
        const clamped = Math.max(-1, Math.min(1, Math.round(v / stepSize) * stepSize))
        if (clamped === value) return
        value = clamped
        panMoved(clamped)
    }

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

    // All the knob's input: the mixer's strip list must not take a sideways drag.
    MouseArea {
        id: drag
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.SizeHorCursor
        property point start
        property real startValue: 0
        onPressed: (mouse) => {
            start = Qt.point(mouse.x, mouse.y)
            startValue = knob.value
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return
            // 100 px across the knob's travel; right/up = right.
            const moved = (mouse.x - start.x) - (mouse.y - start.y)
            knob.moveTo(startValue + moved / 50)
        }
        onDoubleClicked: knob.moveTo(0)
        onWheel: (wheel) => knob.moveTo(knob.value + (wheel.angleDelta.y > 0 ? 0.04 : -0.04))
    }

    ToolTip.visible: drag.containsMouse || drag.pressed
    ToolTip.text: Math.abs(value) < 0.01 ? qsTr("Centre")
                                        : (value < 0 ? qsTr("L %1").arg(Math.round(-value * 64))
                                                     : qsTr("R %1").arg(Math.round(value * 64)))
}
