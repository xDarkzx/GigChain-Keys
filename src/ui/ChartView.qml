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
