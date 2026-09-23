import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Settings for the current patch and the selected channel.
Rectangle {
    id: inspector

    required property DocumentController doc
    required property SelectedChannel selectedChannel

    color: Theme.panel

    function noteName(n) {
        const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        return names[n % 12] + (Math.floor(n / 12) - 2)
    }

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 2
        spacing: Theme.spacing * 4

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Label { text: qsTr("PATCH"); color: Theme.textDim; font.bold: true }
            TextField {
                objectName: "inspectorPatchName"
                Layout.preferredWidth: 220
                enabled: inspector.doc.hasPatch
                text: inspector.doc.currentPatchName
                onEditingFinished: {
                    if (text !== inspector.doc.currentPatchName)
                        inspector.doc.renamePatch(inspector.doc.songIndex, inspector.doc.patchIndex, text)
                    focus = false // hand the keyboard back to navigation shortcuts
                }
                Keys.onEscapePressed: {
                    text = inspector.doc.currentPatchName
                    focus = false
                }
            }
        }

        GridLayout {
            Layout.alignment: Qt.AlignTop
            visible: inspector.selectedChannel.valid
            columns: 4
            columnSpacing: Theme.spacing * 2
            rowSpacing: 4

            Label { text: qsTr("CHANNEL"); color: Theme.textDim; font.bold: true; Layout.columnSpan: 4 }

            Label { text: qsTr("Name") }
            TextField {
                Layout.preferredWidth: 180
                text: inspector.selectedChannel.name
                onEditingFinished: {
                    if (text !== inspector.selectedChannel.name)
                        inspector.doc.setChannelName(inspector.selectedChannel.index, text)
                    focus = false
                }
                Keys.onEscapePressed: {
                    text = inspector.selectedChannel.name
                    focus = false
                }
            }
            Label { text: qsTr("Transpose") }
            SpinBox {
                id: transpose
                from: -48
                to: 48
                value: inspector.selectedChannel.transpose
                onValueModified: {
                    inspector.doc.setChannelTranspose(inspector.selectedChannel.index, value)
                    value = Qt.binding(() => inspector.selectedChannel.transpose) // show what was accepted
                }
            }

            Label { text: qsTr("Key range") }
            RowLayout {
                SpinBox {
                    from: 0
                    to: 127
                    editable: false
                    value: inspector.selectedChannel.keyLow
                    textFromValue: (v) => inspector.noteName(v)
                    onValueModified: {
                        inspector.doc.setChannelKeyRange(inspector.selectedChannel.index, value, inspector.selectedChannel.keyHigh)
                        value = Qt.binding(() => inspector.selectedChannel.keyLow)
                    }
                }
                Label { text: "–" }
                SpinBox {
                    from: 0
                    to: 127
                    editable: false
                    value: inspector.selectedChannel.keyHigh
                    textFromValue: (v) => inspector.noteName(v)
                    onValueModified: {
                        inspector.doc.setChannelKeyRange(inspector.selectedChannel.index, inspector.selectedChannel.keyLow, value)
                        value = Qt.binding(() => inspector.selectedChannel.keyHigh)
                    }
                }
            }
            Label { text: qsTr("MIDI channel") }
            SpinBox {
                from: 0
                to: 16
                editable: false
                value: inspector.selectedChannel.midiChannel
                textFromValue: (v) => v === 0 ? qsTr("Omni") : String(v)
                onValueModified: {
                    inspector.doc.setChannelMidiChannel(inspector.selectedChannel.index, value)
                    value = Qt.binding(() => inspector.selectedChannel.midiChannel)
                }
            }
        }

        Label {
            visible: !inspector.selectedChannel.valid
            Layout.alignment: Qt.AlignTop
            color: Theme.textDim
            text: qsTr("Select a channel in the mixer to edit it")
        }

        Item { Layout.fillWidth: true }
    }
}
