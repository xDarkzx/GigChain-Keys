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
    property DocumentController doc: null
    readonly property var sections: doc !== null ? doc.currentSections : []
    property int currentSection: -1
    property bool playing: false
    property int bar: 0

    spacing: 2 * size

    Repeater {
        objectName: "chartLines"
        model: chart.lines
        delegate: Loader {
            id: lineLoader
            required property var modelData
            readonly property int sectionIndex: modelData.sectionIndex !== undefined ? modelData.sectionIndex : -1
            width: chart.width
            sourceComponent: lineLoader.sectionIndex >= 0 ? sectionHeader
                           : modelData.kind === "lyrics" ? lyricLine
                           : modelData.kind === "section" ? sectionLine
                           : modelData.kind === "comment" ? commentLine
                           : blankLine
            Component {
                id: sectionHeader
                SectionHeader {
                    width: lineLoader.width
                    label: lineLoader.modelData.label
                    sectionIndex: lineLoader.sectionIndex
                    section: lineLoader.sectionIndex < chart.sections.length ? chart.sections[lineLoader.sectionIndex] : undefined
                    doc: chart.doc
                    size: chart.size
                    current: lineLoader.sectionIndex === chart.currentSection
                    playing: chart.playing
                    bar: chart.bar
                }
            }
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
                                color: Theme.chord
                                font.pixelSize: (Theme.fontSize + 5) * chart.size
                                font.bold: true
                                // Chords with no words under them (an intro, a
                                // turnaround) keep a gap between them.
                                rightPadding: modelData.text.trim() === "" ? 18 * chart.size : 0
                            }
                            Text {
                                text: modelData.text !== "" ? modelData.text : " "
                                color: Theme.text
                                font.pixelSize: (Theme.fontSize + 7) * chart.size
                            }
                        }
                    }
                }
            }
            Component {
                id: sectionLine
                Text {
                    topPadding: 10 * chart.size
                    text: lineLoader.modelData.label
                    color: Theme.accentBlue
                    font.pixelSize: (Theme.fontSize + 3) * chart.size
                    font.bold: true
                }
            }
            Component {
                id: commentLine
                Text {
                    topPadding: 8 * chart.size
                    text: lineLoader.modelData.label
                    color: Theme.accentBlue
                    font.pixelSize: (Theme.fontSize + 3) * chart.size
                    font.bold: true
                }
            }
            Component {
                id: blankLine
                Item { height: 14 * chart.size }
            }
        }
    }
}
