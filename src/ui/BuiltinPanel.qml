pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The window of one of the app's own instruments (Saw Stack Pro, Piano
// Pro): what it has and its values (read from the engine, and again as a
// learned knob or a loaded song moves them), its face (flat and dark, one
// colour for each part), and the top bar every one has: its name, its type
// (a list that drops down, and arrows) and its master level. The instrument's
// own cards go under the top bar (from contentTop down), at frameWidth by
// frameHeight; shrunk to fit the area, scrolled beyond that.
Item {
    id: panel

    required property EditorService service
    property string namePrefix: "builtin" // objectNames: <prefix>Panel, <prefix>Next, ...
    property string logoText
    property int masterId: -1
    // A type's kind as the display tags it: the first [part of its name, tag]
    // its name holds, else otherCategory.
    property var categories: []
    property string otherCategory
    function category(name) {
        if (!name) return ""
        for (const c of categories) {
            if (name.indexOf(c[0]) >= 0) return c[1]
        }
        return otherCategory
    }
    property int frameWidth: 1040
    property int frameHeight: 600
    readonly property int contentTop: 64
    default property alias content: frame.data

    // ---- What the instrument has.
    // [{id, name, choices}]: the type, the knobs and the sidechain's choices.
    property var parameters: []
    readonly property var typeParameter: parameters.find((p) => p.name === "Type")
    readonly property var typeNames: typeParameter ? typeParameter.choices : []
    readonly property int typeIndex: choiceOf("Type")
    // The type's name ("" while the values and the list catch up with each other).
    readonly property string typeName: typeIndex >= 0 && typeIndex < typeNames.length ? typeNames[typeIndex] : ""
    property var values: ({})

    // ---- Its face.
    readonly property color face: "#0e1014"
    readonly property color card: "#161920"
    readonly property color cardEdge: "#232834"
    readonly property color well: "#0b0d11"
    readonly property color track: "#262b35"
    readonly property color text: "#e9edf3"
    readonly property color muted: "#8590a2"
    readonly property color soundColour: "#4cc3ff"
    readonly property color fxColour: "#b28cff"
    readonly property color sidechainColour: "#ffc83d"

    function parameter(name) { return parameters.find((q) => q.name === name) }
    function macro(name) {
        const p = parameter(name)
        return p ? p.id : -1
    }
    function value(pid) { return pid >= 0 && values[pid] !== undefined ? values[pid] : -1 }
    function set(pid, v) {
        if (pid < 0) return
        const clamped = Math.max(0, Math.min(1, v))
        service.setBuiltinValue(pid, clamped)
        const next = Object.assign({}, values)
        next[pid] = clamped
        values = next
    }
    // A stepped parameter (by name): which of its choices it is on, and choosing one.
    function choicesOf(name) {
        const p = parameter(name)
        return p ? p.choices : []
    }
    function choiceOf(name) {
        const p = parameter(name)
        if (!p || p.choices.length === 0 || value(p.id) < 0) return -1
        return Math.round(value(p.id) * (p.choices.length - 1))
    }
    function choose(name, index) {
        const p = parameter(name)
        if (!p || p.choices.length === 0) return
        const n = p.choices.length
        set(p.id, (((index % n) + n) % n) / Math.max(1, n - 1))
    }
    function reload() {
        parameters = service.builtinParameters()
        refresh()
    }
    function refresh() {
        let top = masterId
        for (const p of parameters) top = Math.max(top, p.id)
        const next = {}
        let changed = false
        for (let pid = 0; pid <= top; ++pid) {
            next[pid] = service.builtinValue(pid)
            if (next[pid] !== values[pid]) changed = true
        }
        // Only when something moved: every display bound to the values redraws.
        if (changed) values = next
    }
    function chooseType(index) {
        choose("Type", index)
        refresh() // the type's layers, and its knobs where it starts them
    }
    // How a value reads, by what it is.
    function describe(kind, v) {
        if (v < 0) return ""
        switch (kind) {
        case "db": return v <= 0 ? "-inf" : (-48 + 54 * v).toFixed(1) + " dB"
        case "hz": {
            const hz = 20 * Math.pow(1000, v)
            return hz >= 1000 ? (hz / 1000).toFixed(1) + " kHz" : Math.round(hz) + " Hz"
        }
        case "time": {
            const s = 0.0005 * Math.pow(16000, v)
            return s < 1 ? Math.round(s * 1000) + " ms" : s.toFixed(2) + " s"
        }
        case "stretch": return "×" + Math.pow(2, (v - 0.5) * 2).toFixed(2)
        default: return Math.round(v * 100) + "%"
        }
    }

    Component.onCompleted: reload()

    // Shrunk to fit the area (as far as 60%), scrolled beyond that.
    readonly property real fit: Math.max(0.6, Math.min(1.25, (width - 2 * Theme.spacing) / frame.width,
                                                       (height - 2 * Theme.spacing) / frame.height))

    // (Listed as this item's own: its default property is the frame, for the instrument's cards.)
    data: [
    Connections {
        target: panel.service
        function onTargetChanged() { panel.reload() }
    },
    // A learned keyboard knob, a loaded song: the values move on their own.
    Timer {
        interval: 200
        repeat: true
        running: panel.visible
        onTriggered: panel.refresh()
    },
    Flickable {
        objectName: panel.namePrefix + "Scroll"
        anchors.fill: parent
        contentHeight: frame.height * panel.fit + 2 * Theme.spacing
        contentWidth: Math.max(width, frame.width * panel.fit + 2 * Theme.spacing)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        Rectangle {
            id: frame
            objectName: panel.namePrefix + "Panel"
            transformOrigin: Item.TopLeft
            scale: panel.fit
            x: Math.max(Theme.spacing, (panel.width - width * panel.fit) / 2)
            y: Theme.spacing
            width: panel.frameWidth
            height: panel.frameHeight
            radius: 14
            color: panel.face
            border.color: panel.cardEdge

            // ================= The top bar: the name, the type, the master level.
            Item {
                id: topBar
                anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 18; rightMargin: 14 }
                height: panel.contentTop

                Row {
                    id: logo
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    spacing: 6
                    Text { text: panel.logoText; color: panel.text; font.family: Theme.fontFamily; font.pixelSize: 18; font.weight: Font.Black; font.letterSpacing: 2 }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: proText.implicitWidth + 10; height: 18; radius: 4
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: panel.soundColour }
                            GradientStop { position: 1; color: panel.fxColour }
                        }
                        Text { id: proText; anchors.centerIn: parent; text: "PRO"; color: "#0e1014"; font.family: Theme.fontFamily; font.pixelSize: 11; font.weight: Font.Black }
                    }
                }

                // The type: ‹ the name (a list drops down from it) ›.
                Rectangle {
                    id: display
                    anchors.centerIn: parent
                    width: 400
                    height: 42
                    radius: 21
                    color: panel.card
                    border.color: typeMenu.opened ? panel.soundColour : panel.cardEdge
                    Repeater {
                        model: [{ name: panel.namePrefix + "Previous", glyph: "‹", step: -1, left: true },
                                { name: panel.namePrefix + "Next", glyph: "›", step: 1, left: false }]
                        delegate: Rectangle {
                            id: arrow
                            required property var modelData
                            objectName: modelData.name
                            anchors.verticalCenter: parent.verticalCenter
                            x: modelData.left ? 5 : display.width - width - 5
                            width: 32; height: 32; radius: 16
                            color: arrowArea.pressed ? "#11141a" : (arrowArea.containsMouse ? "#262b35" : "transparent")
                            Text { anchors.centerIn: parent; anchors.verticalCenterOffset: -2; text: arrow.modelData.glyph; color: panel.text; font.pixelSize: 22 }
                            MouseArea { id: arrowArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: panel.chooseType(panel.typeIndex + arrow.modelData.step) }
                        }
                    }
                    Item {
                        objectName: panel.namePrefix + "Menu"
                        anchors { fill: parent; leftMargin: 42; rightMargin: 42 }
                        Row {
                            anchors.centerIn: parent
                            spacing: 10
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: kindText.implicitWidth + 12; height: 18; radius: 9
                                color: "#1f3a4d"
                                Text {
                                    id: kindText
                                    anchors.centerIn: parent
                                    text: panel.category(panel.typeName)
                                    color: panel.soundColour
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 9
                                    font.weight: Font.Bold
                                    font.letterSpacing: 1
                                }
                            }
                            Text {
                                objectName: panel.namePrefix + "TypeShown"
                                anchors.verticalCenter: parent.verticalCenter
                                text: panel.typeName
                                color: panel.text
                                font.family: Theme.fontFamily
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: typeMenu.opened ? "▴" : "▾"
                                color: panel.muted
                                font.pixelSize: 11
                            }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: typeMenu.opened ? typeMenu.close() : typeMenu.open() }
                    }
                    // The list drops down under the display, as wide as it.
                    Popup {
                        id: typeMenu
                        y: display.height + 6
                        width: display.width
                        padding: 6
                        background: Rectangle { color: "#1a1e26"; border.color: panel.cardEdge; radius: 12 }
                        contentItem: Column {
                            Repeater {
                                model: panel.typeNames
                                delegate: Rectangle {
                                    id: entry
                                    required property string modelData
                                    required property int index
                                    objectName: panel.namePrefix + "Type_" + index
                                    width: typeMenu.availableWidth
                                    height: 30
                                    radius: 8
                                    color: index === panel.typeIndex ? "#1f3a4d" : (entryArea.containsMouse ? "#262b35" : "transparent")
                                    Text {
                                        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                                        width: 70
                                        text: panel.category(entry.modelData)
                                        color: panel.soundColour
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 9
                                        font.weight: Font.Bold
                                        font.letterSpacing: 1
                                    }
                                    Text {
                                        anchors { left: parent.left; leftMargin: 86; verticalCenter: parent.verticalCenter }
                                        text: entry.modelData
                                        color: panel.text
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                    }
                                    MouseArea {
                                        id: entryArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: { panel.chooseType(entry.index); typeMenu.close() }
                                    }
                                }
                            }
                        }
                    }
                }

                BuiltinKnob {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter; verticalCenterOffset: 4 }
                    owner: panel; pid: panel.masterId; text: "MASTER"; kind: "db"; size: 34
                    colour: panel.text
                }
            }
        }
    }
    ]
}
