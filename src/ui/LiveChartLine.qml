pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// One line of a chart edited in place (the Chart tab): its words typed
// straight in, and its chords above them as chips: click the space above a
// word to type a chord there, drag a chip along the words (or to another
// line) and it lands on the word it is dropped on, double-click a chip to
// rename it, right-click to remove it. Chords from the palette (ChartPanel)
// are dropped the same way. Every change goes to the DocumentController
// (an undo step each; typing a line is one).
//
// An edit rebuilds the chart's lines, this one too: before each, the line
// asks for the caret back (`wantFocus`) and the new line takes it.
Item {
    id: live

    required property var line        // a chartLines() entry: kind "lyrics" or "blank"
    required property DocumentController doc
    property real size: 1.0
    property int currentStep: -1
    property bool started: false
    property int lineAbove: -1        // the chart lines above / below that can be typed in (-1: none)
    property int lineBelow: -1
    property int lengthAbove: 0       // the words' length of the line above (Backspace joins onto it)
    property var focusRequest: null   // {line, cursor} from the chart
    signal wantFocus(int line, int cursor)
    signal focusTaken()

    readonly property int chartLine: live.line.line
    readonly property var chords: {
        const list = []
        const segments = live.line.segments !== undefined ? live.line.segments : []
        for (let i = 0; i < segments.length; ++i) {
            const s = segments[i]
            if (s.chord !== "") list.push({ name: s.chord, at: s.at, index: s.chordIndex, steps: s.steps, understood: s.understood })
        }
        return list
    }
    readonly property real chordHeight: (Theme.fontSize + 5) * live.size * 1.4
    readonly property real chordFont: (Theme.fontSize + 5) * live.size
    readonly property real chipPadding: 4 * live.size
    // Where a dragged chord would land (a character of the words), -1: nowhere.
    property int dropAt: -1
    // One of its chords is being dragged (the line is drawn over the others).
    property bool dragging: false

    // Where each chip sits, from the words' start: over its word; two at one
    // place side by side.
    readonly property var chordXs: {
        const xs = []
        let right = -1e9
        for (let i = 0; i < live.chords.length; ++i) {
            const x = Math.max(live.xOf(live.chords[i].at, words.text, 0), right + 3)
            xs.push(x)
            right = x + chordMetrics.advanceWidth(live.chords[i].name) + 2 * live.chipPadding
        }
        return xs
    }
    readonly property real chordsRight: live.chords.length > 0
                                        ? live.chordXs[live.chordXs.length - 1]
                                          + chordMetrics.advanceWidth(live.chords[live.chords.length - 1].name) + 2 * live.chipPadding
                                        : 0

    width: parent ? parent.width : 0
    height: chordHeight + words.height

    FontMetrics {
        id: chordMetrics
        font.pixelSize: live.chordFont
        font.bold: true
    }

    // A word's first letter (as the document snaps a dropped chord).
    function wordStart(text, at) {
        let place = Math.max(0, Math.min(at, text.length))
        if (place === text.length) return place
        if (/\s/.test(text[place])) {
            while (place < text.length && /\s/.test(text[place])) ++place
            return place
        }
        while (place > 0 && !/\s/.test(text[place - 1])) --place
        return place
    }
    // (text and left are passed in so the bindings follow the typing.)
    function xOf(at, text, left) { return left + words.positionToRectangle(Math.min(at, text.length)).x }
    function atOf(xInLine) { return words.positionAt(xInLine - words.x, words.height / 2) }

    // The line centred, as it is read on stage.
    readonly property real lineWidth: Math.max(words.contentWidth + 4, live.chordsRight)
    readonly property real lineLeft: Math.max(0, (live.width - live.lineWidth) / 2)

    function startNewChord(x) {
        newChord.at = live.wordStart(words.text, live.atOf(x))
        newChord.text = ""
        newChord.visible = true
        newChord.forceActiveFocus()
    }
    function startRename(index, x, name) {
        rename.chordIndex = index
        rename.x = x
        rename.text = name
        rename.visible = true
        rename.forceActiveFocus()
        rename.selectAll()
    }

    // Click above a word: type a chord there.
    MouseArea {
        objectName: "chordSpace"
        width: live.width
        height: live.chordHeight
        cursorShape: Qt.IBeamCursor
        hoverEnabled: true
        onClicked: (mouse) => live.startNewChord(mouse.x)
        ToolTip.visible: containsMouse && live.chords.length === 0 && words.text !== ""
        ToolTip.delay: 800
        ToolTip.text: qsTr("Click above a word to put a chord on it")
    }

    // The chords: chips over the words they change on.
    Repeater {
        model: live.chords
        delegate: ChordDragSource {
            id: chip
            required property var modelData
            required property int index
            objectName: "chartChordChip"
            // (Named for what chord follow makes it, as on stage.)
            Item { objectName: chip.current ? "chartChordCurrent" : chip.next ? "chartChordNext" : "" }
            // Chord follow: the chord being played, lit; the next one, outlined.
            readonly property bool current: live.started && chip.modelData.steps.indexOf(live.currentStep) >= 0
            readonly property bool next: !chip.current
                                         && chip.modelData.steps.indexOf(live.started ? live.currentStep + 1 : 0) >= 0
            // What a drop needs to know.
            fromLine: live.chartLine
            chordIndex: chip.modelData.index
            chordName: chip.modelData.name
            function home() {
                chip.x = Qt.binding(() => words.x + (live.chordXs[chip.index] !== undefined ? live.chordXs[chip.index] : 0))
                chip.y = 0
            }
            Component.onCompleted: home()
            width: label.implicitWidth + 2 * live.chipPadding
            height: live.chordHeight - 2
            radius: Theme.radiusSmall
            color: dragger.active ? Theme.accent : hover.hovered ? "#33f0bf45" : "transparent"
            border.color: dragger.active || hover.hovered ? Theme.chord : chip.next ? Theme.accent : "transparent"
            z: dragger.active ? 10 : 1
            Drag.active: dragger.active
            Drag.keys: ["chord"]
            Drag.hotSpot.x: live.chipPadding
            Drag.hotSpot.y: height / 2
            Text {
                id: label
                anchors.left: parent.left
                anchors.leftMargin: live.chipPadding
                anchors.verticalCenter: parent.verticalCenter
                text: chip.modelData.name
                color: dragger.active ? "white" : !chip.modelData.understood ? Theme.textDim
                     : chip.current ? Theme.accent : Theme.chord
                font.pixelSize: live.chordFont
                font.bold: true
                font.strikeout: !chip.modelData.understood
            }
            HoverHandler { id: hover; cursorShape: Qt.OpenHandCursor }
            DragHandler {
                id: dragger
                objectName: "chordDragger"
                cursorShape: Qt.ClosedHandCursor
                onActiveChanged: {
                    live.dragging = active
                    if (active) return
                    // Dropped where nothing takes it: back where it was.
                    if (chip.Drag.drop() === Qt.IgnoreAction) chip.home()
                }
            }
            TapHandler { onDoubleTapped: live.startRename(chip.modelData.index, chip.x, chip.modelData.name) }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: chipMenu.popup()
            }
            StageMenu {
                id: chipMenu
                StageMenuItem {
                    text: qsTr("Rename %1…").arg(chip.modelData.name)
                    onTriggered: live.startRename(chip.modelData.index, chip.x, chip.modelData.name)
                }
                StageMenuItem {
                    text: qsTr("Remove %1").arg(chip.modelData.name)
                    onTriggered: live.doc.renameChordAt(live.chartLine, chip.modelData.index, "")
                }
            }
            ToolTip.visible: hover.hovered && !dragger.active
            ToolTip.delay: 900
            ToolTip.text: qsTr("Drag onto a word · double-click to rename · right-click to remove")
        }
    }

    // Where a dragged chord will land: a bar before that word.
    Rectangle {
        objectName: "chordDropMark"
        visible: live.dropAt >= 0
        x: live.xOf(live.dropAt, words.text, words.x) - 1
        y: live.chordHeight * 0.2
        width: 2
        height: live.height - live.chordHeight * 0.2
        color: Theme.accent
    }

    // A new chord, typed where the click was.
    TextInput {
        id: newChord
        objectName: "newChordInput"
        property int at: 0
        visible: false
        x: live.xOf(newChord.at, words.text, words.x)
        width: Math.max(contentWidth + 8, 44 * live.size)
        height: live.chordHeight
        z: 20
        leftPadding: 3
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.chord
        font.pixelSize: live.chordFont
        font.bold: true
        selectByMouse: true
        Rectangle { anchors.fill: parent; z: -1; color: Theme.readoutBackground; border.color: Theme.accent; radius: Theme.radiusSmall }
        onAccepted: {
            visible = false
            if (text.trim() !== "") live.doc.placeChordAt(live.chartLine, newChord.at, text.trim())
        }
        Keys.onEscapePressed: visible = false
        onActiveFocusChanged: if (!activeFocus) visible = false
    }

    // A chord renamed in place (empty: removed).
    TextInput {
        id: rename
        objectName: "renameChordInput"
        property int chordIndex: -1
        visible: false
        width: Math.max(contentWidth + 8, 44 * live.size)
        height: live.chordHeight
        z: 20
        leftPadding: 3
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.chord
        font.pixelSize: live.chordFont
        font.bold: true
        selectByMouse: true
        Rectangle { anchors.fill: parent; z: -1; color: Theme.readoutBackground; border.color: Theme.accent; radius: Theme.radiusSmall }
        onAccepted: {
            visible = false
            live.doc.renameChordAt(live.chartLine, rename.chordIndex, text.trim())
        }
        Keys.onEscapePressed: visible = false
        onActiveFocusChanged: if (!activeFocus) visible = false
    }

    // The words, typed straight in.
    TextInput {
        id: words
        objectName: "chartWords"
        x: live.lineLeft
        y: live.chordHeight
        width: Math.max(contentWidth + 2, 24)
        text: live.line.lyrics !== undefined ? live.line.lyrics : ""
        color: Theme.text
        font.pixelSize: (Theme.fontSize + 7) * live.size
        selectByMouse: true
        selectionColor: Theme.accent
        // A blank line: a hint where words can go, while the caret is in it.
        Text {
            visible: words.text === "" && words.activeFocus
            text: qsTr("Type the words…")
            color: Theme.textDim
            font: words.font
        }
        onTextEdited: {
            live.wantFocus(live.chartLine, words.cursorPosition)
            live.doc.setLineLyrics(live.chartLine, words.text)
        }
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                event.accepted = true
                live.wantFocus(live.chartLine + 1, 0)
                live.doc.splitChartLine(live.chartLine, words.cursorPosition)
            } else if (event.key === Qt.Key_Backspace && words.cursorPosition === 0 && words.selectedText === ""
                       && live.lineAbove === live.chartLine - 1) {
                event.accepted = true
                live.wantFocus(live.chartLine - 1, live.lengthAbove)
                live.doc.joinChartLine(live.chartLine)
            } else if (event.key === Qt.Key_Up && live.lineAbove >= 0) {
                event.accepted = true
                live.wantFocus(live.lineAbove, words.cursorPosition)
            } else if (event.key === Qt.Key_Down && live.lineBelow >= 0) {
                event.accepted = true
                live.wantFocus(live.lineBelow, words.cursorPosition)
            }
        }
    }

    // The caret back in this line when the chart asks.
    function takeFocusIfAsked() {
        if (live.focusRequest === null || live.focusRequest === undefined || live.focusRequest.line !== live.chartLine) return
        words.forceActiveFocus()
        const cursor = live.focusRequest.cursor
        words.cursorPosition = Math.max(0, Math.min(cursor, words.text.length))
        live.focusTaken()
    }
    onFocusRequestChanged: takeFocusIfAsked()
    // (Once it is in the scene: a line built by an edit is not yet when completed.)
    Component.onCompleted: Qt.callLater(takeFocusIfAsked)

    // Chords dropped here: one from another place, or from the palette.
    DropArea {
        anchors.fill: parent
        keys: ["chord"]
        onPositionChanged: (drag) => live.dropAt = live.wordStart(words.text, live.atOf(drag.x))
        onExited: live.dropAt = -1
        onDropped: (drop) => {
            const at = live.wordStart(words.text, live.atOf(drop.x))
            live.dropAt = -1
            const source = drop.source as ChordDragSource
            if (source === null) return
            drop.accept()
            if (source.fromLine >= 0)
                live.doc.moveChordTo(source.fromLine, source.chordIndex, live.chartLine, at)
            else
                live.doc.placeChordAt(live.chartLine, at, source.chordName)
        }
    }
}
