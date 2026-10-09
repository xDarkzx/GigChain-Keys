pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// External gear: the hardware synths' sounds the song calls up when it comes
// up. Each: a MIDI output, its channel, the program (1-128) and, if the synth
// wants it, a bank. A change is sent at once, so the synth can be heard.
StageDialog {
    id: dialog

    required property DocumentController doc
    property var programs: []
    property var outputs: []

    title: qsTr("External gear: %1").arg(dialog.doc.currentSongName)
    width: 640
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
        dialog.outputs = dialog.doc.midiOutputs()
        dialog.programs = dialog.doc.externalPrograms()
    }

    // One field of one row changed: the whole list set (and sent).
    function change(row, field, value) {
        const list = dialog.programs.map((p) => Object.assign({}, p))
        list[row][field] = value
        if (dialog.doc.setExternalPrograms(list)) dialog.programs = dialog.doc.externalPrograms()
    }

    ColumnLayout {
        width: parent.width
        spacing: 10

        Label {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 0
            text: qsTr("When this song comes up, each synth below is sent its sound (a Program Change, and a bank select "
                       + "when it needs one) on its MIDI output.")
            color: Theme.textDim
            wrapMode: Text.Wrap
        }
        Label {
            Layout.leftMargin: 20
            visible: dialog.outputs.length === 0
            text: qsTr("No MIDI outputs: plug the synth in (USB or a MIDI interface) and open this again.")
            color: Theme.warning
        }

        Repeater {
            model: dialog.programs
            delegate: RowLayout {
                id: gearRow
                required property var modelData
                required property int index
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 8
                StageComboBox {
                    objectName: "gearPort"
                    Layout.fillWidth: true
                    // Its output even when unplugged (it is sent when it is back).
                    model: dialog.outputs.indexOf(gearRow.modelData.port) >= 0 ? dialog.outputs
                                                                                : dialog.outputs.concat([gearRow.modelData.port])
                    currentIndex: model.indexOf(gearRow.modelData.port)
                    onActivated: (i) => dialog.change(gearRow.index, "port", model[i])
                }
                Label { text: qsTr("Ch") }
                StageSpinBox {
                    objectName: "gearChannel"
                    from: 1; to: 16
                    value: gearRow.modelData.midiChannel
                    onValueModified: dialog.change(gearRow.index, "midiChannel", value)
                }
                Label { text: qsTr("Program") }
                StageSpinBox {
                    objectName: "gearProgram"
                    from: 1; to: 128
                    editable: true
                    value: gearRow.modelData.program + 1
                    onValueModified: dialog.change(gearRow.index, "program", value - 1)
                }
                CheckBox {
                    id: bankOn
                    text: qsTr("Bank")
                    checked: gearRow.modelData.bank >= 0
                    focusPolicy: Qt.NoFocus
                    onToggled: dialog.change(gearRow.index, "bank", bankOn.checked ? 0 : -1)
                }
                StageSpinBox {
                    objectName: "gearBank"
                    visible: gearRow.modelData.bank >= 0
                    from: 0; to: 16383
                    editable: true
                    value: Math.max(gearRow.modelData.bank, 0)
                    onValueModified: dialog.change(gearRow.index, "bank", value)
                }
                StageButton {
                    iconSource: "icons/x.svg"
                    iconSize: 12
                    tip: qsTr("Remove this synth")
                    onClicked: {
                        const list = dialog.programs.filter((_, i) => i !== gearRow.index)
                        if (dialog.doc.setExternalPrograms(list)) dialog.programs = dialog.doc.externalPrograms()
                    }
                }
            }
        }

        StageDivider { Layout.fillWidth: true; Layout.topMargin: 4 }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0
            StageButton {
                objectName: "addGear"
                text: qsTr("Add a synth")
                iconSource: "icons/plus.svg"
                enabled: dialog.outputs.length > 0 && dialog.programs.length < 4
                onClicked: {
                    const list = dialog.programs.concat([{ port: dialog.outputs[0], midiChannel: 1, program: 0, bank: -1 }])
                    if (dialog.doc.setExternalPrograms(list)) dialog.programs = dialog.doc.externalPrograms()
                }
            }
            Item { Layout.fillWidth: true }
            StageButton {
                text: qsTr("Done")
                tone: "accent"
                onClicked: dialog.close()
            }
        }
    }
}
