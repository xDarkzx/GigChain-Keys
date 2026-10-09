pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Where a channel sits on the keyboard: its keys (a split), how hard a key
// must be played for it to sound (a velocity layer), transposition and the
// MIDI channel it listens to. Changes apply at once (and can be undone).
StageDialog {
    id: dialog

    required property DocumentController doc
    required property SelectedChannel channel

    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    function noteName(n) { return noteNames[n % 12] + (Math.floor(n / 12) - 1) }

    title: qsTr("Keyboard zone: %1").arg(dialog.channel.name)
    width: 460
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    ColumnLayout {
        width: parent.width
        spacing: 12

        SettingsSection { title: qsTr("Keys it plays") }
        SettingsRow {
            label: qsTr("Lowest key")
            StageSpinBox {
                objectName: "keyLowBox"
                from: 0; to: 127
                value: dialog.channel.keyLow
                editable: false
                textFromValue: (v) => dialog.noteName(v)
                onValueModified: dialog.doc.setChannelKeyRange(dialog.channel.index, value, Math.max(value, dialog.channel.keyHigh))
            }
        }
        SettingsRow {
            label: qsTr("Highest key")
            StageSpinBox {
                objectName: "keyHighBox"
                from: 0; to: 127
                value: dialog.channel.keyHigh
                editable: false
                textFromValue: (v) => dialog.noteName(v)
                onValueModified: dialog.doc.setChannelKeyRange(dialog.channel.index, Math.min(value, dialog.channel.keyLow), value)
            }
        }
        SettingsRow {
            label: qsTr("Transpose")
            StageSpinBox {
                objectName: "transposeBox"
                from: -48; to: 48
                value: dialog.channel.transpose
                textFromValue: (v) => (v > 0 ? "+" : "") + v + qsTr(" semitones")
                onValueModified: dialog.doc.setChannelTranspose(dialog.channel.index, value)
            }
        }

        StageDivider { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }
        SettingsSection { title: qsTr("How hard (velocity layer)") }
        SettingsRow {
            label: qsTr("From")
            StageSpinBox {
                objectName: "velocityLowBox"
                from: 1; to: 127
                value: dialog.channel.velocityLow
                onValueModified: dialog.doc.setChannelVelocityRange(dialog.channel.index, value, Math.max(value, dialog.channel.velocityHigh))
            }
        }
        SettingsRow {
            label: qsTr("To")
            StageSpinBox {
                objectName: "velocityHighBox"
                from: 1; to: 127
                value: dialog.channel.velocityHigh
                onValueModified: dialog.doc.setChannelVelocityRange(dialog.channel.index, Math.min(value, dialog.channel.velocityLow), value)
            }
        }
        Label {
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.fillWidth: true
            text: qsTr("Two channels on the same keys with different ranges make a velocity layer: "
                       + "a soft touch plays one sound, a hard hit the other.")
            color: Theme.textDim
            wrapMode: Text.Wrap
        }

        StageDivider { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }
        SettingsSection { title: qsTr("MIDI") }
        SettingsRow {
            label: qsTr("Listens to")
            StageComboBox {
                objectName: "midiChannelBox"
                model: [qsTr("All channels")].concat(Array.from({ length: 16 }, (_, c) => qsTr("Channel %1").arg(c + 1)))
                currentIndex: dialog.channel.midiChannel
                onActivated: (i) => dialog.doc.setChannelMidiChannel(dialog.channel.index, i)
            }
        }
        // What it takes from the keyboard: a pad can ignore the sustain
        // pedal while the piano holds, or only one layer take the expression pedal.
        SettingsRow {
            label: qsTr("Takes")
            Flow {
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    // (A fixed list: each box reads its state from the channel, so a tick keeps the boxes.)
                    model: [
                        { what: "sustain", text: qsTr("Sustain pedal"), property: "takesSustain" },
                        { what: "expression", text: qsTr("Expression pedal"), property: "takesExpression" },
                        { what: "modWheel", text: qsTr("Mod wheel"), property: "takesModWheel" },
                        { what: "pitchBend", text: qsTr("Pitch bend"), property: "takesPitchBend" },
                        { what: "aftertouch", text: qsTr("Aftertouch"), property: "takesAftertouch" }
                    ]
                    delegate: CheckBox {
                        id: takes
                        required property var modelData
                        objectName: "takes_" + takes.modelData.what
                        text: takes.modelData.text
                        checked: dialog.channel[takes.modelData.property]
                        focusPolicy: Qt.NoFocus
                        onToggled: dialog.doc.setChannelTakes(dialog.channel.index, takes.modelData.what, takes.checked)
                    }
                }
            }
        }

        // MIDI effects: one key plays a chord; held keys play as an arpeggio on the song's tempo.
        StageDivider { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }
        SettingsSection { title: qsTr("MIDI effects") }
        SettingsRow {
            label: qsTr("One key plays")
            StageComboBox {
                objectName: "chordBox"
                implicitWidth: 200
                model: [qsTr("Just the key"), qsTr("A major chord"), qsTr("A minor chord"), qsTr("A power chord (5th + octave)"),
                        qsTr("Octaves"), qsTr("A sus2 chord"), qsTr("A sus4 chord"), qsTr("A seventh chord")]
                currentIndex: dialog.channel.chord
                onActivated: (i) => dialog.doc.setChannelMidiEffect(dialog.channel.index, "chord", i)
            }
        }
        SettingsRow {
            label: qsTr("Arpeggiator")
            StageComboBox {
                objectName: "arpeggioBox"
                implicitWidth: 140
                model: [qsTr("Off"), qsTr("Up"), qsTr("Down"), qsTr("Up and down"), qsTr("As played")]
                currentIndex: dialog.channel.arpeggio
                onActivated: (i) => dialog.doc.setChannelMidiEffect(dialog.channel.index, "arpeggio", i)
            }
            StageComboBox {
                objectName: "arpRateBox"
                implicitWidth: 120
                enabled: dialog.channel.arpeggio > 0
                model: [qsTr("1/4"), qsTr("1/8"), qsTr("1/8 triplet"), qsTr("1/16")]
                currentIndex: dialog.channel.arpRate
                onActivated: (i) => dialog.doc.setChannelMidiEffect(dialog.channel.index, "arpRate", i)
            }
            StageComboBox {
                objectName: "arpOctavesBox"
                implicitWidth: 120
                enabled: dialog.channel.arpeggio > 0
                model: [qsTr("1 octave"), qsTr("2 octaves"), qsTr("3 octaves")]
                currentIndex: dialog.channel.arpOctaves - 1
                onActivated: (i) => dialog.doc.setChannelMidiEffect(dialog.channel.index, "arpOctaves", i + 1)
            }
        }

        StageDivider { Layout.fillWidth: true; Layout.topMargin: 4 }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0
            StageButton {
                text: qsTr("Whole keyboard")
                onClicked: {
                    dialog.doc.setChannelKeyRange(dialog.channel.index, 0, 127)
                    dialog.doc.setChannelVelocityRange(dialog.channel.index, 1, 127)
                    dialog.doc.setChannelTranspose(dialog.channel.index, 0)
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
