pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Saw Stack Pro's window (BuiltinPanel: its values), laid out as Pads Pro's:
// a browser at the top (the stack's name, a list that drops down by family,
// arrows), the stack's own picture filling the window with its family in big
// letters and its name between arrows, the stack itself over the picture (each
// layer in its own range, as producers build one: sub, bass, mids, highs,
// air; a level and a light for each), a row of knobs across the bottom of
// the picture, and a keyboard under it that plays the stack. The menu (top
// left) holds the sidechain and Enhance. Every knob can be learned to a
// keyboard's knobs with MIDI Learn above.
BuiltinPanel {
    id: panel

    property EngineStatus engineStatus: null // (its keyboard plays through it)

    namePrefix: "sawStack"
    ownTopBar: true
    frameHeight: 700
    // The master level follows the eight layers' (18 parameters apart).
    masterId: 8 * kLayerParams
    categories: stacks.map((s) => [s.name, s.family])

    // The families, and each stack (in Saw Stack Pro's order: SawStackTypes.cpp, kTypes; its picture: tools/make-saw-art.py).
    readonly property var families: ["LEAD", "CHORDS", "PLUCK", "PAD", "BASS"]
    readonly property var stacks: [
        { name: "Supersaw Lead", key: "01-supersaw-lead", family: "LEAD" },
        { name: "Trance Lead", key: "02-trance-lead", family: "LEAD" },
        { name: "Anthem Lead", key: "03-anthem-lead", family: "LEAD" },
        { name: "Big Room Lead", key: "04-big-room-lead", family: "LEAD" },
        { name: "Melodic Dubstep Lead", key: "05-melodic-dubstep-lead", family: "LEAD" },
        { name: "Hoover Lead", key: "06-hoover-lead", family: "LEAD" },
        { name: "Silk Lead", key: "07-silk-lead", family: "LEAD" },
        { name: "Supersaw Chords", key: "08-supersaw-chords", family: "CHORDS" },
        { name: "Future Bass Chords", key: "09-future-bass-chords", family: "CHORDS" },
        { name: "Deep House Chords", key: "10-deep-house-chords", family: "CHORDS" },
        { name: "Gated Trance Chords", key: "11-gated-trance-chords", family: "CHORDS" },
        { name: "Synthwave Chords", key: "12-synthwave-chords", family: "CHORDS" },
        { name: "Uplifting Chords", key: "13-uplifting-chords", family: "CHORDS" },
        { name: "Supersaw Pluck", key: "14-supersaw-pluck", family: "PLUCK" },
        { name: "Future Pluck", key: "15-future-pluck", family: "PLUCK" },
        { name: "Trance Pluck", key: "16-trance-pluck", family: "PLUCK" },
        { name: "Tropical Pluck", key: "17-tropical-pluck", family: "PLUCK" },
        { name: "Rave Stab", key: "18-rave-stab", family: "PLUCK" },
        { name: "Supersaw Pad", key: "19-supersaw-pad", family: "PAD" },
        { name: "Dream Pad", key: "20-dream-pad", family: "PAD" },
        { name: "Analog Warm Pad", key: "21-analog-pad", family: "PAD" },
        { name: "Swell Pad", key: "22-swell-pad", family: "PAD" },
        { name: "Airy Saw Pad", key: "23-air-pad", family: "PAD" },
        { name: "Reese Bass", key: "24-reese-bass", family: "BASS" },
        { name: "Saw Bass", key: "25-saw-bass", family: "BASS" },
        { name: "Octave Bass", key: "26-octave-bass", family: "BASS" },
        { name: "Hoover Bass", key: "27-hoover-bass", family: "BASS" },
        { name: "Pluck Bass", key: "28-pluck-bass", family: "BASS" }
    ]
    readonly property var stack: typeIndex >= 0 && typeIndex < stacks.length ? stacks[typeIndex] : null
    // The browser's list: each family's name, then its stacks.
    readonly property var browserRows: {
        const rows = []
        for (const f of families) {
            rows.push({ header: true, text: f, index: -1 })
            for (let i = 0; i < stacks.length; ++i) {
                if (stacks[i].family === f) rows.push({ header: false, text: stacks[i].name, index: i })
            }
        }
        return rows
    }
    readonly property color accent: "#4cc3ff"

    // ---- The layers (sawstack::paramId(layer, LayerParam): 18 apart).
    readonly property int kLayerParams: 18
    readonly property var lp: ({ on: 0, level: 1, octave: 2, wave: 7, cutoff: 8, lowCut: 16, lowestOnly: 17 })
    function pidOf(layer, param) { return layer * kLayerParams + lp[param] }
    function layerValue(layer, param) { return value(pidOf(layer, param)) }
    function hzOf(v) { return 20 * Math.pow(1000, Math.max(0, v)) }
    function hzText(hz) { return hz >= 1000 ? (hz / 1000).toFixed(hz >= 10000 ? 0 : 1) + "k" : Math.round(hz) + "" }
    // What a layer does, from where it sits: the noise is the air; under the
    // lowest note a sine is the sub, saws the bass; above the mids, the highs.
    function roleOf(layer) {
        if (layer === 7) return "AIR"
        if (layerValue(layer, "lowestOnly") >= 0.5) return Math.round(Math.max(0, layerValue(layer, "wave")) * 2) === 2 ? "SUB" : "BASS"
        const octave = Math.round(Math.max(0, layerValue(layer, "octave")) * 4) - 2
        return octave > 0 || hzOf(layerValue(layer, "lowCut")) >= 700 ? "HIGH" : "MID"
    }
    readonly property var roleColours: ({ SUB: "#6b7dff", BASS: "#3fa9ff", MID: "#38d6c4", HIGH: "#c6e05a", AIR: "#ff7a9c" })
    // The layers the stack is built from (as its type started them), and any
    // switched on since: one switched off stays, to be switched on again.
    property var designed: []
    function noteDesigned() {
        const on = []
        for (let l = 0; l < 8; ++l) if (layerValue(l, "on") >= 0.5) on.push(l)
        designed = on
    }
    // (After the values catch up; a timer of its own, so it never fires once the window is gone.)
    onTypeIndexChanged: designedLater.restart()
    Timer { id: designedLater; interval: 0; onTriggered: panel.noteDesigned() }
    readonly property var mixer: {
        const shown = []
        for (let l = 0; l < 8; ++l) {
            if (designed.indexOf(l) >= 0 || layerValue(l, "on") >= 0.5) shown.push(l)
        }
        // Low to high, as the stack sits: sub, bass, mids, highs, air.
        const order = { SUB: 0, BASS: 1, MID: 2, HIGH: 3, AIR: 4 }
        shown.sort((a, b) => (order[roleOf(a)] - order[roleOf(b)]) || (a - b))
        const count = {}
        for (const l of shown) count[roleOf(l)] = (count[roleOf(l)] || 0) + 1
        const seen = {}
        return shown.map((l) => {
            const role = roleOf(l)
            seen[role] = (seen[role] || 0) + 1
            return { layer: l, role: role, title: count[role] > 1 ? role + " " + seen[role] : role }
        })
    }

    // A knob as the face has them: a dark round cap with a line on it, its
    // value lit round it; the value under it while turned or hovered. Drag up
    // or right for more; the wheel steps.
    component StackKnob: Item {
        id: knob
        required property int pid
        required property string text
        property string kind: "percent"
        property int size: 52
        readonly property real v: panel.value(pid)
        property real dragging: -1
        readonly property real shown: dragging >= 0 ? dragging : Math.max(0, v)
        width: 86
        height: size + 30
        Canvas {
            id: dial
            width: knob.size + 8
            height: knob.size + 8
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
                const ring = width / 2 - 2
                const start = 0.75 * Math.PI
                const sweep = 1.5 * Math.PI
                ctx.lineCap = "round"
                ctx.lineWidth = 3
                ctx.strokeStyle = "rgba(0,0,0,0.45)"
                ctx.beginPath(); ctx.arc(c, c, ring, start, start + sweep); ctx.stroke()
                ctx.strokeStyle = panel.accent
                ctx.beginPath(); ctx.arc(c, c, ring, start, start + sweep * at); ctx.stroke()
                // The cap: lit from above.
                const r = ring - 7
                const g = ctx.createLinearGradient(0, c - r, 0, c + r)
                g.addColorStop(0, hot ? "#4a4e57" : "#3d4048")
                g.addColorStop(1, "#121418")
                ctx.fillStyle = g
                ctx.beginPath(); ctx.arc(c, c, r, 0, 2 * Math.PI); ctx.fill()
                ctx.strokeStyle = "rgba(255,255,255,0.10)"
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.arc(c, c, r - 0.5, 0, 2 * Math.PI); ctx.stroke()
                const a = start + sweep * at
                ctx.strokeStyle = "#f4f6fa"
                ctx.lineWidth = 2.5
                ctx.beginPath()
                ctx.moveTo(c + Math.cos(a) * r * 0.35, c + Math.sin(a) * r * 0.35)
                ctx.lineTo(c + Math.cos(a) * (r - 3), c + Math.sin(a) * (r - 3))
                ctx.stroke()
            }
        }
        Text {
            anchors { top: dial.bottom; topMargin: 4; horizontalCenter: parent.horizontalCenter }
            text: area.pressed || area.containsMouse ? panel.describe(knob.kind, knob.shown) : knob.text
            color: area.pressed || area.containsMouse ? panel.accent : "#e9edf3"
            font.family: Theme.fontFamily
            font.pixelSize: 12
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
                const next = Math.max(0, Math.min(1, startValue + ((mouse.x - start.x) - (mouse.y - start.y)) / 200))
                if (next === knob.dragging) return
                knob.dragging = next
                panel.set(knob.pid, next)
            }
            onReleased: knob.dragging = -1
            onCanceled: knob.dragging = -1
            onWheel: (wheel) => panel.set(knob.pid, Math.max(0, knob.v) + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))
        }
    }

    // ================= The picture, under everything below the top bar.
    Item {
        id: hero
        x: 0
        y: 52
        width: panel.frameWidth
        height: 548
        clip: true
        Image {
            objectName: "sawStackArt"
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            source: panel.stack ? "sawart/" + panel.stack.key + ".jpg" : ""
            asynchronous: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 250 } }
        }

        // The family in big letters, the stack's name between arrows.
        Text {
            id: familyTitle
            objectName: "sawStackFamily"
            anchors { horizontalCenter: parent.horizontalCenter; horizontalCenterOffset: font.letterSpacing / 2 }
            y: 22
            text: panel.stack ? panel.stack.family : ""
            color: "#f2f4f8"
            opacity: 0.9
            font.family: Theme.fontFamily
            font.pixelSize: 62
            font.weight: Font.Thin
            font.letterSpacing: 40
            style: Text.Raised
            styleColor: "#40000000"
        }
        Item {
            id: nameRow
            anchors { horizontalCenter: parent.horizontalCenter; top: familyTitle.bottom; topMargin: 2 }
            width: 520
            height: 40
            Text {
                objectName: "sawStackName"
                anchors.centerIn: parent
                text: panel.typeName
                color: "#ffffff"
                font.family: Theme.fontFamily
                font.pixelSize: 24
                font.weight: Font.DemiBold
                style: Text.Raised
                styleColor: "#50000000"
            }
            Repeater {
                model: [{ name: "sawStackHeroPrevious", glyph: "‹", step: -1, left: true },
                        { name: "sawStackHeroNext", glyph: "›", step: 1, left: false }]
                delegate: Rectangle {
                    id: arrow
                    required property var modelData
                    objectName: modelData.name
                    anchors.verticalCenter: parent.verticalCenter
                    x: modelData.left ? 0 : nameRow.width - width
                    width: 36; height: 36; radius: 18
                    color: arrowArea.containsMouse ? "#33ffffff" : "transparent"
                    Text { anchors.centerIn: parent; anchors.verticalCenterOffset: -3; text: arrow.modelData.glyph; color: "#ffffff"; font.pixelSize: 34; font.weight: Font.Light }
                    MouseArea { id: arrowArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: panel.chooseType(panel.typeIndex + arrow.modelData.step) }
                }
            }
        }

        // ================= The stack: each layer in its own range, low to high.
        Rectangle {
            id: stackBox
            objectName: "sawStackLayers"
            readonly property int column: 92
            anchors { horizontalCenter: parent.horizontalCenter; top: nameRow.bottom; topMargin: 14 }
            width: Math.max(1, panel.mixer.length) * column + Math.max(0, panel.mixer.length - 1) * 8 + 32
            height: 222
            radius: 12
            color: "#a6101218"
            border.color: "#22ffffff"
            Text {
                anchors { left: parent.left; leftMargin: 16; top: parent.top; topMargin: 10 }
                text: qsTr("THE STACK")
                color: "#e9edf3"
                font.family: Theme.fontFamily
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 2
            }
            Row {
                anchors { left: parent.left; leftMargin: 16; top: parent.top; topMargin: 32 }
                spacing: 8
                Repeater {
                    model: panel.mixer
                    delegate: Item {
                        id: strip
                        required property var modelData
                        readonly property int slot: modelData.layer // (not "layer": every Item has one)
                        readonly property color colour: panel.roleColours[modelData.role]
                        readonly property bool on: panel.layerValue(slot, "on") >= 0.5
                        readonly property int levelId: panel.pidOf(slot, "level")
                        readonly property real level: Math.max(0, panel.value(levelId))
                        width: stackBox.column
                        height: 180
                        opacity: on ? 1 : 0.45
                        Text {
                            id: roleText
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: strip.modelData.title
                            color: strip.colour
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Bold
                            font.letterSpacing: 1.5
                        }
                        // Where it sits: from its low cut to its filter.
                        Text {
                            id: bandText
                            anchors { horizontalCenter: parent.horizontalCenter; top: roleText.bottom; topMargin: 2 }
                            text: {
                                if (strip.modelData.role === "SUB") return qsTr("lowest note")
                                const low = panel.hzOf(panel.layerValue(strip.slot, "lowCut"))
                                const high = panel.hzOf(panel.layerValue(strip.slot, "cutoff"))
                                return panel.hzText(Math.max(20, low)) + "–" + panel.hzText(Math.min(20000, high)) + " Hz"
                            }
                            color: "#9aa3b2"
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                        }
                        // The level: drag it; the wheel steps.
                        Item {
                            id: fader
                            objectName: "sawStackLayerLevel_" + strip.slot
                            anchors { horizontalCenter: parent.horizontalCenter; top: bandText.bottom; topMargin: 8 }
                            width: 44
                            height: 118
                            Rectangle {
                                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; bottom: parent.bottom }
                                width: 6
                                radius: 3
                                color: "#40ffffff"
                            }
                            Rectangle {
                                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
                                width: 6
                                radius: 3
                                height: strip.level * parent.height
                                color: strip.colour
                            }
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: (1 - strip.level) * (parent.height - height)
                                width: 30
                                height: 10
                                radius: 3
                                color: faderArea.pressed || faderArea.containsMouse ? "#ffffff" : "#d6dae2"
                                border.color: "#60000000"
                            }
                            Text {
                                visible: faderArea.pressed || faderArea.containsMouse
                                anchors { left: parent.right; leftMargin: -2; verticalCenter: parent.verticalCenter }
                                text: panel.describe("db", strip.level).replace(" dB", "")
                                color: "#ffffff"
                                font.family: Theme.fontFamily
                                font.pixelSize: 10
                            }
                            MouseArea {
                                id: faderArea
                                anchors.fill: parent
                                hoverEnabled: true
                                preventStealing: true
                                cursorShape: Qt.SizeVerCursor
                                onPressed: (mouse) => panel.set(strip.levelId, 1 - mouse.y / height)
                                onPositionChanged: (mouse) => { if (pressed) panel.set(strip.levelId, 1 - mouse.y / height) }
                                onWheel: (wheel) => panel.set(strip.levelId, strip.level + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))
                            }
                        }
                        // Its light: on or off.
                        Rectangle {
                            objectName: "sawStackLayerOn_" + strip.slot
                            readonly property bool lit: strip.on
                            anchors { horizontalCenter: parent.horizontalCenter; top: fader.bottom; topMargin: 8 }
                            width: 16; height: 16; radius: 8
                            color: strip.on ? strip.colour : "#3a404c"
                            border.color: strip.on ? Qt.lighter(strip.colour, 1.3) : "#565d6a"
                            MouseArea {
                                anchors { fill: parent; margins: -6 }
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.set(panel.pidOf(strip.slot, "on"), strip.on ? 0 : 1)
                            }
                        }
                    }
                }
            }
        }

        // ================= The knobs, across the bottom of the picture.
        Rectangle {
            id: knobStrip
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 150
            color: "#99101218"
            Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: "#22ffffff" }
            Text {
                anchors { right: parent.right; rightMargin: 26; top: parent.top; topMargin: 10 }
                text: "GIGCHAIN"
                color: "#b3ffffff"
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.Bold
                font.letterSpacing: 2.5
            }
            Row {
                anchors { horizontalCenter: parent.horizontalCenter; verticalCenter: parent.verticalCenter; verticalCenterOffset: 8 }
                spacing: 4
                StackKnob { objectName: "sawStackKnob_Cutoff"; pid: panel.macro("Cutoff"); text: "Cutoff"; kind: "hz" }
                StackKnob { objectName: "sawStackKnob_Resonance"; pid: panel.macro("Resonance"); text: "Resonance" }
                StackKnob { objectName: "sawStackKnob_Spread"; pid: panel.macro("Spread"); text: "Spread" }
                StackKnob { objectName: "sawStackKnob_Attack"; pid: panel.macro("Attack"); text: "Attack"; kind: "time" }
                StackKnob { objectName: "sawStackKnob_Release"; pid: panel.macro("Release"); text: "Release"; kind: "time" }
                Rectangle { width: 1; height: 80; anchors.verticalCenter: parent.verticalCenter; color: "#26ffffff" }
                StackKnob { objectName: "sawStackKnob_Reverb"; pid: panel.macro("Reverb"); text: "Reverb" }
                StackKnob { objectName: "sawStackKnob_Delay"; pid: panel.macro("Delay"); text: "Delay" }
                StackKnob { objectName: "sawStackKnob_Width"; pid: panel.macro("Width"); text: "Width" }
                Rectangle { width: 1; height: 80; anchors.verticalCenter: parent.verticalCenter; color: "#26ffffff" }
                StackKnob { objectName: "sawStackKnob_Master"; pid: panel.masterId; text: "Master"; kind: "db" }
            }
        }
    }

    // ================= The top bar: the menu, the name, the browser.
    Rectangle {
        id: bar
        x: 0
        y: 0
        width: panel.frameWidth
        height: 52
        radius: 14
        color: "#1b1d22"
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 16; color: parent.color }

        Row {
            x: 22
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Text { text: "SAW STACK"; color: panel.accent; font.family: Theme.fontFamily; font.pixelSize: 19; font.letterSpacing: 1.5 }
            Text { text: "PRO"; color: "#8a909b"; font.family: Theme.fontFamily; font.pixelSize: 19; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
        }

        // The browser: the stack's name (its list drops down from it) and arrows.
        Rectangle {
            id: browser
            anchors.centerIn: parent
            width: 340
            height: 32
            radius: 4
            color: "#111317"
            border.color: list.opened ? panel.accent : "#2a2d33"
            Text {
                anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                text: "|||\\"
                color: "#9aa0aa"
                font.family: Theme.fontFamily
                font.pixelSize: 12
            }
            Text {
                objectName: "sawStackTypeShown"
                anchors.centerIn: parent
                text: panel.typeName
                color: "#ffffff"
                font.family: Theme.fontFamily
                font.pixelSize: 15
            }
            MouseArea {
                anchors { fill: parent; rightMargin: 46 }
                cursorShape: Qt.PointingHandCursor
                onClicked: list.opened ? list.close() : list.open()
            }
            Row {
                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                spacing: 2
                Repeater {
                    model: [{ name: "sawStackPrevious", glyph: "▲", step: -1 }, { name: "sawStackNext", glyph: "▼", step: 1 }]
                    delegate: Rectangle {
                        id: step
                        required property var modelData
                        objectName: modelData.name
                        width: 18; height: 22; radius: 3
                        color: stepArea.containsMouse ? "#2a2d33" : "transparent"
                        Text { anchors.centerIn: parent; text: step.modelData.glyph; color: "#d6dae2"; font.pixelSize: 10 }
                        MouseArea { id: stepArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: panel.chooseType(panel.typeIndex + step.modelData.step) }
                    }
                }
            }
            // Every stack, by family.
            Popup {
                id: list
                objectName: "sawStackBrowser"
                y: browser.height + 6
                x: (browser.width - width) / 2
                width: 380
                height: 520
                padding: 6
                background: Rectangle { color: "#1a1d23"; border.color: "#2a2d33"; radius: 8 }
                contentItem: ListView {
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    model: panel.browserRows
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        objectName: modelData.header ? "" : "sawStackType_" + modelData.index
                        width: ListView.view.width
                        height: modelData.header ? 30 : 28
                        radius: 5
                        color: !modelData.header && modelData.index === panel.typeIndex ? "#1f3a4d"
                             : (!modelData.header && rowArea.containsMouse ? "#262b35" : "transparent")
                        Text {
                            anchors { left: parent.left; leftMargin: row.modelData.header ? 10 : 22; verticalCenter: parent.verticalCenter; verticalCenterOffset: row.modelData.header ? 3 : 0 }
                            text: row.modelData.text
                            color: row.modelData.header ? panel.accent : "#e9edf3"
                            font.family: Theme.fontFamily
                            font.pixelSize: row.modelData.header ? 10 : 13
                            font.weight: row.modelData.header ? Font.Bold : Font.Normal
                            font.letterSpacing: row.modelData.header ? 1.5 : 0
                        }
                        MouseArea {
                            id: rowArea
                            anchors.fill: parent
                            enabled: !row.modelData.header
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { panel.chooseType(row.modelData.index); list.close() }
                        }
                    }
                }
            }
        }
    }

    // ================= The keyboard: plays the stack.
    Rectangle {
        id: keys
        objectName: "sawStackKeyboard"
        x: 0
        y: 600
        width: panel.frameWidth
        height: 100
        radius: 14
        color: "#121418"
        Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 16; color: parent.color }
        readonly property int lowest: 36  // C2
        readonly property int highest: 96 // C7
        function isBlack(note) {
            const k = note % 12
            return k === 1 || k === 3 || k === 6 || k === 8 || k === 10
        }
        readonly property var whites: {
            const list = []
            for (let n = lowest; n <= highest; ++n) if (!isBlack(n)) list.push(n)
            return list
        }
        readonly property var blacks: {
            const list = []
            let white = -1
            for (let n = lowest; n <= highest; ++n) {
                if (isBlack(n)) list.push({ note: n, after: white })
                else ++white
            }
            return list
        }
        readonly property var velocities: panel.engineStatus ? panel.engineStatus.keyVelocities : []
        readonly property real keyWidth: keyRow.width / whites.length
        function play(note, on) {
            if (panel.engineStatus) panel.engineStatus.playNote(note, on)
        }
        Item {
            id: keyRow
            anchors { fill: parent; leftMargin: 18; rightMargin: 18; topMargin: 8; bottomMargin: 10 }
            Repeater {
                model: keys.whites
                delegate: Rectangle {
                    id: white
                    required property int modelData
                    required property int index
                    readonly property bool down: (keys.velocities[modelData] || 0) > 0 || whiteArea.pressed
                    x: index * keys.keyWidth
                    width: keys.keyWidth - 1
                    height: keyRow.height
                    radius: 2
                    gradient: Gradient {
                        GradientStop { position: 0; color: white.down ? "#bfe6ff" : "#f2f2f2" }
                        GradientStop { position: 1; color: white.down ? "#7fcfff" : "#d9dade" }
                    }
                    MouseArea {
                        id: whiteArea
                        anchors.fill: parent
                        onPressed: keys.play(white.modelData, true)
                        onReleased: keys.play(white.modelData, false)
                        onCanceled: keys.play(white.modelData, false)
                    }
                }
            }
            Repeater {
                model: keys.blacks
                delegate: Rectangle {
                    id: black
                    required property var modelData
                    readonly property bool down: (keys.velocities[modelData.note] || 0) > 0 || blackArea.pressed
                    x: (modelData.after + 1) * keys.keyWidth - width / 2
                    width: keys.keyWidth * 0.6
                    height: keyRow.height * 0.62
                    radius: 2
                    gradient: Gradient {
                        GradientStop { position: 0; color: black.down ? "#2a7fb8" : "#2b2d31" }
                        GradientStop { position: 1; color: black.down ? "#1d5f8c" : "#0c0d0f" }
                    }
                    MouseArea {
                        id: blackArea
                        anchors.fill: parent
                        onPressed: keys.play(black.modelData.note, true)
                        onReleased: keys.play(black.modelData.note, false)
                        onCanceled: keys.play(black.modelData.note, false)
                    }
                }
            }
        }
    }
}
