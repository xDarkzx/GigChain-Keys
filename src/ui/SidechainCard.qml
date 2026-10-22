pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The sidechain of the app's own instruments, as Nicky Romero's Kickstart
// has it: its light (on or off), sync or note, the length, a big live
// display of the curve (drag it to stretch the dip), a bank of curves, and
// how hard it ducks. Its parameters are found by name ("Sidechain",
// "Sidechain Rate", ...), so any of the app's instruments that has them can show it.
BuiltinCard {
    id: sidechainCard

    readonly property string prefix: owner.namePrefix
    readonly property int curve: Math.max(0, owner.choiceOf("Sidechain Curve"))
    readonly property real slope: Math.max(0, owner.value(owner.macro("Sidechain Slope")))
    readonly property real mix: Math.max(0, owner.value(owner.macro("Sidechain")))
    readonly property var curveNames: owner.choicesOf("Sidechain Curve")
    property real position: 0

    title: qsTr("SIDECHAIN")
    colour: owner.sidechainColour
    onId: owner.macro("Sidechain On")
    lightName: prefix + "SidechainOn"

    // Sync to the song or restart on each note.
    headerRight: Row {
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        spacing: 4
        Repeater {
            model: sidechainCard.owner.choicesOf("Sidechain Trigger")
            delegate: BuiltinPill {
                required property string modelData
                required property int index
                objectName: sidechainCard.prefix + "Trigger_" + index
                owner: sidechainCard.owner
                text: modelData.toUpperCase()
                selected: sidechainCard.owner.choiceOf("Sidechain Trigger") === index
                onActivated: sidechainCard.owner.choose("Sidechain Trigger", index)
            }
        }
    }

    // Where the duck is, from the engine, while the window shows.
    Timer {
        interval: 33
        repeat: true
        running: sidechainCard.visible
        onTriggered: {
            const at = sidechainCard.owner.service.sidechainPosition()
            if (at >= 0) sidechainCard.position = at
        }
    }

    // The length.
    Row {
        id: rates
        anchors { left: parent.left; right: parent.right; top: parent.top }
        spacing: 4
        Repeater {
            model: sidechainCard.owner.choicesOf("Sidechain Rate")
            delegate: BuiltinPill {
                required property string modelData
                required property int index
                objectName: sidechainCard.prefix + "Rate_" + index
                owner: sidechainCard.owner
                text: modelData
                implicitWidth: (rates.width - 8 * rates.spacing) / 9
                selected: sidechainCard.owner.choiceOf("Sidechain Rate") === index
                onActivated: sidechainCard.owner.choose("Sidechain Rate", index)
            }
        }
    }

    // The display: the curve as it ducks (Mix applied), the playhead where
    // the engine is; drag left or right to stretch the dip.
    Rectangle {
        id: scope
        objectName: sidechainCard.prefix + "SidechainDisplay"
        anchors { left: parent.left; right: parent.right; top: rates.bottom; topMargin: 10 }
        height: 196
        radius: 8
        color: sidechainCard.owner.well
        border.color: sidechainCard.owner.cardEdge
        Canvas {
            id: curveCanvas
            anchors { fill: parent; margins: 1 }
            property var points: sidechainCard.owner.service.sidechainCurve(sidechainCard.curve, sidechainCard.slope, 200)
            property real mix: sidechainCard.mix
            onPointsChanged: requestPaint()
            onMixChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const top = 14
                const bottom = height - 14
                ctx.strokeStyle = "#1a1e26"
                ctx.lineWidth = 1
                for (let q = 1; q < 4; ++q) {
                    ctx.beginPath(); ctx.moveTo(q * width / 4, 0); ctx.lineTo(q * width / 4, height); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(0, top + q * (bottom - top) / 4); ctx.lineTo(width, top + q * (bottom - top) / 4); ctx.stroke()
                }
                if (!points || points.length < 2) return
                const yOf = (g) => bottom - (1 - mix * (1 - g)) * (bottom - top)
                const xOf = (i) => i / (points.length - 1) * width
                // The curve full on, faint, under the one as it ducks.
                ctx.strokeStyle = "#3a3320"
                ctx.setLineDash([3, 4])
                ctx.beginPath()
                for (let i = 0; i < points.length; ++i) {
                    const y = bottom - points[i] * (bottom - top)
                    if (i === 0) ctx.moveTo(xOf(i), y); else ctx.lineTo(xOf(i), y)
                }
                ctx.stroke()
                ctx.setLineDash([])
                const fill = ctx.createLinearGradient(0, top, 0, bottom)
                fill.addColorStop(0, Qt.rgba(1, 0.78, 0.24, 0.32))
                fill.addColorStop(1, Qt.rgba(1, 0.78, 0.24, 0.02))
                ctx.beginPath()
                ctx.moveTo(0, bottom)
                for (let i = 0; i < points.length; ++i) ctx.lineTo(xOf(i), yOf(points[i]))
                ctx.lineTo(width, bottom)
                ctx.closePath()
                ctx.fillStyle = fill
                ctx.fill()
                ctx.beginPath()
                for (let i = 0; i < points.length; ++i) {
                    if (i === 0) ctx.moveTo(xOf(i), yOf(points[i])); else ctx.lineTo(xOf(i), yOf(points[i]))
                }
                ctx.strokeStyle = sidechainCard.owner.sidechainColour
                ctx.lineWidth = 2.5
                ctx.lineJoin = "round"
                ctx.stroke()
            }
        }
        // The playhead, and the level there.
        Rectangle {
            id: playhead
            x: 1 + sidechainCard.position * (scope.width - 2)
            anchors { top: parent.top; bottom: parent.bottom; margins: 1 }
            width: 1
            color: "#80ffffff"
        }
        Rectangle {
            readonly property var points: curveCanvas.points
            readonly property real gain: points && points.length > 1
                ? points[Math.min(points.length - 1, Math.round(sidechainCard.position * (points.length - 1)))] : 1
            x: playhead.x - width / 2
            y: (scope.height - 14) - (1 - sidechainCard.mix * (1 - gain)) * (scope.height - 28) - height / 2
            width: 10; height: 10; radius: 5
            color: "#ffffff"
            border.color: sidechainCard.owner.sidechainColour
            border.width: 2
        }
        Text {
            anchors { left: parent.left; top: parent.top; margins: 10 }
            text: (sidechainCard.curveNames[sidechainCard.curve] || "") + "  ·  "
                  + (sidechainCard.owner.choicesOf("Sidechain Rate")[sidechainCard.owner.choiceOf("Sidechain Rate")] || "")
            color: sidechainCard.owner.text
            font.family: Theme.fontFamily
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
        Text {
            anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
            text: slopeArea.pressed ? qsTr("Slope %1").arg(sidechainCard.owner.describe("stretch", sidechainCard.slope)) : qsTr("Drag to stretch ↔")
            color: sidechainCard.owner.muted
            font.family: Theme.fontFamily
            font.pixelSize: 9
        }
        MouseArea {
            id: slopeArea
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            property real startX: 0
            property real startSlope: 0.5
            onPressed: (mouse) => { startX = mouse.x; startSlope = sidechainCard.slope }
            onPositionChanged: (mouse) => {
                if (pressed) sidechainCard.owner.set(sidechainCard.owner.macro("Sidechain Slope"), startSlope + (mouse.x - startX) / width)
            }
        }
    }

    // The bank of curves, each button drawing its own shape.
    Grid {
        id: bank
        anchors { left: parent.left; right: parent.right; top: scope.bottom; topMargin: 10 }
        columns: 6
        spacing: 6
        Repeater {
            model: sidechainCard.curveNames
            delegate: Rectangle {
                id: curveButton
                required property string modelData
                required property int index
                readonly property bool selected: sidechainCard.curve === index
                objectName: sidechainCard.prefix + "Curve_" + index
                width: (bank.width - 5 * bank.spacing) / 6
                height: 40
                radius: 8
                color: selected ? sidechainCard.owner.sidechainColour : (curveArea.containsMouse ? "#262b35" : "#1d212a")
                Canvas {
                    anchors { fill: parent; margins: 8 }
                    property var points: sidechainCard.owner.service.sidechainCurve(curveButton.index, 0.5, 40)
                    property bool lit: curveButton.selected
                    onLitChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = lit ? "#14161b" : "#c9cfda"
                        ctx.lineWidth = 1.6
                        ctx.lineJoin = "round"
                        ctx.beginPath()
                        for (let i = 0; i < points.length; ++i) {
                            const x = i / (points.length - 1) * width
                            const y = height - points[i] * height
                            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                }
                MouseArea {
                    id: curveArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sidechainCard.owner.choose("Sidechain Curve", curveButton.index)
                }
                ToolTip.visible: curveArea.containsMouse
                ToolTip.delay: 400
                ToolTip.text: curveButton.modelData
            }
        }
    }

    // How hard it ducks, and how long the dip lasts.
    Row {
        anchors { horizontalCenter: parent.horizontalCenter; top: bank.bottom; topMargin: 12 }
        spacing: 24
        BuiltinKnob {
            objectName: sidechainCard.prefix + "Knob_Sidechain"
            owner: sidechainCard.owner; pid: sidechainCard.owner.macro("Sidechain"); text: "MIX"; size: 64
            colour: sidechainCard.owner.sidechainColour
        }
        BuiltinKnob {
            objectName: sidechainCard.prefix + "Knob_SidechainSlope"
            owner: sidechainCard.owner; pid: sidechainCard.owner.macro("Sidechain Slope"); text: "SLOPE"; kind: "stretch"; bipolar: true; size: 64
            colour: sidechainCard.owner.sidechainColour
        }
    }
}
