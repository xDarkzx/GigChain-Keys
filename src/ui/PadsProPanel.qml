pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Pads Pro's window (BuiltinPanel: its values), laid out as a collection of
// analog sounds is: a browser at the top (the pad's name, a list that drops
// down by family, arrows), the pad's own picture filling the window with its
// family in big letters and its name between arrows, a row of knobs across
// the bottom of the picture (four macros, each moving what its pad wired it
// to; the effects' levels; the master), and a keyboard under it that plays
// the pad. The menu (top left) holds Touch. Every knob can be learned to a
// keyboard's knobs with MIDI Learn above.
BuiltinPanel {
    id: panel

    property EngineStatus engineStatus: null // (its keyboard plays through it)

    namePrefix: "padsPro"
    ownTopBar: true
    frameHeight: 700
    masterId: macro("Master")
    masterKind: "paddb"
    categories: pads.map((p) => [p.name, p.family])

    // The families, and each pad (in Pads Pro's order: PadsPro.cpp, kTypes; its picture: tools/make-pad-art.py).
    readonly property var families: ["PIANO", "GUITAR", "STRINGS", "ORGAN", "SYNTH", "AIR", "ATMOS"]
    readonly property var pads: [
        { name: "Juno Warmth", key: "01-juno", family: "SYNTH" },
        { name: "Supersaw Haze", key: "02-supersaw", family: "SYNTH" },
        { name: "Moog Low Glow", key: "03-moog", family: "SYNTH" },
        { name: "Breath Flute", key: "04-flute", family: "AIR" },
        { name: "FM Crystal Bells", key: "05-fmbells", family: "SYNTH" },
        { name: "Tape Choir", key: "06-tapechoir", family: "AIR" },
        { name: "Orchestra Strings", key: "07-violins", family: "STRINGS" },
        { name: "Tonewheel Organ", key: "08-tonewheel", family: "ORGAN" },
        { name: "Angelic", key: "09-angelic", family: "AIR" },
        { name: "Grand Piano", key: "10-piano", family: "PIANO" },
        { name: "Electric Dream", key: "11-epiano", family: "PIANO" },
        { name: "E-Bow Sky", key: "12-ebow", family: "GUITAR" },
        { name: "String Machine", key: "13-stringmachine", family: "STRINGS" },
        { name: "Pipe Organ", key: "14-pipeorgan", family: "ORGAN" },
        { name: "Harmonium", key: "15-harmonium", family: "ORGAN" },
        { name: "Twin Horizon", key: "16-twin", family: "SYNTH" },
        { name: "Vector Sky", key: "17-vector", family: "SYNTH" },
        { name: "Singing Bowl", key: "18-bowl", family: "AIR" },
        { name: "Nebula", key: "19-nebula", family: "AIR" },
        { name: "Mountain Wind", key: "20-wind", family: "ATMOS" },
        { name: "Ocean Shore", key: "21-ocean", family: "ATMOS" },
        { name: "Forest Dawn", key: "22-forest", family: "ATMOS" },
        { name: "Cave Dwelling", key: "23-cave", family: "ATMOS" }
    ]
    readonly property var pad: typeIndex >= 0 && typeIndex < pads.length ? pads[typeIndex] : null
    // The browser's list: each family's name, then its pads.
    readonly property var browserRows: {
        const rows = []
        for (const f of families) {
            rows.push({ header: true, text: f, index: -1 })
            for (let i = 0; i < pads.length; ++i) {
                if (pads[i].family === f) rows.push({ header: false, text: pads[i].name, index: i })
            }
        }
        return rows
    }
    readonly property color accent: "#3fb5ff"
    property bool moreOpen: false

    // A knob as the face has them: a dark round cap with a line on it, its
    // value lit round it; the value under it while turned or hovered. Drag up
    // or right for more; the wheel steps.
    component PadKnob: Item {
        id: knob
        required property int pid
        required property string text
        property string kind: "percent"
        property int size: 56
        readonly property real v: panel.value(pid)
        property real dragging: -1
        readonly property real shown: dragging >= 0 ? dragging : Math.max(0, v)
        width: 96
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
            id: art
            objectName: "padsProArt"
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            source: panel.pad ? "padart/" + panel.pad.key + ".jpg" : ""
            asynchronous: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 250 } }
        }

        // The family in big letters, the pad's name between arrows.
        Text {
            id: familyTitle
            objectName: "padsProFamily"
            anchors { horizontalCenter: parent.horizontalCenter; horizontalCenterOffset: font.letterSpacing / 2 }
            y: 196
            text: panel.pad ? panel.pad.family : ""
            color: "#f2f4f8"
            opacity: 0.9
            font.family: Theme.fontFamily
            font.pixelSize: 76
            font.weight: Font.Thin
            font.letterSpacing: 46
            style: Text.Raised
            styleColor: "#40000000"
        }
        Item {
            id: nameRow
            anchors { horizontalCenter: parent.horizontalCenter; top: familyTitle.bottom; topMargin: 10 }
            width: 520
            height: 40
            Text {
                objectName: "padsProName"
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
                model: [{ name: "padsProHeroPrevious", glyph: "‹", step: -1, left: true },
                        { name: "padsProHeroNext", glyph: "›", step: 1, left: false }]
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

        // ================= The macros, across the bottom of the picture.
        Rectangle {
            id: strip
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
                spacing: 6
                PadKnob { objectName: "padsProKnob_Brightness"; pid: panel.macro("Brightness"); text: "Brightness" }
                PadKnob { objectName: "padsProKnob_Timbre"; pid: panel.macro("Timbre"); text: "Timbre" }
                PadKnob { objectName: "padsProKnob_Time"; pid: panel.macro("Time"); text: "Time" }
                PadKnob { objectName: "padsProKnob_Movement"; pid: panel.macro("Movement"); text: "Movement" }
                Rectangle { width: 1; height: 80; anchors.verticalCenter: parent.verticalCenter; color: "#26ffffff" }
                PadKnob { objectName: "padsProKnob_Chorus"; pid: panel.macro("Chorus"); text: "Chorus Mix" }
                PadKnob { objectName: "padsProKnob_Phaser"; pid: panel.macro("Phaser"); text: "Phaser Mix" }
                Rectangle { width: 1; height: 80; anchors.verticalCenter: parent.verticalCenter; color: "#26ffffff" }
                PadKnob { objectName: "padsProKnob_Delay"; pid: panel.macro("Delay"); text: "Delay Volume" }
                PadKnob { objectName: "padsProKnob_Reverb"; pid: panel.macro("Reverb"); text: "Reverb Volume" }
                Rectangle { width: 1; height: 80; anchors.verticalCenter: parent.verticalCenter; color: "#26ffffff" }
                PadKnob { objectName: "padsProKnob_Master"; pid: panel.masterId; text: "Master"; kind: "paddb" }
            }
        }

        // ================= The rest (from the menu): over the picture's left.
        Rectangle {
            id: more
            objectName: "padsProMore"
            x: panel.moreOpen ? 0 : -width
            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            y: 0
            width: 260
            height: parent.height - strip.height
            color: "#e6121418"
            Text {
                anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 16 }
                text: qsTr("MORE")
                color: "#e9edf3"
                font.family: Theme.fontFamily
                font.pixelSize: 12
                font.weight: Font.Bold
                font.letterSpacing: 2
            }
            Column {
                anchors { left: parent.left; leftMargin: 14; right: parent.right; rightMargin: 14; top: parent.top; topMargin: 50 }
                spacing: 6
                PadKnob { objectName: "padsProKnob_Touch"; pid: panel.macro("Touch"); text: "Touch"; size: 48 }
                Text {
                    width: parent.width
                    text: qsTr("How much playing harder is louder. Pads sit steady under the keys: it starts at none.")
                    color: "#8590a2"
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
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

        // The menu: the rest of the knobs.
        Item {
            objectName: "padsProMenu"
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            width: 30
            height: 30
            Column {
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: 3
                    delegate: Rectangle { width: 18; height: 2; radius: 1; color: panel.moreOpen ? panel.accent : "#d6dae2" }
                }
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: panel.moreOpen = !panel.moreOpen }
        }
        Row {
            x: 60
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Text { text: "PADS"; color: panel.accent; font.family: Theme.fontFamily; font.pixelSize: 19; font.letterSpacing: 1.5 }
            Text { text: "PRO"; color: "#8a909b"; font.family: Theme.fontFamily; font.pixelSize: 19; font.weight: Font.DemiBold; font.letterSpacing: 1.5 }
        }

        // The browser: the pad's name (its list drops down from it) and arrows.
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
                objectName: "padsProTypeShown"
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
                    model: [{ name: "padsProPrevious", glyph: "▲", step: -1 }, { name: "padsProNext", glyph: "▼", step: 1 }]
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
            // Every pad, by family.
            Popup {
                id: list
                objectName: "padsProBrowser"
                y: browser.height + 6
                x: (browser.width - width) / 2
                width: 380
                height: 470
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
                        objectName: modelData.header ? "" : "padsProType_" + modelData.index
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

    // ================= The keyboard: plays the pad.
    Rectangle {
        id: keys
        objectName: "padsProKeyboard"
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
