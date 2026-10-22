pragma ComponentBehavior: Bound

import QtQuick

// Saw Stack Pro's window (BuiltinPanel: its top bar, its values). On the
// left, the eight layers that make the stack (each one's light, wave, voices
// and level), and under them the sound knobs and the effects. On the right
// the sidechain, as Kickstart has it. Every control here changes the sound;
// the knobs can be learned to a keyboard's knobs with MIDI Learn above.
BuiltinPanel {
    id: panel

    namePrefix: "sawStack"
    logoText: "SAW STACK"
    // The master level follows the eight layers' (18 parameters apart).
    masterId: 8 * kLayerParams
    categories: [["Bass", "BASS"], ["Pad", "PAD"], ["Pluck", "PLUCK"], ["Chords", "CHORDS"]]
    otherCategory: "LEAD"

    // A layer's parameters are 18 apart (sawstack::paramId(layer, LayerParam)).
    readonly property int kLayerParams: 18
    readonly property var lp: ({ on: 0, level: 1, voices: 3, wave: 7 })
    readonly property var layers: [
        { name: qsTr("Sub"), role: qsTr("Weight under the lowest note") },
        { name: qsTr("Low Saw"), role: qsTr("Body under the chord") },
        { name: qsTr("Main Saw"), role: qsTr("The stack itself") },
        { name: qsTr("Wide Saw"), role: qsTr("Width at the sides") },
        { name: qsTr("Octave Up"), role: qsTr("Shine on top") },
        { name: qsTr("Body"), role: qsTr("Hollow square weight") },
        { name: qsTr("Pluck"), role: qsTr("The hit as a chord starts") },
        { name: qsTr("Air"), role: qsTr("Noise, one for the chord") }
    ]
    readonly property var layerColours: ["#4f7dff", "#3fa9ff", "#4cc3ff", "#38d6c4", "#7be07a", "#c6e05a", "#ffb547", "#ff7a9c"]

    function pidOf(layer, param) { return layer * kLayerParams + lp[param] }

    // ================= The left: the layers, the sound, the effects.
    BuiltinCard {
        id: layersCard
        owner: panel
        title: qsTr("LAYERS")
        x: 14
        y: panel.contentTop
        width: 620
        height: 316
        headerRight: Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: qsTr("Click a light to switch a layer, drag its bar for its level")
            color: panel.muted
            font.family: Theme.fontFamily
            font.pixelSize: 9
        }
        Column {
            anchors.fill: parent
            anchors.topMargin: -4
            spacing: 3
            Repeater {
                model: 8
                delegate: BuiltinLayerRow {
                    required property int index
                    owner: panel
                    width: parent.width
                    onId: panel.pidOf(index, "on")
                    levelId: panel.pidOf(index, "level")
                    title: panel.layers[index].name
                    role: panel.layers[index].role
                    colour: panel.layerColours[index]
                    // 0 saw, 1 square, 2 sine (sawstack::Wave); the air layer is noise.
                    shape: index === 7 ? 3 : Math.round(Math.max(0, panel.value(panel.pidOf(index, "wave"))) * 2)
                    note: index === 7 ? "" : (1 + Math.round(Math.max(0, panel.value(panel.pidOf(index, "voices"))) * 15)) + "v"
                    lightName: "sawStackLayerOn_" + index
                    levelName: "sawStackLayerLevel_" + index
                }
            }
        }
    }

    BuiltinCard {
        id: soundCard
        owner: panel
        title: qsTr("SOUND")
        x: layersCard.x
        y: layersCard.y + layersCard.height + 12
        width: 330
        height: panel.frameHeight - 14 - y
        Row {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 4
            spacing: 0
            BuiltinKnob { objectName: "sawStackKnob_Cutoff"; owner: panel; pid: panel.macro("Cutoff"); text: "CUTOFF"; kind: "hz" }
            BuiltinKnob { objectName: "sawStackKnob_Resonance"; owner: panel; pid: panel.macro("Resonance"); text: "RES" }
            BuiltinKnob { objectName: "sawStackKnob_Spread"; owner: panel; pid: panel.macro("Spread"); text: "SPREAD"; bipolar: true }
            BuiltinKnob { objectName: "sawStackKnob_Attack"; owner: panel; pid: panel.macro("Attack"); text: "ATTACK"; kind: "time" }
            BuiltinKnob { objectName: "sawStackKnob_Release"; owner: panel; pid: panel.macro("Release"); text: "RELEASE"; kind: "time" }
        }
    }

    BuiltinCard {
        owner: panel
        title: qsTr("EFFECTS")
        colour: panel.fxColour
        x: soundCard.x + soundCard.width + 12
        y: soundCard.y
        width: layersCard.x + layersCard.width - x
        height: soundCard.height
        Row {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 4
            BuiltinKnob { objectName: "sawStackKnob_Reverb"; owner: panel; pid: panel.macro("Reverb"); text: "REVERB"; colour: panel.fxColour }
            BuiltinKnob { objectName: "sawStackKnob_Delay"; owner: panel; pid: panel.macro("Delay"); text: "DELAY"; colour: panel.fxColour }
            BuiltinKnob { objectName: "sawStackKnob_OTT"; owner: panel; pid: panel.macro("OTT"); text: "OTT"; colour: panel.fxColour }
            BuiltinKnob { objectName: "sawStackKnob_Width"; owner: panel; pid: panel.macro("Width"); text: "WIDTH"; colour: panel.fxColour }
        }
    }

    // ================= The right: the sidechain, as Kickstart.
    SidechainCard {
        owner: panel
        x: layersCard.x + layersCard.width + 12
        y: layersCard.y
        width: panel.frameWidth - 14 - x
        height: panel.frameHeight - 14 - y
    }
}
