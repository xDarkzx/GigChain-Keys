import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// The current song's chart: lyrics with the chords above the words where
// they change. Paste a chord sheet (Ctrl+V) or drop a downloaded text file
// and it is cleaned up into the chart; Edit to change anything by hand.
Rectangle {
    id: panel

    required property DocumentController doc
    property bool editing: false

    color: Theme.background

    // Paste only when not typing in the editor (there Ctrl+V pastes text).
    Shortcut {
        sequences: [StandardKey.Paste]
        enabled: panel.visible && !panel.editing
        onActivated: panel.doc.pasteChartFromClipboard(panel.doc.songIndex)
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import a chart")
        nameFilters: [qsTr("Chord charts (*.txt *.cho *.chopro *.chordpro *.crd *.pro *.onsong)"), qsTr("All files (*)")]
        onAccepted: panel.doc.importChartFile(panel.doc.songIndex, selectedFile)
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls && drop.urls.length > 0) {
                panel.doc.importChartFile(panel.doc.songIndex, drop.urls[0])
                drop.accept()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 2
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            Label {
                Layout.fillWidth: true
                text: panel.doc.currentSongName
                font.pixelSize: Theme.headerFontSize
                font.bold: true
                elide: Text.ElideRight
            }
            StageButton {
                objectName: "pasteChartButton"
                text: qsTr("Paste chords")
                visible: !panel.editing
                onClicked: panel.doc.pasteChartFromClipboard(panel.doc.songIndex)
                tip: qsTr("Copy a song's chords and lyrics from any site or file, then paste (Ctrl+V)")
            }
            StageButton {
                text: qsTr("Import file…")
                visible: !panel.editing
                onClicked: importDialog.open()
            }
            StageButton {
                objectName: "editChartButton"
                text: panel.editing ? qsTr("Done") : qsTr("Edit")
                highlighted: panel.editing
                onClicked: {
                    if (panel.editing) panel.doc.setSongChart(panel.doc.songIndex, editor.text)
                    else editor.text = panel.doc.currentChart
                    panel.editing = !panel.editing
                }
            }
        }

        StageDivider { Layout.fillWidth: true }

        // Just pasted: the site's clutter was removed; one click brings back
        // exactly what was pasted.
        Rectangle {
            objectName: "pasteUndoBar"
            visible: panel.doc.canUndoPaste && !panel.editing
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            radius: Theme.radiusCard
            border.color: Theme.outline
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.barTop }
                GradientStop { position: 1.0; color: Theme.barBottom }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 6
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Cleaned up: the site's extras were removed and chords placed over the words.")
                    color: Theme.textDim
                    elide: Text.ElideRight
                }
                StageButton {
                    objectName: "undoPasteButton"
                    text: qsTr("Undo")
                    iconSource: "icons/undo.svg"
                    onClicked: panel.doc.undoPaste()
                }
            }
        }

        // The chart as the player reads it.
        ScrollView {
            visible: !panel.editing
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            Column {
                width: parent.width
                spacing: 2

                Label {
                    objectName: "chartEmpty"
                    visible: panel.doc.currentChart.trim() === ""
                    width: parent.width
                    topPadding: 40
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: panel.doc.songIndex < 0
                          ? qsTr("This setlist has no songs yet.\n\nCopy a song's chords and lyrics from any chord site or file "
                                 + "and press Ctrl+V (or Paste chords): it becomes your first song, named from the sheet.")
                          : qsTr("No chart for this song yet.\n\nCopy the chords and lyrics from any chord site or file and "
                                 + "press Ctrl+V (or Paste chords), or drop a downloaded text file here. "
                                 + "Spacing and tab lines are cleaned up for you.")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSize + 2
                }

                ChartView {
                    width: parent.width
                    lines: panel.doc.chartLines(panel.doc.currentChart)
                }
            }
        }

        // Editing: ChordPro text, chords in [brackets] before the word.
        ScrollView {
            visible: panel.editing
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: editor
                objectName: "chartEditor"
                font.family: "Consolas"
                font.pixelSize: Theme.fontSize + 2
                wrapMode: TextEdit.NoWrap
                placeholderText: qsTr("[Dm]I love [C#m7]you so much[D/E]\n\nPut a chord in [brackets] right before the word it changes on.")
                background: Rectangle {
                    color: Theme.readoutBackground
                    border.color: editor.activeFocus ? Theme.accent : Theme.outline
                    radius: Theme.radiusSmall
                    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelDark }
                }
            }
        }
        Label {
            visible: panel.editing
            Layout.fillWidth: true
            text: qsTr("Chords go in [brackets] just before the word they change on. Lines like {comment: Chorus} label a section.")
            color: Theme.textDim
            font.pixelSize: Theme.smallFontSize
            wrapMode: Text.Wrap
        }
    }
}
