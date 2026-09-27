pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The looper from the keyboard: learn a button or pad for Record, Loop and
// the rest, and a knob to choose the instrument (lit in the mixer). They are
// kept with this setlist.
StageDialog {
    id: dialog

    required property LoopController loops

    title: qsTr("Loops from the keyboard")
    width: 560
    onClosed: loops.cancelLearning()

    ColumnLayout {
        width: dialog.width
        spacing: 8

        Label {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            Layout.bottomMargin: 0
            text: qsTr("Record and Loop act on the instrument lit in the mixer. Holding Record and Loop together clears its loop. These are kept with this setlist.")
            color: Theme.textDim
            wrapMode: Text.Wrap
        }

        Repeater {
            model: dialog.loops.controls
            delegate: RowLayout {
                id: row
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                spacing: Theme.spacing
                readonly property bool learning: dialog.loops.learning === modelData.button
                Label {
                    Layout.preferredWidth: 160
                    text: row.modelData.label
                    color: Theme.text
                }
                Label {
                    objectName: "loopControlTrigger"
                    Layout.fillWidth: true
                    text: row.learning ? qsTr("Press the button, pad or pedal…")
                                       : row.modelData.trigger !== "" ? row.modelData.trigger : qsTr("Not set")
                    color: row.learning ? Theme.accent : row.modelData.trigger !== "" ? Theme.text : Theme.textDim
                    elide: Text.ElideRight
                }
                StageButton {
                    objectName: "loopControlLearn"
                    text: row.learning ? qsTr("Cancel") : qsTr("Learn")
                    highlighted: row.learning
                    onClicked: row.learning ? dialog.loops.cancelLearning() : dialog.loops.learn(row.modelData.button)
                }
                StageButton {
                    text: "✕"
                    enabled: row.modelData.trigger !== ""
                    tip: qsTr("Forget it")
                    onClicked: dialog.loops.forget(row.modelData.button)
                }
            }
        }

        StageDivider { Layout.fillWidth: true; Layout.topMargin: 4 }

        // The instrument knob.
        RowLayout {
            id: knobRow
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            spacing: Theme.spacing
            readonly property bool learning: dialog.loops.learning === dialog.loops.knobControl
            Label {
                Layout.preferredWidth: 160
                text: qsTr("Choose the instrument")
                color: Theme.text
            }
            Label {
                objectName: "loopSelectorText"
                Layout.fillWidth: true
                text: knobRow.learning ? qsTr("Turn the knob or wheel a little…")
                                       : dialog.loops.selectorText !== "" ? dialog.loops.selectorText : qsTr("Not set")
                color: knobRow.learning ? Theme.accent : dialog.loops.selectorText !== "" ? Theme.text : Theme.textDim
                elide: Text.ElideRight
            }
            StageButton {
                objectName: "loopSelectorLearn"
                text: knobRow.learning ? qsTr("Cancel") : qsTr("Learn")
                highlighted: knobRow.learning
                onClicked: knobRow.learning ? dialog.loops.cancelLearning() : dialog.loops.learn(dialog.loops.knobControl)
            }
            StageButton {
                text: "✕"
                enabled: dialog.loops.selectorText !== ""
                tip: qsTr("Forget it")
                onClicked: dialog.loops.forget(dialog.loops.knobControl)
            }
        }
        RowLayout {
            Layout.leftMargin: Theme.spacingLarge + 160 + Theme.spacing
            visible: dialog.loops.selectorText !== ""
            spacing: Theme.spacing
            Label { text: qsTr("It is"); color: Theme.textDim }
            StageComboBox {
                objectName: "loopSelectorMode"
                implicitWidth: 280
                model: [qsTr("A knob with a range (turn to choose)"), qsTr("An endless encoder (steps)"),
                        qsTr("An endless encoder (64 = still)")]
                currentIndex: dialog.loops.selectorMode
                onActivated: (index) => dialog.loops.selectorMode = index
            }
        }

        StageDivider { Layout.fillWidth: true; Layout.topMargin: Theme.spacing }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            Item { Layout.fillWidth: true }
            StageButton { text: qsTr("Done"); tone: "accent"; onClicked: dialog.close() }
        }
    }
}
