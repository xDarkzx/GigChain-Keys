pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// The current song's chart, edited where it is read: click into the words
// and type, click above a word to put a chord on it, drag chords onto other
// words, double-click a section's title to rename it, + Section for a new
// one. Chords to drop on the words: the song's own (the palette), or one
// typed in the chord box. Paste a chord sheet (Ctrl+V) or drop a downloaded
// text file and it is cleaned up into the chart. The ChordPro text itself
// is under ⋯ > Edit as text.
Rectangle {
    id: panel

    required property DocumentController doc
    required property EngineStatus engineStatus
    property bool editing: false // the ChordPro text editor (⋯ > Edit as text)

    readonly property bool empty: panel.doc.currentChart.trim() === ""
    // Typing in the words, a chord or a title: Ctrl+V pastes there, not a new chart.
    readonly property bool typing: panel.Window.activeFocusItem !== null
                                   && panel.Window.activeFocusItem.hasOwnProperty("cursorPosition")

    color: Theme.background

    Shortcut {
        sequences: [StandardKey.Paste]
        enabled: panel.visible && !panel.editing && !panel.typing
        onActivated: panel.doc.pasteChartFromClipboard(panel.doc.songIndex)
    }

    // A chord's "How to play" (its menu): its diagram.
    ChordDiagram {
        id: editDiagram
        doc: panel.doc
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import a chart")
        nameFilters: [qsTr("Chord charts (*.txt *.cho *.chopro *.chordpro *.crd *.pro *.onsong)"), qsTr("All files (*)")]
        onAccepted: panel.doc.importChartFile(panel.doc.songIndex, selectedFile)
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: (drop) => {
            if (drop.hasUrls && drop.urls.length > 0) {
                panel.doc.importChartFile(panel.doc.songIndex, drop.urls[0])
                drop.accept()
            }
        }
    }

    // A line of words typed at the end of the chart (also the first one).
    function appendLine(text) {
        let chart = panel.doc.currentChart
        while (chart.length > 0 && /\s/.test(chart[chart.length - 1])) chart = chart.slice(0, -1)
        const next = chart === "" ? text : chart + "\n" + text
        panelChart.focusRequest = { line: next.split("\n").length - 1, cursor: text.length }
        panel.doc.setSongChart(panel.doc.songIndex, next)
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
                objectName: "addSectionButton"
                visible: !panel.editing && panel.doc.songIndex >= 0
                text: qsTr("Section")
                iconSource: "icons/plus.svg"
                tip: qsTr("Add a section at the end (Verse, Chorus…)")
                onClicked: sectionMenu.popup(0, height)
                StageMenu {
                    id: sectionMenu
                    objectName: "addSectionMenu"
                    Repeater {
                        model: [qsTr("Intro"), qsTr("Verse"), qsTr("Pre-Chorus"), qsTr("Chorus"), qsTr("Bridge"),
                                qsTr("Solo"), qsTr("Outro")]
                        delegate: StageMenuItem {
                            required property string modelData
                            text: modelData
                            onTriggered: {
                                panel.doc.addChartSection(modelData)
                                chartScroll.toEnd()
                            }
                        }
                    }
                }
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
                objectName: "chartTextDone"
                visible: panel.editing
                text: qsTr("Done")
                highlighted: true
                onClicked: {
                    panel.doc.setSongChart(panel.doc.songIndex, editor.text)
                    panel.editing = false
                }
            }
            StageButton {
                objectName: "chartMoreButton"
                visible: !panel.editing
                text: "⋯"
                tip: qsTr("More")
                onClicked: moreMenu.popup(0, height)
                StageMenu {
                    id: moreMenu
                    StageMenuItem {
                        objectName: "editAsTextItem"
                        text: qsTr("Edit as text (ChordPro)")
                        onTriggered: {
                            editor.text = panel.doc.currentChart
                            panel.editing = true
                        }
                    }
                }
            }
        }

        // The chords to drop on the words: one typed here, or the song's own.
        Rectangle {
            objectName: "chordPalette"
            z: 2 // a chord dragged out of it shows over the chart
            visible: !panel.editing && panel.doc.songIndex >= 0
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            radius: Theme.radiusCard
            color: Theme.readoutBackground
            border.color: Theme.outline
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: Theme.spacing
                TextField {
                    id: chordField
                    objectName: "chordField"
                    Layout.preferredWidth: 110
                    placeholderText: qsTr("Chord…")
                    font.bold: true
                    color: Theme.chord
                    ToolTip.visible: hovered && text === ""
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Type a chord (Gm, C#m7, D/F#), then drag it onto a word")
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Row {
                        id: chips
                        objectName: "paletteChords"
                        anchors.verticalCenter: parent.verticalCenter
                        height: parent.height
                        spacing: 6
                        Repeater {
                            model: {
                                const typed = chordField.text.trim()
                                const list = panel.doc.currentChartChords.slice()
                                if (typed !== "" && list.indexOf(typed) < 0) list.unshift(typed)
                                return list
                            }
                            // (A slot the Row places; the chip inside it moves freely when dragged.)
                            delegate: Item {
                                id: slot
                                required property string modelData
                                anchors.verticalCenter: parent.verticalCenter
                                width: paletteChip.width
                                height: paletteChip.height
                            ChordDragSource {
                                id: paletteChip
                                objectName: "paletteChord"
                                // What a drop needs to know: a new chord, by name.
                                chordName: slot.modelData
                                width: chipText.implicitWidth + 16
                                height: 30
                                radius: Theme.radiusSmall
                                color: paletteDrag.active ? Theme.accent : Theme.buttonTop
                                border.color: Theme.chord
                                z: paletteDrag.active ? 10 : 1
                                Drag.active: paletteDrag.active
                                Drag.keys: ["chord"]
                                Drag.hotSpot.x: 6
                                Drag.hotSpot.y: height / 2
                                Text {
                                    id: chipText
                                    anchors.centerIn: parent
                                    text: slot.modelData
                                    color: paletteDrag.active ? "white" : Theme.chord
                                    font.pixelSize: Theme.fontSize + 2
                                    font.bold: true
                                }
                                HoverHandler { cursorShape: Qt.OpenHandCursor }
                                DragHandler {
                                    id: paletteDrag
                                    cursorShape: Qt.ClosedHandCursor
                                    onActiveChanged: {
                                        if (active) return
                                        paletteChip.Drag.drop()
                                        paletteChip.x = 0 // back in its slot
                                        paletteChip.y = 0
                                    }
                                }
                            }
                            }
                        }
                    }
                }
                // A chord dragged here is taken off the words.
                Rectangle {
                    objectName: "chordRemoveZone"
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 30
                    radius: Theme.radiusSmall
                    color: removeDrop.containsDrag ? Theme.danger : "transparent"
                    border.color: removeDrop.containsDrag ? Theme.danger : Theme.outline
                    Row {
                        anchors.centerIn: parent
                        spacing: 4
                        Image { source: "icons/x.svg"; width: 14; height: 14; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: qsTr("Remove")
                            color: removeDrop.containsDrag ? "white" : Theme.textDim
                            font.pixelSize: Theme.smallFontSize
                        }
                    }
                    DropArea {
                        id: removeDrop
                        anchors.fill: parent
                        keys: ["chord"]
                        onDropped: (drop) => {
                            const source = drop.source as ChordDragSource
                            if (source === null || source.fromLine < 0) return // (from the palette: nothing to remove)
                            drop.accept()
                            panel.doc.renameChordAt(source.fromLine, source.chordIndex, "")
                        }
                    }
                    ToolTip.visible: removeHover.hovered
                    ToolTip.text: qsTr("Drag a chord here to take it off the words")
                    HoverHandler { id: removeHover }
                }
            }
        }

        // The chart, edited where it is read.
        ScrollView {
            id: chartScroll
            objectName: "chartScroll"
            visible: !panel.editing
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            // Dragging the mouse moves chords, not the page. (A Flickable that
            // is not interactive ignores the wheel too: the wheel is turned
            // into scrolling here, as a page scrolls.)
            Component.onCompleted: contentItem.interactive = false
            WheelHandler {
                objectName: "chartWheel"
                target: null
                onWheel: (event) => {
                    const page = chartScroll.contentItem as Flickable
                    if (page === null) return
                    const step = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 120 * 3 * Theme.fontSize * 2
                    page.contentY = Math.max(0, Math.min(page.contentHeight - page.height, page.contentY - step))
                }
            }
            function toEnd() {
                Qt.callLater(() => chartScroll.ScrollBar.vertical.position = 1.0 - chartScroll.ScrollBar.vertical.size)
            }

            // Chord follow: the line being played stays in the upper third.
            NumberAnimation {
                id: followScroll
                target: chartScroll.contentItem
                property: "contentY"
                duration: 250
                easing.type: Easing.OutCubic
            }
            Connections {
                target: panelChart
                function onFollowYChanged() {
                    if (panelChart.followY < 0) return
                    followScroll.to = Math.max(0, Math.min(panelChart.followY - chartScroll.height / 3,
                                                           chartScroll.contentHeight - chartScroll.height))
                    followScroll.restart()
                }
            }

            Column {
                width: parent.width
                spacing: 2

                Label {
                    objectName: "chartEmpty"
                    visible: panel.empty
                    width: parent.width
                    topPadding: 30
                    bottomPadding: 10
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: panel.doc.songIndex < 0
                          ? qsTr("This setlist has no songs yet.\n\nCopy a song's chords and lyrics from any chord site or file "
                                 + "and press Ctrl+V (or Paste chords): it becomes your first song, named from the sheet.")
                          : qsTr("No chart for this song yet. Type the words below, then click above a word to add a chord, "
                                 + "or paste a chord sheet (Ctrl+V) or drop a downloaded text file here.")
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSize + 2
                }

                ChartView {
                    id: panelChart
                    objectName: "chartView"
                    width: parent.width
                    liveEdit: true
                    // Enter at the very end of the song: no line there (blank
                    // lines at the end are not shown), so the new-line box takes the caret.
                    onFocusRequestChanged: Qt.callLater(() => {
                        const asked = panelChart.focusRequest
                        if (asked === null || asked === undefined) return
                        const shown = panelChart.lines
                        const last = shown.length > 0 ? shown[shown.length - 1].line : -1
                        if (asked.line <= last) return
                        panelChart.focusRequest = null
                        newLine.forceActiveFocus()
                    })
                    lines: panel.doc.chartLines(panel.doc.currentChart)
                    doc: panel.doc
                    currentSection: panel.engineStatus.songSection
                    playing: panel.engineStatus.songPlaying
                    bar: panel.engineStatus.songBar
                    currentStep: panel.engineStatus.chordStep
                    followStarted: panel.engineStatus.chordStarted
                    onChordClicked: (name) => editDiagram.show(name)
                }

                // A new line of words at the end (the first one in an empty chart).
                TextInput {
                    id: newLine
                    objectName: "newChartLine"
                    visible: panel.doc.songIndex >= 0
                    width: parent.width
                    topPadding: 18
                    bottomPadding: 18
                    horizontalAlignment: TextInput.AlignHCenter
                    color: Theme.text
                    font.pixelSize: Theme.fontSize + 7
                    selectByMouse: true
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: newLine.topPadding
                        visible: newLine.text === ""
                        text: newLine.activeFocus ? qsTr("Type the words…") : qsTr("+ Click to type a new line")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontSize + 3
                    }
                    onTextEdited: {
                        const typed = newLine.text
                        newLine.text = ""
                        panel.appendLine(typed)
                    }
                }
            }
        }

        // ⋯ > Edit as text: the ChordPro text, chords in [brackets] before the word.
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
