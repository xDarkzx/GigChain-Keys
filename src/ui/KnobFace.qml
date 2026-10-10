import QtQuick

// A hardware rotary knob, MainStage-style: a ring of tick marks, an LED arc
// for the value round a dark well, and a brushed-metal cap with a pointer.
// Drawn only: the control (a Dial) that owns it moves `position`.
Item {
    id: face

    // Where the knob is in its travel, 0 (fully left) .. 1 (fully right).
    property real position: 0
    // Where the lit arc starts (0: from the left, a level; 0.5: from the
    // top, a pan). `lit` false: no arc (switched off).
    property real arcFrom: 0
    property bool lit: true
    property color ledColor: Theme.ledBlue

    readonly property real sweep: 270 // degrees of travel, as on hardware
    readonly property real startAngle: 135 // where travel begins (clockwise from 3 o'clock)

    implicitWidth: 34
    implicitHeight: 34

    // Ticks, the arc's well and the lit arc.
    Canvas {
        id: ring
        anchors.fill: parent
        property real position: face.position
        property real arcFrom: face.arcFrom
        property bool lit: face.lit
        property color ledColor: face.ledColor
        onPositionChanged: requestPaint()
        onArcFromChanged: requestPaint()
        onLitChanged: requestPaint()
        onLedColorChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const cx = width / 2
            const cy = height / 2
            const r = Math.min(width, height) / 2
            const toRad = (fraction) => (face.startAngle + fraction * face.sweep) * Math.PI / 180
            // Ticks: 11 marks round the travel.
            ctx.strokeStyle = Theme.knobTick
            ctx.lineWidth = 1
            for (let i = 0; i <= 10; ++i) {
                const a = toRad(i / 10)
                ctx.beginPath()
                ctx.moveTo(cx + Math.cos(a) * (r - 1), cy + Math.sin(a) * (r - 1))
                ctx.lineTo(cx + Math.cos(a) * (r - 3), cy + Math.sin(a) * (r - 3))
                ctx.stroke()
            }
            // The well the LEDs sit in.
            const arcR = r - 5
            ctx.lineCap = "round"
            ctx.strokeStyle = Theme.knobWell
            ctx.lineWidth = 3
            ctx.beginPath()
            ctx.arc(cx, cy, arcR, toRad(0), toRad(1))
            ctx.stroke()
            if (!lit) return
            const a0 = toRad(Math.min(arcFrom, position))
            const a1 = toRad(Math.max(arcFrom, position))
            if (a1 - a0 < 0.01) return
            // A soft glow, then the lit LEDs.
            ctx.strokeStyle = Qt.rgba(ledColor.r, ledColor.g, ledColor.b, 0.25)
            ctx.lineWidth = 5
            ctx.beginPath()
            ctx.arc(cx, cy, arcR, a0, a1)
            ctx.stroke()
            ctx.strokeStyle = ledColor
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.arc(cx, cy, arcR, a0, a1)
            ctx.stroke()
        }
    }

    // The cap: a dark skirt, then brushed metal lit from above.
    Rectangle {
        id: skirt
        anchors.centerIn: parent
        width: parent.width - 14
        height: width
        radius: width / 2
        color: Theme.knobSkirt
        border.color: Theme.outline
        Rectangle {
            id: cap
            anchors.centerIn: parent
            width: parent.width - 4
            height: width
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.metalLight }
                GradientStop { position: 0.55; color: Theme.metalMid }
                GradientStop { position: 1.0; color: Theme.metalDark }
            }
            // The light catching the cap's top edge.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 1
                width: parent.width * 0.6
                height: parent.height * 0.3
                radius: height / 2
                color: Theme.metalShine
            }
        }
        // The pointer, turning with the knob.
        Item {
            anchors.fill: parent
            rotation: -135 + face.position * face.sweep
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 2
                width: 2
                height: parent.height * 0.32
                radius: 1
                color: face.lit ? Theme.knobPointer : Theme.textDim
            }
        }
    }
}
