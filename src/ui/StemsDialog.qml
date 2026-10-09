pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// The song's stems (MainStage's multi-track playback): more tracks played
// locked to its backing track, each at its own level and on its own
// outputs (a click and a guide to the in-ears, the band to the desk).
// A level, mute or output change is heard at once.
StageDialog {
    id: dialog

    required property DocumentController doc

    title: qsTr("Stems: %1").arg(dialog.doc.currentSongName)
    width: 680
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property var outputChoices: {
        const list = [qsTr("Main mix (1-2)")]
        for (let p = 1; p <= 7; ++p) list.push(qsTr("Outputs %1-%2").arg(2 * p + 1).arg(2 * p + 2))
        return list
    }

    ColumnLayout {
        width: parent.width
        spacing: 10

        Label {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 0
            text: dialog.doc.songBackingTrack !== ""
                  ? qsTr("Stems play with the backing track (%1), from the same place. Stems on their own outputs skip the "
                         + "master fader: the in-ears keep their click when the band is turned down.").arg(dialog.doc.songBackingTrack)
                  : qsTr("Stems play from the same place, together. Add the song's backing track too, or play the stems on their own.")
            color: Theme.textDim
            wrapMode: Text.Wrap
        }

        Repeater {
            model: dialog.doc.songStems
            delegate: RowLayout {
                id: stemRow
                required property var modelData
                required property int index
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 8
                function mix(volumeDb, mute, outputPair) {
                    dialog.doc.setSongStemMix(dialog.doc.songIndex, stemRow.index, volumeDb, mute, outputPair)
                }
                Label {
                    Layout.fillWidth: true
                    text: stemRow.modelData.file
                    elide: Text.ElideMiddle
                }
                StageSpinBox {
                    objectName: "stemLevel"
                    implicitWidth: 130
                    from: -60; to: 6
                    editable: true
                    value: Math.round(Math.max(stemRow.modelData.volumeDb, -60))
                    textFromValue: (v) => qsTr("%1 dB").arg(v)
                    valueFromText: (t) => parseInt(t)
                    onValueModified: stemRow.mix(value <= -60 ? -96 : value, stemRow.modelData.mute, stemRow.modelData.outputPair)
                }
                StageButton {
                    objectName: "stemMute"
                    text: qsTr("M")
                    checked: stemRow.modelData.mute
                    tip: qsTr("Mute this stem")
                    onClicked: stemRow.mix(stemRow.modelData.volumeDb, !stemRow.modelData.mute, stemRow.modelData.outputPair)
                }
                StageComboBox {
                    objectName: "stemOutput"
                    implicitWidth: 150
                    model: dialog.outputChoices
                    currentIndex: stemRow.modelData.outputPair
                    onActivated: (i) => stemRow.mix(stemRow.modelData.volumeDb, stemRow.modelData.mute, i)
                }
                StageButton {
                    iconSource: "icons/x.svg"
                    iconSize: 12
                    tip: qsTr("Remove this stem (its file stays in the setlist's folder)")
                    onClicked: dialog.doc.removeSongStem(dialog.doc.songIndex, stemRow.index)
                }
            }
        }

        StageDivider { Layout.fillWidth: true; Layout.topMargin: 4 }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0
            StageButton {
                objectName: "addStem"
                text: qsTr("Add stems…")
                iconSource: "icons/plus.svg"
                enabled: dialog.doc.songStems.length < 8
                onClicked: stemFiles.open()
            }
            Item { Layout.fillWidth: true }
            StageButton {
                text: qsTr("Done")
                tone: "accent"
                onClicked: dialog.close()
            }
        }
    }

    FileDialog {
        id: stemFiles
        title: qsTr("Stems")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Audio files (*.wav *.mp3 *.flac *.m4a *.aac *.ogg *.aiff *.aif)"), qsTr("All files (*)")]
        onAccepted: {
            for (const file of selectedFiles) {
                if (!dialog.doc.addSongStem(dialog.doc.songIndex, file)) break // (why: in the banner)
            }
        }
    }
}
