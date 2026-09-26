import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Where a channel sits on the keyboard: its keys (a split), how hard a key
// must be played for it to sound (a velocity layer), transposition and the
// MIDI channel it listens to. Changes apply at once (and can be undone).
Popup {
    id: dialog

    required property DocumentController doc
    required property SelectedChannel channel

    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    function noteName(n) { return noteNames[n % 12] + (Math.floor(n / 12) - 1) }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.border
        radius: 8
    }
    Overlay.modal: Rectangle { color: "#99000000" }

    ColumnLayout {
        width: parent.width
        spacing: 12

        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            leftPadding: 16
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Keyboard zone: %1").arg(dialog.channel.name)
            font.pixelSize: 16
            font.bold: true
            elide: Text.ElideRight
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

        SettingsSection { title: qsTr("Keys it plays") }
        SettingsRow {
            label: qsTr("Lowest key")
            SpinBox {
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
            SpinBox {
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
            SpinBox {
                objectName: "transposeBox"
                from: -48; to: 48
                value: dialog.channel.transpose
                textFromValue: (v) => (v > 0 ? "+" : "") + v + qsTr(" semitones")
                onValueModified: dialog.doc.setChannelTranspose(dialog.channel.index, value)
            }
        }

        SettingsSection { title: qsTr("How hard (velocity layer)") }
        SettingsRow {
            label: qsTr("From")
            SpinBox {
                objectName: "velocityLowBox"
                from: 1; to: 127
                value: dialog.channel.velocityLow
                onValueModified: dialog.doc.setChannelVelocityRange(dialog.channel.index, value, Math.max(value, dialog.channel.velocityHigh))
            }
        }
        SettingsRow {
            label: qsTr("To")
            SpinBox {
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

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Button {
                text: qsTr("Whole keyboard")
                focusPolicy: Qt.NoFocus
                onClicked: {
                    dialog.doc.setChannelKeyRange(dialog.channel.index, 0, 127)
                    dialog.doc.setChannelVelocityRange(dialog.channel.index, 1, 127)
                    dialog.doc.setChannelTranspose(dialog.channel.index, 0)
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Done")
                highlighted: true
                focusPolicy: Qt.NoFocus
                onClicked: dialog.close()
            }
        }
    }
}
