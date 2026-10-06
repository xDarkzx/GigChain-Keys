pragma ComponentBehavior: Bound
import QtQuick

// A song's chart as the musician reads it: each chord above the syllable it
// changes on, section headings, notes. `lines` comes from
// DocumentController.chartLines(). `size` scales everything (1 in the
// editor, bigger on stage).
//
// With `doc` set, each section title is a SectionHeader: centred and large,
// with the instruments the section plays; `currentSection`, `playing` and
// `bar` light the one in force (from EngineStatus).
Column {
    id: chart

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

    spacing: 2 * size

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
                Text {
                    width: lineLoader.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    topPadding: 8 * chart.size
                    text: lineLoader.modelData.label
                    color: Theme.textDim
                    font.pixelSize: (Theme.fontSize + 3) * chart.size
                    font.italic: true
                }
            }
            Component {
                id: blankLine
                Item { height: 14 * chart.size }
            }
        }
    }
}
