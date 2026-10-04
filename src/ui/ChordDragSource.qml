import QtQuick

// A chord that can be dragged onto the words of a chart: a chip of the
// chart itself (fromLine, chordIndex: where it is) or of the chord palette
// (fromLine -1: a new chord, by name). What a line's DropArea reads.
Rectangle {
    property int fromLine: -1
    property int chordIndex: -1
    property string chordName: ""
}
