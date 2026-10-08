import QtQuick
import QtQuick.Controls

// Right-click on a fader, a pan knob or the master: learn the keyboard knob
// (or fader) that moves it, see which one does, or forget it. `slot` is
// DocumentController.setMixerKnob's: 0 the master, 1 + n strip n's volume,
// 17 + n strip n's pan; -1 none (a strip past the 16th).
StageMenu {
    id: menu

    required property DocumentController doc
    required property EngineStatus engineStatus
    property int slot: -1
    // What it moves, for the menu ("the volume", "the pan", "the master").
    property string what: ""

    // Asked when it opens (the knob may have been learned since).
    property string knob: ""
    onAboutToShow: menu.knob = menu.slot >= 0 ? menu.doc.mixerKnobName(menu.slot) : ""

    StageMenuItem {
        objectName: "learnKnob"
        text: qsTr("MIDI Learn %1…").arg(menu.what)
        enabled: menu.slot >= 0
        onTriggered: menu.engineStatus.learnMixerKnob(menu.slot)
    }
    StageMenuItem {
        objectName: "forgetKnob"
        text: menu.knob !== "" ? qsTr("Forget MIDI Learn (%1)").arg(menu.knob) : qsTr("No MIDI knob learned yet")
        enabled: menu.knob !== ""
        onTriggered: menu.doc.forgetMixerKnob(menu.slot)
    }
}
