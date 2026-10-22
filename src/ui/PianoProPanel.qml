pragma ComponentBehavior: Bound

import QtQuick

// Piano Pro's window (BuiltinPanel: its top bar, its values), as Saw Stack
// Pro's. On the left the piano: which one plays (and whether its recordings
// are ready), its four layers (the piano, an octave up, strings, a sub),
// then the sound knobs and the effects. On the right the sidechain, as
// Kickstart has it. Every control here changes the sound; the knobs can be
// learned to a keyboard's knobs with MIDI Learn above.
BuiltinPanel {
    id: panel

    namePrefix: "pianoPro"
    logoText: "PIANO"
    masterId: macro("Level")
    categories: [["House", "HOUSE"], ["Emotional", "EMOTIONAL"], ["Felt", "FELT"], ["Grand", "ACOUSTIC"], ["Upright", "ACOUSTIC"]]
    otherCategory: "ELECTRIC"

    readonly property var layers: [
        { key: "Piano", name: qsTr("Piano"), role: qsTr("The piano itself"), shape: 4, colour: "#4cc3ff" },
        { key: "Octave", name: qsTr("Octave Up"), role: qsTr("The same piano an octave up"), shape: 4, colour: "#7be07a" },
        { key: "Strings", name: qsTr("Strings"), role: qsTr("Soft strings two octaves up"), shape: 0, colour: "#b28cff" },
        { key: "Sub", name: qsTr("Sub"), role: qsTr("A sine under the lowest key"), shape: 2, colour: "#4f7dff" }
    ]

    // Which piano plays, and whether its recordings are ready (read from the engine).
    property real recordings: -1
    property int source: -1
    readonly property var sourceNames: [qsTr("Kawai grand"), qsTr("Yamaha upright"), qsTr("Electric piano"), qsTr("DX piano")]
    readonly property bool recordedSource: source === 0 || source === 1
    readonly property string status: {
        if (source < 0) return ""
        const name = sourceNames[source]
        if (!recordedSource) return qsTr("%1, made by FM synthesis").arg(name)
        if (recordings >= 1) return qsTr("Recorded %1 (VCSL, free to use: CC0)").arg(name)
        if (recordings >= 0.5) return qsTr("Loading the recorded %1…").arg(name)
        return qsTr("The %1 recordings are missing: the FM piano plays instead").arg(name)
    }
    readonly property color statusColour: !recordedSource || recordings >= 1 ? "#7be07a" : (recordings >= 0.5 ? sidechainColour : "#ff6b6b")
    function readStatus() {
        recordings = service.builtinReading("pianoRecordings")
        const s = service.builtinReading("pianoSource")
        source = s < 0 ? -1 : Math.round(s * 3)
    }
    onValuesChanged: readStatus()
    Timer {
        interval: 500
        repeat: true
        running: panel.visible && panel.recordedSource && panel.recordings < 1
        onTriggered: panel.readStatus()
    }

    // ================= The left: the piano, the sound, the effects.
    BuiltinCard {
        id: pianoCard
        owner: panel
        title: qsTr("PIANO")
        x: 14
        y: panel.contentTop
        width: 620
        height: 220
        headerRight: Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: qsTr("Click a light to switch a layer, drag its bar for its level")
            color: panel.muted
            font.family: Theme.fontFamily
            font.pixelSize: 9
        }
        Row {
            id: statusRow
            objectName: "pianoProStatus"
            readonly property string text: panel.status
            anchors { left: parent.left; top: parent.top; topMargin: -6 }
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: panel.statusColour
            }
            Text {
                text: panel.status
                color: panel.text
                font.family: Theme.fontFamily
                font.pixelSize: 11
            }
        }
        Column {
            anchors { left: parent.left; right: parent.right; top: statusRow.bottom; topMargin: 8 }
            spacing: 3
            Repeater {
                model: panel.layers
                delegate: BuiltinLayerRow {
                    required property var modelData
                    required property int index
                    owner: panel
                    width: parent.width
                    onId: panel.macro(modelData.key + " On")
                    levelId: panel.macro(modelData.key + " Level")
                    title: modelData.name
                    role: modelData.role
                    colour: modelData.colour
                    shape: modelData.shape
                    lightName: "pianoProLayerOn_" + index
                    levelName: "pianoProLayerLevel_" + index
                }
            }
        }
    }

    BuiltinCard {
        id: soundCard
        owner: panel
        title: qsTr("SOUND")
        x: pianoCard.x
        y: pianoCard.y + pianoCard.height + 12
        width: pianoCard.width
        height: 140
        Row {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 2
            spacing: 22
            BuiltinKnob { objectName: "pianoProKnob_Tone"; owner: panel; pid: panel.macro("Tone"); text: "TONE"; kind: "hz"; size: 52 }
            BuiltinKnob { objectName: "pianoProKnob_Resonance"; owner: panel; pid: panel.macro("Resonance"); text: "RES"; size: 52 }
            BuiltinKnob { objectName: "pianoProKnob_Attack"; owner: panel; pid: panel.macro("Attack"); text: "ATTACK"; kind: "time"; size: 52 }
            BuiltinKnob { objectName: "pianoProKnob_Release"; owner: panel; pid: panel.macro("Release"); text: "RELEASE"; kind: "time"; size: 52 }
            BuiltinKnob { objectName: "pianoProKnob_Width"; owner: panel; pid: panel.macro("Width"); text: "WIDTH"; size: 52 }
        }
    }

    BuiltinCard {
        owner: panel
        title: qsTr("EFFECTS")
        colour: panel.fxColour
        x: pianoCard.x
        y: soundCard.y + soundCard.height + 12
        width: pianoCard.width
        height: panel.frameHeight - 14 - y
        Row {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 2
            spacing: 30
            BuiltinKnob { objectName: "pianoProKnob_Reverb"; owner: panel; pid: panel.macro("Reverb"); text: "REVERB"; colour: panel.fxColour; size: 52 }
            BuiltinKnob { objectName: "pianoProKnob_Delay"; owner: panel; pid: panel.macro("Delay"); text: "DELAY"; colour: panel.fxColour; size: 52 }
            BuiltinKnob { objectName: "pianoProKnob_OTT"; owner: panel; pid: panel.macro("OTT"); text: "OTT"; colour: panel.fxColour; size: 52 }
            BuiltinKnob { objectName: "pianoProKnob_Tape"; owner: panel; pid: panel.macro("Tape"); text: "TAPE"; colour: panel.fxColour; size: 52 }
        }
    }

    // ================= The right: the sidechain, as Kickstart.
    SidechainCard {
        owner: panel
        x: pianoCard.x + pianoCard.width + 12
        y: pianoCard.y
        width: panel.frameWidth - 14 - x
        height: panel.frameHeight - 14 - y
    }
}
