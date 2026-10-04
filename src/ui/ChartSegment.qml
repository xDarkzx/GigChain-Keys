import QtQuick

// One piece of a chart line: its chord above the words it changes on.
// `modelData` is a segment from DocumentController.chartLines(). With chord
// follow, the chord being played is lit and the next one outlined; a chord
// the app cannot read is dimmed and struck through (fix it in the editor).
Column {
    id: segment

    required property var modelData
    property real size: 1.0
    property int currentStep: -1
    property bool started: false
    property bool measuring: false // the hidden copy that only measures a line
    // Its chord tapped: how to play it (the chord diagram).
    signal chordClicked(string name)
    readonly property var steps: segment.modelData.steps !== undefined ? segment.modelData.steps : []
    readonly property bool current: segment.started && segment.steps.indexOf(segment.currentStep) >= 0
    readonly property bool next: !segment.current && segment.steps.indexOf(segment.started ? segment.currentStep + 1 : 0) >= 0
    readonly property bool understood: segment.modelData.understood !== false

    objectName: segment.measuring ? "" : segment.current ? "chartChordCurrent" : segment.next ? "chartChordNext" : ""

    Text {
        id: chordText
        text: segment.modelData.chord !== "" ? segment.modelData.chord : " "
        color: !segment.understood ? Theme.textDim : segment.current ? Theme.accent : Theme.chord
        font.pixelSize: (Theme.fontSize + 5) * segment.size
        font.bold: true
        font.strikeout: !segment.understood
        // Chords with no words under them (an intro, a turnaround) keep a
        // gap between them.
        rightPadding: segment.modelData.text.trim() === "" ? 18 * segment.size : 0
        HoverHandler {
            enabled: !segment.measuring && segment.modelData.chord !== ""
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            objectName: "chordTap"
            enabled: !segment.measuring && segment.modelData.chord !== ""
            onTapped: segment.chordClicked(segment.modelData.chord)
        }
        Rectangle {
            // The next chord: outlined, so the eye finds it.
            visible: segment.next && segment.modelData.chord !== ""
            x: -3 * segment.size
            y: -1 * segment.size
            width: chordText.contentWidth + 6 * segment.size
            height: chordText.contentHeight + 2 * segment.size
            color: "transparent"
            border.color: Theme.accent
            border.width: 1
            radius: Theme.radiusSmall
        }
    }
    Text {
        text: segment.modelData.text !== "" ? segment.modelData.text : " "
        color: Theme.text
        font.pixelSize: (Theme.fontSize + 7) * segment.size
    }
}
