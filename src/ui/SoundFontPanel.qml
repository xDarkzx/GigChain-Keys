pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

// The SoundFont Player's window, laid out as FL Studio's SoundFont Player:
// the file, then the patch, bank and name; Reverb and Chorus (the light is
// the built-in effect, the knob its level); the envelope as four sliders;
// the pitch LFO and the filter (Misc); the options under them. In the colours
// of the app's own instruments (BuiltinPanel's). Every knob and slider fully
// down plays the SoundFont's own value, as FL's do.
Item {
    id: panel

    required property EditorService service

    // ---- The face (BuiltinPanel's, so BuiltinKnob and BuiltinCard fit in).
    readonly property color face: "#0e1014"
    readonly property color card: "#161920"
    readonly property color cardEdge: "#232834"
    readonly property color well: "#0b0d11"
    readonly property color track: "#262b35"
    readonly property color text: "#e9edf3"
    readonly property color muted: "#8590a2"
    readonly property color soundColour: "#4cc3ff"
    readonly property color fxColour: "#b28cff"

    // ---- What it has: [{id, name, choices}], the values, the file.
    property var parameters: []
    property var values: ({})
    property string filePath: ""
    property real fileState: 0 // 0 none, 0.25 not read, 0.5 reading, 1 playing
    property real presetsSeen: -1
    readonly property var presetNames: parameters.length > 0 ? parameters[0].choices : []
    readonly property int presetIndex: choiceOf(0, presetNames.length)
    readonly property string presetLabel: presetIndex >= 0 && presetIndex < presetNames.length ? presetNames[presetIndex] : ""
    // "000:012 Name" -> bank, program, name.
    function presetPart(label, part) {
        if (!label) return ""
        const space = label.indexOf(" ")
        const numbers = label.substring(0, space).split(":")
        if (part === "bank") return parseInt(numbers[0])
        if (part === "program") return parseInt(numbers[1])
        return label.substring(space + 1)
    }
    readonly property string fileName: {
        if (filePath === "") return ""
        const parts = filePath.split(/[\\/]/)
        return parts[parts.length - 1]
    }

    function pid(name) {
        const p = parameters.find((q) => q.name === name)
        return p ? p.id : -1
    }
    function value(id) { return id >= 0 && values[id] !== undefined ? values[id] : -1 }
    function set(id, v) {
        if (id < 0) return
        const clamped = Math.max(0, Math.min(1, v))
        service.setBuiltinValue(id, clamped)
        const next = Object.assign({}, values)
        next[id] = clamped
        values = next
    }
    function choiceOf(id, count) {
        const v = value(id)
        if (v < 0 || count <= 0) return -1
        return Math.round(v * Math.max(0, count - 1))
    }
    function choose(id, index, count) {
        if (count <= 0) return
        set(id, (((index % count) + count) % count) / Math.max(1, count - 1))
    }
    function choosePreset(index) { choose(0, index, presetNames.length) }
    // The first preset of the bank before or after this one.
    function stepBank(step) {
        const bank = presetPart(presetLabel, "bank")
        const banks = []
        for (const n of presetNames) {
            const b = presetPart(n, "bank")
            if (banks.indexOf(b) < 0) banks.push(b)
        }
        if (banks.length === 0) return
        const at = banks.indexOf(bank)
        const target = banks[((at + step) % banks.length + banks.length) % banks.length]
        choosePreset(presetNames.findIndex((n) => presetPart(n, "bank") === target))
    }

    function reload() {
        parameters = service.builtinParameters()
        presetsSeen = service.builtinReading("soundFontPresets")
        refresh()
    }
    function refresh() {
        let top = 0
        for (const p of parameters) top = Math.max(top, p.id)
        const next = {}
        let changed = false
        for (let id = 0; id <= top; ++id) {
            next[id] = service.builtinValue(id)
            if (next[id] !== values[id]) changed = true
        }
        if (changed) values = next
        filePath = service.builtinFile()
        fileState = service.builtinReading("soundFontFile")
        // Another file's presets are in: the list again.
        if (service.builtinReading("soundFontPresets") !== presetsSeen) reload()
    }

    // How a value reads (as the engine converts it: sf2:: in SoundFont.cpp).
    function describe(kind, v) {
        if (v < 0) return ""
        const seconds = (s) => s < 1 ? Math.round(s * 1000) + " ms" : s.toFixed(2) + " s"
        const hertz = (hz) => hz >= 1000 ? (hz / 1000).toFixed(1) + " kHz" : (hz < 10 ? hz.toFixed(2) : Math.round(hz)) + " Hz"
        if (kind === "level") return Math.round(v * 200) + "%"
        if (v <= 0) return qsTr("SoundFont")
        switch (kind) {
        case "attack": return seconds(0.001 * Math.pow(10000, v))
        case "decay": return seconds(0.001 * Math.pow(20000, v))
        case "sustain": return v >= 1 ? "0 dB" : (-60 * (1 - v)).toFixed(1) + " dB"
        case "delay": return seconds(0.001 * Math.pow(5000, v))
        case "amount": {
            const d = (v - 0.5) * 2
            const cents = Math.sign(d) * d * d * 200
            return (cents > 0 ? "+" : "") + Math.round(cents) + " ct"
        }
        case "speed": return hertz(0.1 * Math.pow(200, v))
        case "cut": return hertz(20 * Math.pow(1000, v))
        case "mod": return "+" + (8 * v).toFixed(1) + " oct"
        default: return Math.round(v * 100) + "%"
        }
    }

    Component.onCompleted: reload()

    Connections {
        target: panel.service
        function onTargetChanged() { panel.reload() }
    }
    // A learned keyboard knob, a loaded song, a file read: the values move on their own.
    Timer {
        interval: 200
        repeat: true
        running: panel.visible
        onTriggered: panel.refresh()
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Load a SoundFont")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("SoundFont 2 (*.sf2 *.SF2)"), qsTr("All files (*)")]
        onAccepted: {
            panel.service.setBuiltinFile(selectedFile.toString())
            panel.refresh()
        }
    }

    // Shrunk to fit the area (as far as 60%), scrolled beyond that.
    readonly property real fit: Math.max(0.6, Math.min(1.35, (width - 2 * Theme.spacing) / frame.width,
                                                       (height - 2 * Theme.spacing) / frame.height))

    // ---- The pieces.
    // A field sunk into the face.
    component Well: Rectangle {
        color: panel.well
        radius: 6
        border.color: panel.cardEdge
    }
    // A small flat button (the folder, the arrows).
    component FaceButton: Rectangle {
        id: faceButton
        property string glyph
        property string iconSource
        signal clicked()
        width: 34
        height: 34
        radius: 6
        color: buttonArea.pressed ? "#11141a" : (buttonArea.containsMouse ? "#262b35" : panel.card)
        border.color: panel.cardEdge
        Text {
            anchors.centerIn: parent
            visible: faceButton.iconSource === ""
            text: faceButton.glyph
            color: panel.text
            font.family: Theme.fontFamily
            font.pixelSize: 18
        }
        Image {
            anchors.centerIn: parent
            visible: faceButton.iconSource !== ""
            source: faceButton.iconSource
            sourceSize: Qt.size(18, 18)
        }
        MouseArea {
            id: buttonArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: faceButton.clicked()
        }
    }
    // A number with ‹ › on each side (FL's PATCH and BANK).
    component Stepper: Column {
        id: stepper
        property string label
        property string number
        property string namePrefix
        signal step(int by)
        spacing: 4
        Text { text: stepper.label; color: panel.muted; font.family: Theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 1.2 }
        Well {
            width: 92
            height: 34
            Repeater {
                model: [{ glyph: "‹", by: -1, left: true }, { glyph: "›", by: 1, left: false }]
                delegate: Rectangle {
                    id: arrow
                    required property var modelData
                    objectName: stepper.namePrefix + (modelData.left ? "Previous" : "Next")
                    x: modelData.left ? 2 : parent.width - width - 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24; height: 30; radius: 5
                    color: arrowArea.pressed ? "#11141a" : (arrowArea.containsMouse ? "#262b35" : "transparent")
                    Text { anchors.centerIn: parent; anchors.verticalCenterOffset: -1; text: arrow.modelData.glyph; color: panel.text; font.pixelSize: 18 }
                    MouseArea { id: arrowArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: stepper.step(arrow.modelData.by) }
                }
            }
            Text {
                anchors.centerIn: parent
                text: stepper.number
                color: panel.text
                font.family: Theme.fontFamily
                font.pixelSize: 15
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
            }
        }
    }
    // One of FL's envelope sliders: fully down plays the SoundFont's own value.
    component Fader: Item {
        id: fader
        required property int pid
        required property string text
        property string kind
        readonly property real v: panel.value(pid)
        property real dragging: -1
        readonly property real shown: dragging >= 0 ? dragging : Math.max(0, v)
        width: 44
        Rectangle {
            id: slot
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; bottom: label.top; bottomMargin: 8 }
            width: 6
            radius: 3
            color: panel.track
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: parent.height * fader.shown
                radius: 3
                color: panel.soundColour
                opacity: fader.shown > 0 ? 1 : 0
            }
        }
        Rectangle {
            id: cap
            anchors.horizontalCenter: slot.horizontalCenter
            y: slot.y + (slot.height - height) * (1 - fader.shown)
            width: 30
            height: 14
            radius: 4
            color: faderArea.containsMouse || faderArea.pressed ? "#2c323e" : "#232832"
            border.color: fader.shown > 0 ? panel.soundColour : panel.cardEdge
            Rectangle { anchors.centerIn: parent; width: 16; height: 2; radius: 1; color: panel.text; opacity: 0.8 }
        }
        Text {
            id: label
            anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter }
            text: faderArea.pressed || faderArea.containsMouse ? panel.describe(fader.kind, fader.shown) : fader.text
            color: faderArea.pressed || faderArea.containsMouse ? panel.soundColour : panel.muted
            font.family: Theme.fontFamily
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0.8
        }
        MouseArea {
            id: faderArea
            objectName: "soundFontFader_" + fader.text
            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: slot.bottom; bottomMargin: -8 }
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeVerCursor
            property real startY
            property real startValue
            onPressed: (mouse) => {
                startY = mouse.y
                startValue = Math.max(0, fader.v)
                fader.dragging = startValue
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                const next = Math.max(0, Math.min(1, startValue - (mouse.y - startY) / slot.height))
                if (next === fader.dragging) return
                fader.dragging = next
                panel.set(fader.pid, next)
            }
            onReleased: fader.dragging = -1
            onCanceled: fader.dragging = -1
            onDoubleClicked: panel.set(fader.pid, 0) // back to the SoundFont's
            onWheel: (wheel) => panel.set(fader.pid, Math.max(0, fader.v) + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))
        }
    }
    // A row of choices (FL's options), one lit.
    component Choices: Row {
        id: choices
        required property int pid
        required property string label
        required property var names
        readonly property int chosen: panel.choiceOf(pid, names.length)
        spacing: 8
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: choices.label
            color: panel.muted
            font.family: Theme.fontFamily
            font.pixelSize: 9
            font.weight: Font.Bold
            font.letterSpacing: 1.2
        }
        Row {
            spacing: 2
            Repeater {
                model: choices.names
                delegate: Rectangle {
                    id: choice
                    required property string modelData
                    required property int index
                    readonly property bool on: index === choices.chosen
                    objectName: "soundFontChoice_" + choices.label + "_" + index
                    width: choiceText.implicitWidth + 14
                    height: 26
                    radius: 5
                    color: on ? "#1f3a4d" : (choiceArea.containsMouse ? "#262b35" : panel.well)
                    border.color: on ? panel.soundColour : panel.cardEdge
                    Text {
                        id: choiceText
                        anchors.centerIn: parent
                        text: choice.modelData
                        color: choice.on ? panel.text : panel.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    MouseArea {
                        id: choiceArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.choose(choices.pid, choice.index, choices.names.length)
                    }
                }
            }
        }
    }

    Flickable {
        objectName: "soundFontScroll"
        anchors.fill: parent
        contentHeight: frame.height * panel.fit + 2 * Theme.spacing
        contentWidth: Math.max(width, frame.width * panel.fit + 2 * Theme.spacing)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

        Rectangle {
            id: frame
            objectName: "soundFontPanel"
            transformOrigin: Item.TopLeft
            scale: panel.fit
            x: Math.max(Theme.spacing, (panel.width - width * panel.fit) / 2)
            y: Theme.spacing
            width: 760
            height: 620
            radius: 14
            color: panel.face
            border.color: panel.cardEdge

            // ================= The file.
            Row {
                id: fileRow
                x: 16
                y: 16
                spacing: 8
                FaceButton {
                    objectName: "soundFontLoad"
                    iconSource: "icons/folder.svg"
                    onClicked: fileDialog.open()
                }
                Well {
                    width: frame.width - 32 - 34 - 8
                    height: 34
                    Text {
                        objectName: "soundFontFileName"
                        anchors { left: parent.left; right: status.left; leftMargin: 12; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        text: panel.fileName !== "" ? panel.fileName : qsTr("No SoundFont loaded")
                        color: panel.fileName !== "" ? panel.text : panel.muted
                        elide: Text.ElideMiddle
                        font.family: Theme.fontFamily
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Text {
                        id: status
                        objectName: "soundFontStatus"
                        anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                        text: panel.fileState === 0.5 ? qsTr("Reading…")
                            : panel.fileState === 0.25 ? qsTr("Could not be read")
                            : panel.fileState === 1 ? (panel.presetNames.length === 1 ? qsTr("1 preset")
                                                                                    : qsTr("%1 presets").arg(panel.presetNames.length)) : ""
                        color: panel.fileState === 0.25 ? Theme.danger : panel.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: 11
                    }
                }
            }

            // ================= Patch, bank, name.
            Row {
                id: presetRow
                x: 16
                y: fileRow.y + 34 + 14
                spacing: 10
                opacity: panel.presetNames.length > 0 ? 1 : 0.4
                Stepper {
                    label: qsTr("PATCH")
                    namePrefix: "soundFontPatch"
                    number: panel.presetLabel !== "" ? String(panel.presetPart(panel.presetLabel, "program")) : "–"
                    onStep: (by) => panel.choosePreset(panel.presetIndex + by)
                }
                Stepper {
                    label: qsTr("BANK")
                    namePrefix: "soundFontBank"
                    number: panel.presetLabel !== "" ? String(panel.presetPart(panel.presetLabel, "bank")) : "–"
                    onStep: (by) => panel.stepBank(by)
                }
                Column {
                    spacing: 4
                    Text { text: qsTr("NAME"); color: panel.muted; font.family: Theme.fontFamily; font.pixelSize: 9; font.weight: Font.Bold; font.letterSpacing: 1.2 }
                    Well {
                        id: nameField
                        objectName: "soundFontName"
                        readonly property string shown: panel.presetLabel !== "" ? panel.presetPart(panel.presetLabel, "name") : ""
                        width: frame.width - 32 - 2 * 92 - 2 * 10
                        height: 34
                        border.color: presetMenu.opened ? panel.soundColour : panel.cardEdge
                        Text {
                            anchors { left: parent.left; right: caret.left; leftMargin: 12; rightMargin: 8; verticalCenter: parent.verticalCenter }
                            text: nameField.shown !== "" ? nameField.shown : qsTr("Load a SoundFont to choose its sounds")
                            color: panel.presetLabel !== "" ? panel.soundColour : panel.muted
                            elide: Text.ElideRight
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        Text {
                            id: caret
                            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            text: presetMenu.opened ? "▴" : "▾"
                            color: panel.muted
                            font.pixelSize: 11
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: panel.presetNames.length > 0
                            cursorShape: Qt.PointingHandCursor
                            onClicked: presetMenu.opened ? presetMenu.close() : presetMenu.open()
                        }
                        // Every preset, by bank and program.
                        Popup {
                            id: presetMenu
                            y: nameField.height + 6
                            width: nameField.width
                            height: Math.min(360, presetList.contentHeight + 2 * padding)
                            padding: 6
                            onOpened: presetList.positionViewAtIndex(Math.max(0, panel.presetIndex), ListView.Center)
                            background: Rectangle { color: "#1a1e26"; border.color: panel.cardEdge; radius: 10 }
                            contentItem: ListView {
                                id: presetList
                                objectName: "soundFontPresetList"
                                clip: true
                                model: panel.presetNames
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                delegate: Rectangle {
                                    id: entry
                                    required property string modelData
                                    required property int index
                                    width: presetList.width
                                    height: 28
                                    radius: 6
                                    color: index === panel.presetIndex ? "#1f3a4d" : (entryArea.containsMouse ? "#262b35" : "transparent")
                                    Text {
                                        anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                                        width: 64
                                        text: entry.modelData.substring(0, entry.modelData.indexOf(" "))
                                        color: panel.muted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 11
                                        font.features: { "tnum": 1 }
                                    }
                                    Text {
                                        anchors { left: parent.left; leftMargin: 80; right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                        text: panel.presetPart(entry.modelData, "name")
                                        color: panel.text
                                        elide: Text.ElideRight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                    }
                                    MouseArea {
                                        id: entryArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: { panel.choosePreset(entry.index); presetMenu.close() }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ================= Reverb and chorus: the light is the built-in effect.
            BuiltinCard {
                id: reverbCard
                owner: panel
                title: qsTr("REVERB")
                colour: panel.fxColour
                onId: panel.pid("Reverb")
                lightName: "soundFontReverbLight"
                x: 16
                y: presetRow.y + 34 + 18 + 18
                width: (frame.width - 32 - 12) / 2
                height: 128
                headerRight: Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: qsTr("BUILT-IN")
                    color: panel.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }
                BuiltinKnob {
                    objectName: "soundFontKnob_ReverbLevel"
                    anchors.centerIn: parent
                    owner: panel; pid: panel.pid("Reverb Level"); text: "LEVEL"; kind: "level"; colour: panel.fxColour; size: 54
                }
            }
            BuiltinCard {
                owner: panel
                title: qsTr("CHORUS")
                colour: panel.fxColour
                onId: panel.pid("Chorus")
                lightName: "soundFontChorusLight"
                x: reverbCard.x + reverbCard.width + 12
                y: reverbCard.y
                width: reverbCard.width
                height: reverbCard.height
                headerRight: Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: qsTr("BUILT-IN")
                    color: panel.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }
                BuiltinKnob {
                    objectName: "soundFontKnob_ChorusLevel"
                    anchors.centerIn: parent
                    owner: panel; pid: panel.pid("Chorus Level"); text: "LEVEL"; kind: "level"; colour: panel.fxColour; size: 54
                }
            }

            // ================= The envelope: four sliders.
            BuiltinCard {
                id: envelopeCard
                owner: panel
                title: qsTr("ENVELOPE")
                x: 16
                y: reverbCard.y + reverbCard.height + 12
                width: reverbCard.width
                height: frame.height - y - 16 - 26 - 16
                headerRight: Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: qsTr("Down: the SoundFont's")
                    color: panel.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                }
                Row {
                    anchors { fill: parent; topMargin: 4 }
                    spacing: (width - 4 * 44) / 3
                    Fader { height: parent.height; pid: panel.pid("Attack"); text: "A"; kind: "attack" }
                    Fader { height: parent.height; pid: panel.pid("Decay"); text: "D"; kind: "decay" }
                    Fader { height: parent.height; pid: panel.pid("Sustain"); text: "S"; kind: "sustain" }
                    Fader { height: parent.height; pid: panel.pid("Release"); text: "R"; kind: "decay" }
                }
            }

            // ================= The pitch LFO and the filter.
            BuiltinCard {
                id: lfoCard
                owner: panel
                title: qsTr("LFO")
                x: envelopeCard.x + envelopeCard.width + 12
                y: envelopeCard.y
                width: envelopeCard.width
                height: (envelopeCard.height - 12) / 2
                headerRight: Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: qsTr("PITCH")
                    color: panel.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }
                Row {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 2
                    spacing: 12
                    BuiltinKnob { objectName: "soundFontKnob_LfoDelay"; owner: panel; pid: panel.pid("LFO Delay"); text: "DEL"; kind: "delay"; size: 46 }
                    BuiltinKnob { objectName: "soundFontKnob_LfoAmount"; owner: panel; pid: panel.pid("LFO Amount"); text: "AMT"; kind: "amount"; size: 46
                                   bipolar: panel.value(panel.pid("LFO Amount")) > 0 } // (fully left: off, nothing lit)
                    BuiltinKnob { objectName: "soundFontKnob_LfoSpeed"; owner: panel; pid: panel.pid("LFO Speed"); text: "SPD"; kind: "speed"; size: 46 }
                }
            }
            BuiltinCard {
                owner: panel
                title: qsTr("MISC")
                x: lfoCard.x
                y: lfoCard.y + lfoCard.height + 12
                width: lfoCard.width
                height: lfoCard.height
                headerRight: Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: qsTr("FILTER")
                    color: panel.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 1.2
                }
                Row {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 2
                    spacing: 24
                    BuiltinKnob { objectName: "soundFontKnob_Cutoff"; owner: panel; pid: panel.pid("Cutoff"); text: "CUT"; kind: "cut"; size: 46 }
                    BuiltinKnob { objectName: "soundFontKnob_CutoffMod"; owner: panel; pid: panel.pid("Cutoff Mod"); text: "MOD"; kind: "mod"; size: 46 }
                }
            }

            // ================= The options.
            Row {
                x: 16
                y: frame.height - 16 - 26
                spacing: 16
                Choices { pid: panel.pid("Quality"); label: qsTr("QUALITY"); names: [qsTr("Crunchy"), qsTr("Standard"), qsTr("HQ")] }
                Choices { pid: panel.pid("Mode"); label: qsTr("MODE"); names: [qsTr("Auto"), qsTr("Instrument"), qsTr("Drums")] }
                Choices { pid: panel.pid("Velocity Cutoff"); label: qsTr("VEL → CUT"); names: [qsTr("Off"), qsTr("On")] }
            }

            // ================= Nothing loaded yet: what to do.
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: reverbCard.top; bottom: parent.bottom; margins: 16 }
                visible: panel.fileName === ""
                radius: 10
                color: Qt.rgba(0.055, 0.063, 0.078, 0.92)
                border.color: panel.cardEdge
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 64
                    spacing: 14
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Load a SoundFont")
                        color: panel.text
                        font.family: Theme.fontFamily
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Choose a .sf2 file: a whole General MIDI set or a single instrument. It is saved with the song, by where the file is.")
                        color: panel.muted
                        font.family: Theme.fontFamily
                        font.pixelSize: 12
                    }
                    Rectangle {
                        objectName: "soundFontLoadBig"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: loadText.implicitWidth + 36
                        height: 38
                        radius: 8
                        color: loadArea.containsMouse ? "#2a5f80" : "#1f3a4d"
                        border.color: panel.soundColour
                        Text {
                            id: loadText
                            anchors.centerIn: parent
                            text: qsTr("Choose a SoundFont…")
                            color: panel.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: loadArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: fileDialog.open()
                        }
                    }
                }
            }
        }
    }
}
