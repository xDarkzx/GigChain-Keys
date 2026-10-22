pragma ComponentBehavior: Bound

import QtQuick

// A knob of the app's own instruments (Saw Stack Pro, Piano Pro): a flat
// ring with its value lit in its part's colour, the name under it (the value
// while turned or hovered). Drag up or right for more; the wheel steps.
Item {
    id: knob

    required property var owner // the instrument's window (BuiltinPanel)
    required property int pid
    required property string text
    property string kind: "percent"
    property color colour: owner.soundColour
    property bool bipolar: false
    property int size: 46
    readonly property real v: owner.value(pid)
    property real dragging: -1
    readonly property real shown: dragging >= 0 ? dragging : Math.max(0, v)
    implicitWidth: Math.max(size + 12, 56)
    implicitHeight: size + 22

    Canvas {
        id: dial
        width: knob.size
        height: knob.size
        anchors.horizontalCenter: parent.horizontalCenter
        opacity: knob.v < 0 ? 0.4 : 1
        property real at: knob.shown
        property bool hot: area.containsMouse || area.pressed
        onAtChanged: requestPaint()
        onHotChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const c = width / 2
            const r = width / 2 - 3
            const start = 0.75 * Math.PI
            const sweep = 1.5 * Math.PI
            ctx.lineCap = "round"
            ctx.lineWidth = 4
            ctx.strokeStyle = knob.owner.track
            ctx.beginPath(); ctx.arc(c, c, r, start, start + sweep); ctx.stroke()
            const from = knob.bipolar ? start + sweep / 2 : start
            const to = start + sweep * at
            ctx.strokeStyle = knob.colour
            ctx.beginPath(); ctx.arc(c, c, r, Math.min(from, to), Math.max(from, to)); ctx.stroke()
            ctx.fillStyle = hot ? "#232832" : "#1c2028"
            ctx.beginPath(); ctx.arc(c, c, r - 7, 0, 2 * Math.PI); ctx.fill()
            const a = start + sweep * at
            ctx.fillStyle = knob.owner.text
            ctx.beginPath(); ctx.arc(c + Math.cos(a) * (r - 13), c + Math.sin(a) * (r - 13), 2.5, 0, 2 * Math.PI); ctx.fill()
        }
    }
    Text {
        anchors { top: dial.bottom; topMargin: 5; horizontalCenter: parent.horizontalCenter }
        text: area.pressed || area.containsMouse ? knob.owner.describe(knob.kind, knob.shown) : knob.text
        color: area.pressed || area.containsMouse ? knob.colour : knob.owner.muted
        font.family: Theme.fontFamily
        font.pixelSize: 10
        font.weight: Font.DemiBold
        font.letterSpacing: 0.8
    }
    MouseArea {
        id: area
        anchors.fill: dial
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.SizeVerCursor
        property point start
        property real startValue: 0
        onPressed: (mouse) => {
            start = Qt.point(mouse.x, mouse.y)
            startValue = Math.max(0, knob.v)
            knob.dragging = startValue
        }
        onPositionChanged: (mouse) => {
            if (!pressed) return
            const next = Math.max(0, Math.min(1, startValue + ((mouse.x - start.x) - (mouse.y - start.y)) / 180))
            if (next === knob.dragging) return
            knob.dragging = next
            knob.owner.set(knob.pid, next)
        }
        onReleased: knob.dragging = -1
        onCanceled: knob.dragging = -1
        onWheel: (wheel) => knob.owner.set(knob.pid, Math.max(0, knob.v) + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))
    }
}
