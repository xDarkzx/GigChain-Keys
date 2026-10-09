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
    implicitWidth: 38
    implicitHeight: 38

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

    // A hardware knob whose LEDs light from the centre (Logic's pan ring).
    background: KnobFace {
        implicitWidth: 38
        implicitHeight: 38
        position: (knob.value + 1) / 2
        arcFrom: 0.5
        ledColor: Theme.ledBlue
    }
    handle: Item {} // (the face draws its own pointer)

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
