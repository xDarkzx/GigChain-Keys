pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The window of one of the app's own effects, Squash, Enhance or Sidechain,
// opened by clicking it in a channel's effects (EffectWindows'
// builtinEffectRequested). Drawn in the style of the app's own instruments
// (BuiltinKnob, BuiltinPill), each effect in its own colour. It follows the
// engine while open and closes itself when the effect is removed or replaced.
Window {
    id: win

    required property EffectWindows windows
    required property int channel
    required property int effect
    required property string pluginId

    readonly property bool isOtt: pluginId === "builtin:ott"
    readonly property bool isEnhance: pluginId === "builtin:enhance"
    readonly property bool isSidechain: pluginId === "builtin:sidechain"
    readonly property int readings: 0x5C00 // BuiltinEffectNode::kReadings

    // What the knobs and pills ask of their window (as BuiltinPanel offers).
    readonly property color accent: isOtt ? "#ff8a3d" : isEnhance ? "#b57bff" : "#ffc83d"
    readonly property color soundColour: accent
    readonly property color sidechainColour: accent
    readonly property color track: "#2a2f3a"
    readonly property color text: "#e8ebf1"
    readonly property color muted: "#8a92a3"
    readonly property color well: "#0f1218"
    readonly property color cardEdge: "#262b35"

    // The engine's values are read again this often (knobs moved by a
    // keyboard knob, the meters, the playhead).
    property int tick: 0
    readonly property var params: windows.builtinEffectParameters(channel, effect)

    function value(pid) {
        void win.tick
        return windows.builtinEffectValue(channel, effect, pid)
    }
    function set(pid, v) {
        windows.setBuiltinEffectValue(channel, effect, pid, Math.max(0, Math.min(1, v)))
        tick++
    }
    function choicesOf(pid) {
        for (let i = 0; i < params.length; ++i) if (params[i].id === pid) return params[i].choices
        return []
    }
    function choiceOf(pid) {
        const n = choicesOf(pid).length
        const v = value(pid)
        return n > 1 && v >= 0 ? Math.round(v * (n - 1)) : 0
    }
    function choose(pid, index) {
        const n = choicesOf(pid).length
        if (n > 1) set(pid, index / (n - 1))
    }
    function describe(kind, v) {
        if (kind === "db") {
            const db = (v - 0.5) * 48
            return (db > 0.05 ? "+" : "") + db.toFixed(1) + " dB"
        }
        if (kind === "time") return "×" + Math.pow(2, (v - 0.5) * 4).toFixed(2)
        if (kind === "stretch") {
            const s = Math.round((v - 0.5) * 200)
            return (s > 0 ? "+" : "") + s + "%"
        }
        return Math.round(v * 100) + "%"
    }

    width: isSidechain ? 640 : 560
    height: isSidechain ? 600 : isOtt ? 470 : 480
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
    color: "#11141a"

    Timer {
        interval: 40
        repeat: true
        running: win.visible
        property int count: 0
        onTriggered: {
            win.tick++
            // Every half second: still there? (removed, replaced, another song)
            if (++count % 12 === 0 && win.windows.effectPluginId(win.channel, win.effect) !== win.pluginId) win.close()
        }
    }

    // The backdrop: a glow of the effect's colour from the top.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(win.accent.r, win.accent.g, win.accent.b, 0.16) }
            GradientStop { position: 0.35; color: "#13161d" }
            GradientStop { position: 1.0; color: "#0d0f14" }
        }
    }

    // The name, as the instruments' windows show theirs.
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 74
        Row {
            anchors { left: parent.left; leftMargin: 24; verticalCenter: parent.verticalCenter }
            spacing: 10
            Text {
                objectName: "builtinFxName"
                text: win.isOtt ? "SQUASH" : win.isEnhance ? "ENHANCE" : "SIDECHAIN"
                color: win.text
                font.family: Theme.fontFamily
                font.pixelSize: 30
                font.weight: Font.Black
                font.letterSpacing: 2
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: proLabel.implicitWidth + 12
                height: 18
                radius: 4
                color: win.accent
                Text {
                    id: proLabel
                    anchors.centerIn: parent
                    text: "FX"
                    color: "#14161b"
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    font.weight: Font.Black
                }
            }
        }
        Text {
            anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
            text: win.isOtt ? qsTr("Three-band upward and downward compression")
                  : win.isEnhance ? qsTr("Fuller, finished sound in one knob")
                  : qsTr("Tempo-synced ducking")
            color: win.muted
            font.family: Theme.fontFamily
            font.pixelSize: 11
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 24; rightMargin: 24 }
            height: 1
            color: win.cardEdge
        }
    }

    Loader {
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; margins: 24 }
        sourceComponent: win.isOtt ? ottPanel : win.isEnhance ? enhancePanel : sidechainPanel
    }

    // ---- Squash: Depth large in the middle, the bands' meters, the rest round it.
    Component {
        id: ottPanel
        Item {
            anchors.fill: parent
            // The three bands at work: up (pulled up) or down (pushed down) from the line.
            Row {
                id: meters
                anchors { left: parent.left; top: parent.top }
                spacing: 14
                Repeater {
                    model: [qsTr("LOW"), qsTr("MID"), qsTr("HIGH")]
                    delegate: Item {
                        id: band
                        required property string modelData
                        required property int index
                        readonly property real at: win.value(win.readings + index) // 0.5: nothing
                        width: 38
                        height: 220
                        Rectangle {
                            id: meterWell
                            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: bandName.top; bottomMargin: 8 }
                            radius: 6
                            color: win.well
                            border.color: win.cardEdge
                            Rectangle {
                                readonly property real amount: band.at < 0 ? 0 : Math.max(-0.5, Math.min(0.5, band.at - 0.5))
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width - 12
                                radius: 3
                                y: amount >= 0 ? parent.height / 2 - amount * (parent.height - 8) : parent.height / 2
                                height: Math.max(2, Math.abs(amount) * (parent.height - 8))
                                color: amount >= 0 ? win.accent : "#5fb4ff"
                                Behavior on height { NumberAnimation { duration: 60 } }
                            }
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                                height: 1
                                color: "#3a4150"
                            }
                        }
                        Text {
                            id: bandName
                            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
                            text: band.modelData
                            color: win.muted
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
            Text {
                anchors { left: meters.left; top: meters.bottom; topMargin: 8 }
                width: meters.width
                wrapMode: Text.WordWrap
                text: qsTr("Orange: lifted. Blue: pushed down.")
                color: win.muted
                font.family: Theme.fontFamily
                font.pixelSize: 9
            }

            // Depth: how much of it is heard.
            BuiltinKnob {
                id: depthKnob
                objectName: "ottDepth"
                anchors { horizontalCenter: parent.horizontalCenter; horizontalCenterOffset: 60; top: parent.top; topMargin: 6 }
                owner: win; pid: 0; text: "DEPTH"; size: 132
            }
            Row {
                anchors { horizontalCenter: depthKnob.horizontalCenter; top: depthKnob.bottom; topMargin: 18 }
                spacing: 6
                BuiltinKnob { objectName: "ottTime"; owner: win; pid: 1; text: "TIME"; kind: "time"; bipolar: true; size: 52 }
                BuiltinKnob { objectName: "ottUpward"; owner: win; pid: 2; text: "UPWARD"; size: 52 }
                BuiltinKnob { objectName: "ottDownward"; owner: win; pid: 3; text: "DOWNWARD"; size: 52 }
                BuiltinKnob { objectName: "ottInGain"; owner: win; pid: 4; text: "IN"; kind: "db"; bipolar: true; size: 52 }
                BuiltinKnob { objectName: "ottOutGain"; owner: win; pid: 5; text: "OUT"; kind: "db"; bipolar: true; size: 52 }
            }
        }
    }

    // ---- Enhance: the amount, and the four settings as big tiles.
    Component {
        id: enhancePanel
        Item {
            anchors.fill: parent
            BuiltinKnob {
                id: amountKnob
                objectName: "enhanceAmount"
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top }
                owner: win; pid: 0; text: "AMOUNT"; size: 140
            }
            Grid {
                id: modes
                anchors { left: parent.left; right: parent.right; top: amountKnob.bottom; topMargin: 18 }
                columns: 4
                spacing: 10
                Repeater {
                    model: [
                        { name: "A", what: qsTr("Tight lows, airy highs") },
                        { name: "B", what: qsTr("Warm saturation") },
                        { name: "C", what: qsTr("Mids forward, louder") },
                        { name: "D", what: qsTr("Glue: one compressor") }
                    ]
                    delegate: Rectangle {
                        id: tile
                        required property var modelData
                        required property int index
                        readonly property bool selected: win.choiceOf(1) === index
                        objectName: "enhanceMode_" + index
                        width: (modes.width - 3 * modes.spacing) / 4
                        height: 92
                        radius: 10
                        color: selected ? win.accent : (tileArea.containsMouse ? "#262b35" : "#1a1e26")
                        border.color: selected ? win.accent : win.cardEdge
                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 16
                            spacing: 6
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: tile.modelData.name
                                color: tile.selected ? "#14161b" : win.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 26
                                font.weight: Font.Black
                            }
                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: tile.modelData.what
                                color: tile.selected ? "#14161b" : win.muted
                                font.family: Theme.fontFamily
                                font.pixelSize: 10
                            }
                        }
                        MouseArea {
                            id: tileArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.choose(1, tile.index)
                        }
                    }
                }
            }
        }
    }

    // ---- Sidechain: sync or note, the length, the live curve, the bank, the knobs.
    Component {
        id: sidechainPanel
        Item {
            id: sc
            anchors.fill: parent
            readonly property int curve: win.choiceOf(2)
            readonly property real slope: Math.max(0, win.value(3))
            readonly property real mix: Math.max(0, win.value(0))
            readonly property real position: Math.max(0, win.value(win.readings))
            readonly property var rateNames: win.choicesOf(1)
            readonly property var curveNames: win.choicesOf(2)

            Row {
                id: triggers
                anchors { right: parent.right; top: parent.top }
                spacing: 4
                Repeater {
                    model: win.choicesOf(4)
                    delegate: BuiltinPill {
                        required property string modelData
                        required property int index
                        objectName: "sidechainTrigger_" + index
                        owner: win
                        text: modelData.toUpperCase()
                        selected: win.choiceOf(4) === index
                        onActivated: win.choose(4, index)
                    }
                }
            }
            Text {
                anchors { left: parent.left; verticalCenter: triggers.verticalCenter }
                text: qsTr("RATE")
                color: win.muted
                font.family: Theme.fontFamily
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }
            Row {
                id: rates
                anchors { left: parent.left; right: parent.right; top: triggers.bottom; topMargin: 10 }
                spacing: 4
                Repeater {
                    model: sc.rateNames
                    delegate: BuiltinPill {
                        required property string modelData
                        required property int index
                        objectName: "sidechainRate_" + index
                        owner: win
                        text: modelData
                        implicitWidth: (rates.width - (sc.rateNames.length - 1) * rates.spacing) / Math.max(1, sc.rateNames.length)
                        selected: win.choiceOf(1) === index
                        onActivated: win.choose(1, index)
                    }
                }
            }

            // The curve as it ducks (Depth applied), the playhead where the engine is; drag to stretch.
            Rectangle {
                id: scope
                objectName: "sidechainDisplay"
                anchors { left: parent.left; right: parent.right; top: rates.bottom; topMargin: 10 }
                height: 190
                radius: 8
                color: win.well
                border.color: win.cardEdge
                Canvas {
                    id: curveCanvas
                    anchors { fill: parent; margins: 1 }
                    property var points: win.windows.sidechainCurve(sc.curve, sc.slope, 200)
                    property real mix: sc.mix
                    onPointsChanged: requestPaint()
                    onMixChanged: requestPaint()
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
                        const fill = ctx.createLinearGradient(0, top, 0, bottom)
                        fill.addColorStop(0, Qt.rgba(win.accent.r, win.accent.g, win.accent.b, 0.32))
                        fill.addColorStop(1, Qt.rgba(win.accent.r, win.accent.g, win.accent.b, 0.02))
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
                        ctx.strokeStyle = win.accent
                        ctx.lineWidth = 2.5
                        ctx.lineJoin = "round"
                        ctx.stroke()
                    }
                }
                Rectangle {
                    id: playhead
                    x: 1 + sc.position * (scope.width - 2)
                    anchors { top: parent.top; bottom: parent.bottom; margins: 1 }
                    width: 1
                    color: "#80ffffff"
                }
                Rectangle {
                    readonly property var points: curveCanvas.points
                    readonly property real gain: points && points.length > 1
                        ? points[Math.min(points.length - 1, Math.round(sc.position * (points.length - 1)))] : 1
                    x: playhead.x - width / 2
                    y: (scope.height - 14) - (1 - sc.mix * (1 - gain)) * (scope.height - 28) - height / 2
                    width: 10; height: 10; radius: 5
                    color: "#ffffff"
                    border.color: win.accent
                    border.width: 2
                }
                Text {
                    anchors { left: parent.left; top: parent.top; margins: 10 }
                    text: (sc.curveNames[sc.curve] || "") + "  ·  " + (sc.rateNames[win.choiceOf(1)] || "")
                    color: win.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
                    text: slopeArea.pressed ? qsTr("Slope %1").arg(win.describe("stretch", sc.slope)) : qsTr("Drag to stretch ↔")
                    color: win.muted
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
                    onPressed: (mouse) => { startX = mouse.x; startSlope = sc.slope }
                    onPositionChanged: (mouse) => { if (pressed) win.set(3, startSlope + (mouse.x - startX) / width) }
                }
            }

            // The bank of curves, each drawing its own shape.
            Grid {
                id: bank
                anchors { left: parent.left; right: parent.right; top: scope.bottom; topMargin: 10 }
                columns: 6
                spacing: 6
                Repeater {
                    model: sc.curveNames
                    delegate: Rectangle {
                        id: curveButton
                        required property string modelData
                        required property int index
                        readonly property bool selected: sc.curve === index
                        objectName: "sidechainCurve_" + index
                        width: (bank.width - 5 * bank.spacing) / 6
                        height: 40
                        radius: 8
                        color: selected ? win.accent : (curveArea.containsMouse ? "#262b35" : "#1d212a")
                        Canvas {
                            anchors { fill: parent; margins: 8 }
                            property var points: win.windows.sidechainCurve(curveButton.index, 0.5, 40)
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
                            onClicked: win.choose(2, curveButton.index)
                        }
                        ToolTip.visible: curveArea.containsMouse
                        ToolTip.delay: 400
                        ToolTip.text: curveButton.modelData
                    }
                }
            }

            Row {
                anchors { horizontalCenter: parent.horizontalCenter; top: bank.bottom; topMargin: 14 }
                spacing: 32
                BuiltinKnob { objectName: "sidechainDepth"; owner: win; pid: 0; text: "DEPTH"; size: 64 }
                BuiltinKnob { objectName: "sidechainSlope"; owner: win; pid: 3; text: "SLOPE"; kind: "stretch"; bipolar: true; size: 64 }
            }
        }
    }
}
