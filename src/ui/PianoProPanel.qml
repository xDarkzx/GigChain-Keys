pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Piano Pro's window (BuiltinPanel: its values), laid out as Pads Pro's and
// Saw Stack Pro's: a browser at the top (the piano's name, a list that drops
// down by family, arrows), the piano's own picture filling the window with
// its family in big letters, its name between arrows and whether its
// recordings are ready, its layers over the picture (the piano, the same
// piano an octave up, strings, a sub: a level and a light for each), a row of
// knobs across the bottom of the picture, and a keyboard under it that plays
// it. Every knob can be learned to a keyboard's knobs with MIDI Learn above.
// (Squash, Enhance and the sidechain are effects of their own, added after it.)
BuiltinPanel {
    id: panel

    property EngineStatus engineStatus: null // (its keyboard plays through it)

    namePrefix: "pianoPro"
    ownTopBar: true
    frameHeight: 700
    masterId: macro("Level")
    categories: pianos.map((p) => [p.name, p.family])

    // The families, and each piano (in Piano Pro's order: PianoTypes.cpp, kTypes; its picture: tools/make-piano-art.py).
    readonly property var families: ["ACOUSTIC", "ELECTRIC", "SYNTH", "HOUSE"]
    readonly property var pianos: [
        { name: "Concert Grand", key: "01-grand-piano", family: "ACOUSTIC" },
        { name: "Bright Grand", key: "02-bright-grand", family: "ACOUSTIC" },
        { name: "Ballad Grand", key: "03-ballad-grand", family: "ACOUSTIC" },
        { name: "Upright Piano", key: "04-upright-piano", family: "ACOUSTIC" },
        { name: "Felt Piano", key: "05-felt-piano", family: "ACOUSTIC" },
        { name: "Rhodes", key: "06-electric-piano", family: "ELECTRIC" },
        { name: "Suitcase Rhodes", key: "07-suitcase-rhodes", family: "ELECTRIC" },
        { name: "Wurlitzer", key: "08-wurlitzer", family: "ELECTRIC" },
        { name: "Electric Grand", key: "09-electric-grand", family: "ELECTRIC" },
        { name: "Pianet", key: "10-pianet", family: "ELECTRIC" },
        { name: "DX Piano", key: "11-dx-piano", family: "ELECTRIC" },
        { name: "Lo-fi Keys", key: "12-lofi-keys", family: "ELECTRIC" },
        { name: "Poly Keys", key: "13-poly-keys", family: "SYNTH" },
        { name: "Poly Strings", key: "14-poly-strings", family: "SYNTH" },
        { name: "Poly Brass", key: "15-poly-brass", family: "SYNTH" },
        { name: "House Piano", key: "16-house-piano", family: "HOUSE" },
        { name: "Big House Piano", key: "17-big-house-piano", family: "HOUSE" },
        { name: "Prog House Piano", key: "18-prog-house-piano", family: "HOUSE" },
        { name: "Emotional Piano", key: "19-emotional-piano", family: "HOUSE" }
    ]
    readonly property var piano: typeIndex >= 0 && typeIndex < pianos.length ? pianos[typeIndex] : null
    // The browser's list: each family's name, then its pianos.
    readonly property var browserRows: {
        const rows = []
        for (const f of families) {
            rows.push({ header: true, text: f, index: -1 })
            for (let i = 0; i < pianos.length; ++i) {
                if (pianos[i].family === f) rows.push({ header: false, text: pianos[i].name, index: i })
            }
        }
        return rows
    }
    readonly property color accent: "#ffbf6b"

    // The layers.
    readonly property var layers: [
        { key: "Piano", title: "PIANO", role: qsTr("the piano itself"), colour: "#ffbf6b" },
        { key: "Octave", title: "OCTAVE UP", role: qsTr("an octave higher"), colour: "#7be07a" },
        { key: "Strings", title: "STRINGS", role: qsTr("two octaves up"), colour: "#b28cff" },
        { key: "Sub", title: "SUB", role: qsTr("under the lowest"), colour: "#4f7dff" }
    ]

    // Which piano plays, and whether its recordings are ready (read from the engine: PianoPro.h, kSourceParameter).
    property real recordings: -1
    property int source: -1
    readonly property var sourceNames: [qsTr("concert grand"), qsTr("upright"), qsTr("Rhodes"), qsTr("Wurlitzer"),
                                        qsTr("electric grand"), qsTr("Pianet"), qsTr("DX piano"), qsTr("polysynth")]
    readonly property bool recordedSource: source >= 0 && source <= 5
    readonly property string status: {
        if (source < 0) return ""
        const name = sourceNames[source]
        if (source === 6) return qsTr("The DX piano, made by FM synthesis")
        if (source === 7) return qsTr("An 80s polysynth: saw, pulse and sub through a 24 dB filter")
        if (recordings >= 1) return qsTr("The recorded %1").arg(name)
        if (recordings >= 0.5) return qsTr("Loading the recorded %1…").arg(name)
        return qsTr("The %1 recordings are missing: an FM piano plays instead").arg(name)
    }
    readonly property color statusColour: !recordedSource || recordings >= 1 ? "#7be07a" : (recordings >= 0.5 ? "#ffc83d" : "#ff6b6b")
    function readStatus() {
        recordings = service.builtinReading("pianoRecordings")
        const s = service.builtinReading("pianoSource")
        source = s < 0 ? -1 : Math.round(s * 7)
    }
    onValuesChanged: readStatus()
    Timer {
        interval: 500
        repeat: true
        running: panel.visible && panel.recordedSource && panel.recordings < 1
        onTriggered: panel.readStatus()
    }

    // A knob as the face has them: a dark round cap with a line on it, its
    // value lit round it; the value under it while turned or hovered. Drag up
    // or right for more; the wheel steps.
    component PianoKnob: Item {
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
            objectName: "pianoProArt"
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            source: panel.piano ? "pianoart/" + panel.piano.key + ".jpg" : ""
            asynchronous: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 250 } }
        }

        // The family in big letters, the piano's name between arrows.
        Text {
            id: familyTitle
            objectName: "pianoProFamily"
            anchors { horizontalCenter: parent.horizontalCenter; horizontalCenterOffset: font.letterSpacing / 2 }
            y: 22
            text: panel.piano ? panel.piano.family : ""
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
                objectName: "pianoProName"
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
                model: [{ name: "pianoProHeroPrevious", glyph: "‹", step: -1, left: true },
                        { name: "pianoProHeroNext", glyph: "›", step: 1, left: false }]
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
        // What plays, and whether its recordings are ready.
        Rectangle {
            id: statusPill
            anchors { horizontalCenter: parent.horizontalCenter; top: nameRow.bottom; topMargin: 4 }
            width: statusRow.implicitWidth + 24
            height: 24
            radius: 12
            color: "#8c101218"
            visible: panel.status !== ""
            Row {
                id: statusRow
                objectName: "pianoProStatus"
                readonly property string text: panel.status
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8; height: 8; radius: 4
                    color: panel.statusColour
                }
                Text {
                    text: panel.status
                    color: "#e9edf3"
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                }
            }
        }

        // ================= The layers: the piano, an octave up, strings, a sub.
        Rectangle {
            id: layerBox
            objectName: "pianoProLayers"
            readonly property int column: 132
            anchors { horizontalCenter: parent.horizontalCenter; top: statusPill.bottom; topMargin: 14 }
            width: panel.layers.length * column + (panel.layers.length - 1) * 8 + 32
            height: 206
            radius: 12
            color: "#a6101218"
            border.color: "#22ffffff"
            Text {
                anchors { left: parent.left; leftMargin: 16; top: parent.top; topMargin: 10 }
                text: qsTr("LAYERS")
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
                    model: panel.layers
                    delegate: Item {
                        id: strip
                        required property var modelData
                        required property int index
                        readonly property color colour: modelData.colour
                        readonly property int onId: panel.macro(modelData.key + " On")
                        readonly property int levelId: panel.macro(modelData.key + " Level")
                        readonly property bool on: panel.value(onId) >= 0.5
                        readonly property real level: Math.max(0, panel.value(levelId))
                        width: layerBox.column
                        height: 166
                        opacity: on ? 1 : 0.45
                        Text {
                            id: titleText
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: strip.modelData.title
                            color: strip.colour
                            font.family: Theme.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.Bold
                            font.letterSpacing: 1.5
                        }
                        Text {
                            id: roleText
                            anchors { horizontalCenter: parent.horizontalCenter; top: titleText.bottom; topMargin: 2 }
                            text: strip.modelData.role
                            color: "#9aa3b2"
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                        }
                        // The level: drag it; the wheel steps.
                        Item {
                            id: fader
                            objectName: "pianoProLayerLevel_" + strip.index
                            anchors { horizontalCenter: parent.horizontalCenter; top: roleText.bottom; topMargin: 8 }
                            width: 44
                            height: 104
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
                            objectName: "pianoProLayerOn_" + strip.index
                            readonly property bool lit: strip.on
                            anchors { horizontalCenter: parent.horizontalCenter; top: fader.bottom; topMargin: 8 }
                            width: 16; height: 16; radius: 8
                            color: strip.on ? strip.colour : "#3a404c"
                            border.color: strip.on ? Qt.lighter(strip.colour, 1.3) : "#565d6a"
                            MouseArea {
                                anchors { fill: parent; margins: -6 }
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.set(strip.onId, strip.on ? 0 : 1)
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
                PianoKnob { objectName: "pianoProKnob_Tone"; pid: panel.macro("Tone"); text: "Tone"; kind: "hz" }
                PianoKnob { objectName: "pianoProKnob_Resonance"; pid: panel.macro("Resonance"); text: "Resonance" }
                PianoKnob { objectName: "pianoProKnob_Attack"; pid: panel.macro("Attack"); text: "Attack"; kind: "time" }
                PianoKnob { objectName: "pianoProKnob_Release"; pid: panel.macro("Release"); text: "Release"; kind: "time" }
                PianoKnob { objectName: "pianoProKnob_Width"; pid: panel.macro("Width"); text: "Width" }
                Rectangle { width: 1; height: 80; anchors.verticalCenter: parent.verticalCenter; color: "#26ffffff" }
                PianoKnob { objectName: "pianoProKnob_Reverb"; pid: panel.macro("Reverb"); text: "Reverb" }
                PianoKnob { objectName: "pianoProKnob_Delay"; pid: panel.macro("Delay"); text: "Delay" }
                PianoKnob { objectName: "pianoProKnob_Tape"; pid: panel.macro("Tape"); text: "Tape" }
                Rectangle { width: 1; height: 80; anchors.verticalCenter: parent.verticalCenter; color: "#26ffffff" }
                PianoKnob { objectName: "pianoProKnob_Master"; pid: panel.masterId; text: "Master"; kind: "db" }
            }
        }
    }

    // ================= The top bar: the name, the browser.
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
            Text { text: "PIANO"; color: panel.accent; font.family: Theme.fontFamily; font.pixelSize: 19; font.letterSpacing: 1.5 }
            Text { text: "PRO"; color: "#8a909b"; font.family: Theme.fontFamily; font.pixelSize: 19; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
        }

        // The browser: the piano's name (its list drops down from it) and arrows.
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
                objectName: "pianoProTypeShown"
                anchors.centerIn: parent
                text: panel.typeName
                color: "#ffffff"
                font.family: Theme.fontFamily
                font.pixelSize: 15
            }
            MouseArea {
                objectName: "pianoProMenu"
                anchors { fill: parent; rightMargin: 46 }
                cursorShape: Qt.PointingHandCursor
                onClicked: list.opened ? list.close() : list.open()
            }
            Row {
                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                spacing: 2
                Repeater {
                    model: [{ name: "pianoProPrevious", glyph: "▲", step: -1 }, { name: "pianoProNext", glyph: "▼", step: 1 }]
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
            // Every piano, by family.
            Popup {
                id: list
                objectName: "pianoProBrowser"
                y: browser.height + 6
                x: (browser.width - width) / 2
                width: 380
                height: 560
                padding: 6
                background: Rectangle { color: "#1a1d23"; border.color: "#2a2d33"; radius: 8 }
                contentItem: ListView {
                    objectName: "pianoProTypeList"
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    model: panel.browserRows
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        objectName: modelData.header ? "" : "pianoProType_" + modelData.index
                        width: ListView.view.width
                        height: modelData.header ? 30 : 28
                        radius: 5
                        color: !modelData.header && modelData.index === panel.typeIndex ? "#4d3a1f"
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

    // ================= The keyboard: plays the piano.
    Rectangle {
        id: keys
        objectName: "pianoProKeyboard"
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
                        GradientStop { position: 0; color: white.down ? "#ffe2b8" : "#f2f2f2" }
                        GradientStop { position: 1; color: white.down ? "#ffbf6b" : "#d9dade" }
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
                        GradientStop { position: 0; color: black.down ? "#b8823a" : "#2b2d31" }
                        GradientStop { position: 1; color: black.down ? "#8c5e22" : "#0c0d0f" }
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
