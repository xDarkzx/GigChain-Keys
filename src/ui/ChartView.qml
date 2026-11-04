pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// A song's chart as the musician reads it: each chord above the syllable it
// changes on, section headings, notes. `lines` comes from
// DocumentController.chartLines(). `size` scales everything (1 in the
// editor, bigger on stage).
//
// With `doc` set, each section title is a SectionHeader: centred and large,
// with the instruments the section plays; `currentSection`, `playing` and
// `bar` light the one in force (from EngineStatus).
//
// Edited (liveEdit), a chord dragged anywhere over the chart lands in the
// nearest sensible place, shown before it is let go: over a line of words,
// on its nearest word; over a row of chords (an intro, a solo), between
// the chords there; on an empty line, which becomes a row; on a section's
// title or below the last line, on a new row there.
Item {
    id: chart
    implicitHeight: column.implicitHeight
    // (While a chord is dragged: where over the chart, for the view around it to scroll.)
    signal dragMoved(real y)
    signal dragEnded()

    property var lines: []
    property real size: 1.0
    property bool editable: true // false on stage: section titles are read, not edited
    // Edited in place (the Chart tab): words typed in, chords dragged, titles
    // renamed. Off on stage.
    property bool liveEdit: false
    // The caret, given back to a line after an edit rebuilds them: {line,
    // cursor}, or {line, cell, what} (a chord box or a word).
    property var focusRequest: null
    // The line being edited (its empty chord boxes shown), -1: none.
    property int openLine: -1
    // A chord tapped (on stage) or asked for (its menu, in the editor): how to play it.
    signal chordClicked(string name)
    property DocumentController doc: null
    readonly property var sections: doc !== null ? doc.currentSections : []
    property int currentSection: -1
    property bool playing: false
    property int bar: 0
    // The chord the song's timeline has come to (from EngineStatus), lit.
    property int currentStep: -1
    property bool followStarted: false
    // Where the line being played is (-1: none), for the view around the
    // chart to scroll to.
    readonly property real followY: {
        const line = chart.doc !== null && chart.followStarted ? chart.doc.chordLine(chart.currentStep) : -1
        const item = line >= 0 ? lineRepeater.itemAt(line) : null
        return item !== null ? item.y : -1
    }

    // ---- Where a dragged chord lands: {kind: "word" (line, at) | "row"
    // (line, index) | "newRow" (before), and the mark's x, y, w, h}.
    function newRowAt(before, y) {
        return { kind: "newRow", before: before, x: chart.width * 0.25, y: y - 1.5, w: chart.width * 0.5, h: 3 }
    }
    function dropTargetAt(px, py) {
        const count = lineRepeater.count
        for (let i = 0; i < count; ++i) {
            const loader = lineRepeater.itemAt(i) as Loader
            const shown = chart.lines[i] // (what that line shows: the repeater's model)
            if (loader === null || shown === undefined) continue
            if (py >= loader.y + loader.height) continue
            if (py < loader.y) return chart.newRowAt(shown.line, loader.y - column.spacing / 2) // (between two lines)
            const live = loader.item as LiveChartLine
            if (live !== null) {
                const target = live.dropTargetAt(px, py - loader.y)
                target.y += loader.y
                return target
            }
            // A title or a note: the row just under it (at its end), or a new one there.
            const next = i + 1 < count ? lineRepeater.itemAt(i + 1) as Loader : null
            const nextShown = chart.lines[i + 1]
            const nextLive = next !== null ? next.item as LiveChartLine : null
            if (nextLive !== null && nextShown !== undefined && (nextShown.row === true || nextShown.kind === "blank")) {
                const end = nextLive.dropTargetAt(chart.width, 0)
                end.y += next.y
                return end
            }
            return chart.newRowAt(shown.line + 1, loader.y + loader.height)
        }
        const last = count > 0 ? lineRepeater.itemAt(count - 1) : null
        const lastShown = count > 0 ? chart.lines[count - 1] : undefined
        return chart.newRowAt(lastShown !== undefined ? lastShown.line + 1 : 0, last !== null ? last.y + last.height + 4 : 4)
    }
    function drop(source, target) {
        const moving = source.fromLine >= 0
        if (target.kind === "word") {
            if (moving) chart.doc.moveChordTo(source.fromLine, source.chordIndex, target.line, target.at)
            else chart.doc.placeChordAt(target.line, target.at, source.chordName)
        } else if (target.kind === "row") {
            if (moving) chart.doc.moveChordInRow(source.fromLine, source.chordIndex, target.line, target.index)
            else chart.doc.placeChordInRow(target.line, target.index, source.chordName)
        } else {
            if (moving) chart.doc.moveChordToNewRow(source.fromLine, source.chordIndex, target.before)
            else chart.doc.addChordRow(target.before, source.chordName)
        }
    }
    property var dropTarget: null

    DropArea {
        id: dropLayer
        objectName: "chartDropLayer"
        anchors.fill: parent
        enabled: chart.liveEdit && chart.doc !== null
        keys: ["chord"]
        onPositionChanged: (drag) => {
            chart.dropTarget = chart.dropTargetAt(drag.x, drag.y)
            chart.dragMoved(drag.y)
        }
        onEntered: (drag) => chart.dropTarget = chart.dropTargetAt(drag.x, drag.y)
        onExited: {
            chart.dropTarget = null
            chart.dragEnded()
        }
        onDropped: (event) => {
            const source = event.source as ChordDragSource
            const target = chart.dropTargetAt(event.x, event.y)
            chart.dropTarget = null
            chart.dragEnded()
            if (source === null) return
            const before = chart.doc.currentChart
            chart.drop(source, target)
            // Taken only when the chart changed: dropped back where it was (or
            // refused, said in the banner), the chord goes back to its place.
            if (chart.doc.currentChart !== before) event.accept()
        }
    }

    Column {
    id: column
    width: chart.width
    spacing: 2 * chart.size

    Repeater {
        id: lineRepeater
        objectName: "chartLines"
        model: chart.lines
        delegate: Loader {
            id: lineLoader
            required property var modelData
            required property int index
            readonly property int sectionIndex: modelData.sectionIndex !== undefined ? modelData.sectionIndex : -1
            readonly property bool typable: modelData.kind === "lyrics" || modelData.kind === "blank"
            // The lines next to it that can be typed in (for Up, Down and Backspace).
            function neighbour(step) {
                const next = chart.lines[lineLoader.index + step]
                return next !== undefined && (next.kind === "lyrics" || next.kind === "blank") ? next : null
            }
            width: chart.width
            z: (lineLoader.item as LiveChartLine) !== null && (lineLoader.item as LiveChartLine).dragging ? 5 : 0
            sourceComponent: chart.liveEdit && lineLoader.typable ? liveLine
                           : lineLoader.sectionIndex >= 0 ? sectionHeader
                           : modelData.kind === "lyrics" ? lyricLine
                           : modelData.kind === "section" ? sectionLine
                           : modelData.kind === "comment" ? commentLine
                           : blankLine
            Component {
                id: liveLine
                LiveChartLine {
                    width: lineLoader.width
                    line: lineLoader.modelData
                    doc: chart.doc
                    size: chart.size
                    currentStep: chart.currentStep
                    started: chart.followStarted
                    lineAbove: lineLoader.neighbour(-1) !== null ? lineLoader.neighbour(-1).line : -1
                    lineBelow: lineLoader.neighbour(1) !== null ? lineLoader.neighbour(1).line : -1
                    lengthAbove: lineLoader.neighbour(-1) !== null && lineLoader.neighbour(-1).lyrics !== undefined
                                 ? lineLoader.neighbour(-1).lyrics.length : 0
                    open: chart.openLine === lineLoader.modelData.line
                    onOpenRequested: (line) => chart.openLine = line
                    focusRequest: chart.focusRequest
                    onWantFocus: (line, cursor) => chart.focusRequest = { line: line, cursor: cursor }
                    onWantCell: (line, cell, what) => chart.focusRequest = { line: line, cell: cell, what: what }
                    onChordDiagramRequested: (name) => chart.chordClicked(name)
                    onFocusTaken: Qt.callLater(() => chart.focusRequest = null) // (not while lines are being built)
                }
            }
            Component {
                id: sectionHeader
                SectionHeader {
                    width: lineLoader.width
                    label: lineLoader.modelData.label
                    chartLine: lineLoader.modelData.line
                    sectionIndex: lineLoader.sectionIndex
                    section: lineLoader.sectionIndex < chart.sections.length ? chart.sections[lineLoader.sectionIndex] : undefined
                    doc: chart.doc
                    size: chart.size
                    editable: chart.editable
                    current: lineLoader.sectionIndex === chart.currentSection
                    playing: chart.playing
                    bar: chart.bar
                }
            }
            Component {
                id: lyricLine
                // The line centred as a whole: each segment its chord above
                // its words. A line wider than the chart wraps, centred too.
                Item {
                    objectName: "chartLyricLine"
                    width: lineLoader.width
                    height: flow.height
                    // The line's own width, laid out in one row (not shown).
                    Row {
                        id: natural
                        visible: false
                        Repeater {
                            model: lineLoader.modelData.segments
                            delegate: ChartSegment {
                                size: chart.size
                                currentStep: chart.currentStep
                                started: chart.followStarted
                                measuring: true
                            }
                        }
                    }
                    Flow {
                        id: flow
                        objectName: "chartLyricFlow"
                        width: Math.min(parent.width, natural.implicitWidth)
                        x: (parent.width - width) / 2
                        Repeater {
                            model: lineLoader.modelData.segments
                            delegate: ChartSegment {
                                size: chart.size
                                currentStep: chart.currentStep
                                started: chart.followStarted
                                onChordClicked: (name) => chart.chordClicked(name)
                            }
                        }
                    }
                }
            }
            Component {
                id: sectionLine
                Text {
                    width: lineLoader.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    topPadding: 10 * chart.size
                    text: lineLoader.modelData.label
                    color: Theme.accentBlue
                    font.pixelSize: (Theme.fontSize + 3) * chart.size
                    font.bold: true
                }
            }
            Component {
                id: commentLine
                // A note to the player ("Capo 2", "play softly"), centred.
                // Editing, right-click: make it a section, or take it out.
                Text {
                    objectName: "chartNote"
                    width: lineLoader.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    topPadding: 8 * chart.size
                    text: lineLoader.modelData.label
                    color: Theme.textDim
                    font.pixelSize: (Theme.fontSize + 3) * chart.size
                    font.italic: true
                    TapHandler {
                        enabled: chart.editable && chart.doc !== null
                        acceptedButtons: Qt.RightButton
                        onTapped: noteMenu.popup()
                    }
                    // ✕ at its right end while the mouse is over it, as every line has.
                    HoverHandler { id: noteHover; enabled: chart.liveEdit && chart.doc !== null }
                    Rectangle {
                        objectName: "deleteNoteButton"
                        visible: noteHover.hovered
                        anchors.right: parent.right
                        anchors.rightMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: parent.topPadding / 2
                        width: 22 * Math.max(chart.size, 1)
                        height: width
                        radius: width / 2
                        color: noteDeleteHover.hovered ? Theme.danger : "#22ffffff"
                        border.color: noteDeleteHover.hovered ? Theme.danger : "#44ffffff"
                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            color: noteDeleteHover.hovered ? "white" : Theme.textDim
                            font.pixelSize: Theme.smallFontSize * Math.max(chart.size, 1)
                            font.bold: true
                            font.italic: false
                        }
                        HoverHandler { id: noteDeleteHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: chart.doc.removeChartSection(lineLoader.modelData.line) }
                        ToolTip.visible: noteDeleteHover.hovered
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Delete this note (Ctrl+Z brings it back)")
                    }
                    StageMenu {
                        id: noteMenu
                        StageMenuItem {
                            objectName: "chartNoteToSection"
                            text: qsTr("Make it a section")
                            onTriggered: chart.doc.renameChartSection(lineLoader.modelData.line, lineLoader.modelData.label)
                        }
                        StageMenuItem {
                            objectName: "chartNoteRemove"
                            text: qsTr("Remove this note")
                            onTriggered: chart.doc.removeChartSection(lineLoader.modelData.line)
                        }
                    }
                }
            }
            Component {
                id: blankLine
                Item { height: 14 * chart.size }
            }
        }
    }
    }

    // Where the dragged chord will land: a bar between chords, a box round
    // the word, or a line where a new row goes ("+ chords").
    Rectangle {
        objectName: "chartDropMark"
        visible: chart.dropTarget !== null
        z: 50
        x: chart.dropTarget !== null ? chart.dropTarget.x : 0
        y: chart.dropTarget !== null ? chart.dropTarget.y : 0
        width: chart.dropTarget !== null ? chart.dropTarget.w : 0
        height: chart.dropTarget !== null ? chart.dropTarget.h : 0
        radius: Theme.radiusSmall
        color: chart.dropTarget !== null && chart.dropTarget.kind !== "word" ? Theme.accent : "transparent"
        border.color: Theme.accent
        border.width: 2
        Text {
            visible: chart.dropTarget !== null && chart.dropTarget.kind === "newRow"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: 2
            text: qsTr("+ a new row of chords")
            color: Theme.accent
            font.pixelSize: Theme.smallFontSize
            font.bold: true
        }
    }
}
