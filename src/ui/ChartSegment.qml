import QtQuick

// One piece of a chart line: its chord above the words it changes on.
// `modelData` is a segment from DocumentController.chartLines().
Column {
    id: segment

    required property var modelData
    property real size: 1.0

    Text {
        text: segment.modelData.chord !== "" ? segment.modelData.chord : " "
        color: Theme.chord
        font.pixelSize: (Theme.fontSize + 5) * segment.size
        font.bold: true
        // Chords with no words under them (an intro, a turnaround) keep a
        // gap between them.
        rightPadding: segment.modelData.text.trim() === "" ? 18 * segment.size : 0
    }
    Text {
        text: segment.modelData.text !== "" ? segment.modelData.text : " "
        color: Theme.text
        font.pixelSize: (Theme.fontSize + 7) * segment.size
    }
}
