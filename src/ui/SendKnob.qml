import QtQuick
import QtQuick.Controls

// The channel's send to the aux effects (reverb, delay), as in Logic's send
// knobs: all the way left is off, then -40 dB up to +6 dB. Drag right or up
// for more, scroll; double-click switches it off. `sendDb` is the channel's
// value (-96 = off); the knob follows it whenever it is not dragged.
Dial {
    id: knob

    property real sendDb: -96
    signal sendMoved(real db)

    readonly property real offDb: -96
    from: -40
    to: 6
    stepSize: 0.5
    focusPolicy: Qt.NoFocus
    implicitWidth: 32
    implicitHeight: 32

    Binding on value {
        value: Math.max(knob.from, knob.sendDb)
        when: !drag.pressed
        restoreMode: Binding.RestoreNone
    }

    readonly property bool off: value <= from

    function moveTo(v) {
        const clamped = Math.max(from, Math.min(to, Math.round(v / stepSize) * stepSize))
        if (clamped === value && !(clamped <= from && sendDb > offDb)) return
        value = clamped
        sendMoved(clamped <= from ? offDb : clamped)
    }

    // A level knob: its LEDs light from the left, amber as Logic's sends; dark when off.
    background: KnobFace {
        implicitWidth: 32
        implicitHeight: 32
        position: knob.position
        arcFrom: 0
        lit: !knob.off
        ledColor: Theme.ledAmber
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
            // 100 px across the knob's travel; right/up = more.
            const moved = (mouse.x - start.x) - (mouse.y - start.y)
            knob.moveTo(startValue + moved * (knob.to - knob.from) / 100)
        }
        onDoubleClicked: knob.moveTo(knob.from)
        onWheel: (wheel) => knob.moveTo(knob.value + (wheel.angleDelta.y > 0 ? 1 : -1))
    }

    ToolTip.visible: drag.containsMouse || drag.pressed
    ToolTip.text: off ? qsTr("Aux send: off") : qsTr("Aux send: %1 dB").arg(value.toFixed(1))
}
