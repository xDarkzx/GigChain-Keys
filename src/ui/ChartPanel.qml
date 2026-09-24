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
            Button {
                objectName: "pasteChartButton"
                text: qsTr("Paste chords")
                visible: !panel.editing
                focusPolicy: Qt.NoFocus
                onClicked: panel.doc.pasteChartFromClipboard(panel.doc.songIndex)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy a song's chords and lyrics from any site or file, then paste (Ctrl+V)")
            }
            Button {
                text: qsTr("Import file…")
                visible: !panel.editing
                focusPolicy: Qt.NoFocus
                onClicked: importDialog.open()
            }
            Button {
                objectName: "editChartButton"
                text: panel.editing ? qsTr("Done") : qsTr("Edit")
                highlighted: panel.editing
                focusPolicy: Qt.NoFocus
                onClicked: {
                    if (panel.editing) panel.doc.setSongChart(panel.doc.songIndex, editor.text)
                    else editor.text = panel.doc.currentChart
                    panel.editing = !panel.editing
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
                    text: qsTr("No chart for this song yet.\n\nCopy the chords and lyrics from any chord site or file and "
                               + "press Ctrl+V (or Paste chords), or drop a downloaded text file here. "
                               + "Spacing and tab lines are cleaned up for you.")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSize + 2
                }

                Repeater {
                    objectName: "chartLines"
                    model: panel.doc.chartLines(panel.doc.currentChart)
                    delegate: Loader {
                        id: lineLoader
                        required property var modelData
                        width: parent ? parent.width : 0
                        sourceComponent: modelData.kind === "lyrics" ? lyricLine
                                       : modelData.kind === "section" ? sectionLine
                                       : modelData.kind === "comment" ? commentLine
                                       : blankLine
                        Component {
                            id: lyricLine
                            // Each segment: its chord above its words.
                            Flow {
                                width: lineLoader.width
                                Repeater {
                                    model: lineLoader.modelData.segments
                                    delegate: Column {
                                        required property var modelData
                                        Text {
                                            text: modelData.chord !== "" ? modelData.chord : " "
                                            color: Theme.accent
                                            font.pixelSize: Theme.fontSize + 5
                                            font.bold: true
                                            rightPadding: modelData.text === "" ? 8 : 0
                                        }
                                        Text {
                                            text: modelData.text !== "" ? modelData.text : " "
                                            color: Theme.text
                                            font.pixelSize: Theme.fontSize + 7
                                        }
                                    }
                                }
                            }
                        }
                        Component {
                            id: sectionLine
                            Text {
                                topPadding: 10
                                text: lineLoader.modelData.label
                                color: Theme.accentBlue
                                font.pixelSize: Theme.fontSize + 3
                                font.bold: true
                            }
                        }
                        Component {
                            id: commentLine
                            Text {
                                topPadding: 8
                                text: lineLoader.modelData.label
                                color: Theme.accentBlue
                                font.pixelSize: Theme.fontSize + 3
                                font.bold: true
                            }
                        }
                        Component {
                            id: blankLine
                            Item { height: 14 }
                        }
                    }
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
                background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: Theme.radius }
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
