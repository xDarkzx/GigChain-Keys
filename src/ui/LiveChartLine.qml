pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// One line of a chart edited in place (the Chart tab), as cells: each word
// with a chord box above it, laid out side by side and centred as the line
// is read on stage. A chord is drawn in its word's box by layout (never
// placed by measuring), so it always sits on its word.
//
// Click a line: it opens, an empty box shows as a bar over each word. Click
// a box to type a chord (Tab: the next box), click a chord to change it
// (empty: removed), drag it to another box or line, right-click it for more.
// Double-click a word to change it where it is (Tab: the next word). The
// line's own menu (right-click) edits the whole line as text, or adds a line
// below. Each change is one undo step (DocumentController).
//
// An edit rebuilds the chart's lines, this one too: the line typed in asks
// for the caret back (`wantFocus`) and the new line takes it.
Item {
    id: live

    required property var line        // a chartLines() entry: kind "lyrics" or "blank"
    required property DocumentController doc
    property real size: 1.0
    property int currentStep: -1
    property bool started: false
    property bool open: false         // the line being edited (its empty boxes shown)
    property int lineAbove: -1        // the chart lines above / below that can be typed in (-1: none)
    property int lineBelow: -1
    property int lengthAbove: 0       // the words' length of the line above (Backspace joins onto it)
    // From the chart, after an edit rebuilt the lines: {line, cursor} types
    // the line as text; {line, cell, what} types in a cell's box or word.
    property var focusRequest: null
    signal wantFocus(int line, int cursor)
    signal wantCell(int line, int cell, string what)
    signal focusTaken()
    signal openRequested(int line)
    signal chordDiagramRequested(string name) // a chord's menu: how to play it

    readonly property int chartLine: live.line.line
    readonly property string lyrics: live.line.lyrics !== undefined ? live.line.lyrics : ""
    readonly property var cells: live.line.cells !== undefined ? live.line.cells : []
    // A row of chords (an intro, a solo: chords and no words), drawn as chips in order.
    readonly property bool isRow: live.line.row === true
    readonly property var rowChords: live.line.rowChords !== undefined ? live.line.rowChords : []
    readonly property real chordHeight: (Theme.fontSize + 5) * live.size * 1.4
    readonly property real chordFont: (Theme.fontSize + 5) * live.size
    readonly property real wordFont: (Theme.fontSize + 7) * live.size
    readonly property real chipPadding: 4 * live.size
    // One of its chords is being dragged (the line is drawn over the others).
    property bool dragging: false
    // The whole line typed as text (its menu, a blank line, Enter, Up/Down).
    property bool typingLine: false
    // A cell asked to be typed in: {cell, what: "chord" | "word"} (Tab).
    property var cellEditAsked: null

    width: parent ? parent.width : 0
    height: live.typingLine ? lineText.y + lineText.height
          : live.isRow ? rowView.height + 8 * live.size
          : Math.max(cellsFlow.height, live.chordHeight + wordMetrics.height)

    // Where a chord dragged to (px, py) of this line lands (the chart's drop
    // layer asks): between two chords of a row; at the start of an empty
    // line (it becomes a row); on the nearest word of a line of words. With
    // the mark to show there, in this line's coordinates.
    function dropTargetAt(px, py) {
        if (live.isRow) {
            const gap = rowView.spacing
            let index = 0
            let markX = rowView.x - gap / 2
            for (let i = 0; i < rowRepeater.count; ++i) {
                const chip = rowRepeater.itemAt(i)
                if (chip === null) continue
                if (px > rowView.x + chip.x + chip.width / 2) {
                    index = i + 1
                    markX = rowView.x + chip.x + chip.width + gap / 2
                }
            }
            return { kind: "row", line: live.chartLine, index: index, x: markX - 1.5, y: rowView.y, w: 3, h: rowView.height }
        }
        if (live.cells.length === 0)
            return { kind: "row", line: live.chartLine, index: 0, x: live.width / 2 - 1.5, y: 0, w: 3, h: live.height }
        let best = -1
        let distance = Infinity
        for (let c = 0; c < cellsRepeater.count; ++c) {
            const item = cellsRepeater.itemAt(c)
            if (item === null) continue
            const dx = Math.abs(cellsFlow.x + item.x + item.width / 2 - px)
            const dy = Math.abs(cellsFlow.y + item.y + item.height / 2 - py)
            if (dx + 3 * dy < distance) {
                distance = dx + 3 * dy
                best = c
            }
        }
        const cell = cellsRepeater.itemAt(best)
        return { kind: "word", line: live.chartLine, at: live.cells[best].at,
                 x: cellsFlow.x + cell.x, y: cellsFlow.y + cell.y, w: cell.width - (live.cells[best].space ? wordMetrics.advanceWidth(" ") : 0), h: live.chordHeight }
    }

    FontMetrics {
        id: wordMetrics
        font.pixelSize: live.wordFont
    }

    // The words as they will read after `word` (a cell) becomes `text`.
    function lyricsWith(cell, text) {
        const end = cell.at + live.lyrics.substring(cell.at).replace(/\s.*$/, "").length
        return live.lyrics.substring(0, cell.at) + text + live.lyrics.substring(end)
    }
    function typeLine(cursor) {
        live.openRequested(live.chartLine)
        lineText.text = live.lyrics
        live.typingLine = true
        lineText.forceActiveFocus()
        lineText.cursorPosition = Math.max(0, Math.min(cursor, lineText.text.length))
    }
    // Tab in a box or a word: saved, then the next (previous) cell's box or
    // word typed in. (After the save rebuilds the line: the cells are new.)
    function moveOn(from, step, what, save) {
        save()
        live.wantCell(live.chartLine, from + step, what)
    }

    // ✕ at the line's right end, while the mouse is over the line (or it is
    // open): one click deletes the whole line (Ctrl+Z brings it back).
    HoverHandler { id: lineHover }
    Rectangle {
        id: deleteLineButton
        objectName: "deleteLineButton"
        visible: (lineHover.hovered || live.open) && !live.typingLine
        z: 30
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        width: 22 * Math.max(live.size, 1)
        height: width
        radius: width / 2
        color: deleteHover.hovered ? Theme.danger : "#22ffffff"
        border.color: deleteHover.hovered ? Theme.danger : "#44ffffff"
        Text {
            anchors.centerIn: parent
            text: "✕"
            color: deleteHover.hovered ? "white" : Theme.textDim
            font.pixelSize: Theme.smallFontSize * Math.max(live.size, 1)
            font.bold: true
        }
        HoverHandler { id: deleteHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: live.doc.deleteChartLine(live.chartLine) }
        ToolTip.visible: deleteHover.hovered
        ToolTip.delay: 400
        ToolTip.text: live.isRow ? qsTr("Delete this row of chords (Ctrl+Z brings it back)")
                                 : qsTr("Delete this line (Ctrl+Z brings it back)")
    }

    // The line's ground: a click opens it; a right-click gives its menu.
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: -6
        anchors.rightMargin: -6
        radius: Theme.radiusSmall
        color: live.open ? "#0dffffff" : "transparent"
        border.color: live.open ? Theme.outline : "transparent"
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: {
            if (live.cells.length === 0) live.typeLine(0) // a blank line: type its words
            else live.openRequested(live.chartLine)
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: lineMenu.popup()
    }
    StageMenu {
        id: lineMenu
        StageMenuItem {
            objectName: "editLineAsText"
            text: qsTr("Edit the line as text")
            onTriggered: live.typeLine(live.lyrics.length)
        }
        StageMenuItem {
            objectName: "insertLineBelow"
            text: qsTr("Add a line below")
            onTriggered: {
                live.wantFocus(live.chartLine + 1, 0)
                live.doc.editLineAndSplit(live.chartLine, live.lyrics, live.lyrics.length)
            }
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
        StageMenuItem {
            objectName: "deleteLine"
            text: live.isRow ? qsTr("Delete this row of chords") : qsTr("Delete this line")
            onTriggered: live.doc.deleteChartLine(live.chartLine)
        }
    }

    // A row of chords: its chips in order with bar lines between them;
    // drag one along the row or anywhere in the chart, click it to change
    // it, right-click for more; the box at the end types a new one.
    Row {
        id: rowView
        objectName: "chartChordRow"
        visible: live.isRow && !live.typingLine
        anchors.horizontalCenter: parent.horizontalCenter
        y: 4 * live.size
        spacing: 10 * live.size
        Repeater {
            id: rowRepeater
            model: live.isRow ? live.rowChords : []
            delegate: Item {
                id: rowSlot
                required property var modelData
                required property int index
                width: rowChip.width
                height: live.chordHeight
                // The bar line before it.
                Rectangle {
                    visible: rowSlot.index > 0
                    x: -rowView.spacing / 2 - 1
                    y: 4 * live.size
                    width: 2
                    height: parent.height - 8 * live.size
                    color: "#44ffffff"
                }
                ChordDragSource {
                    id: rowChip
                    objectName: "chartRowChord"
                    fromLine: live.chartLine
                    chordIndex: rowSlot.modelData.index
                    chordName: rowSlot.modelData.name
                    readonly property bool current: live.started && rowSlot.modelData.steps.indexOf(live.currentStep) >= 0
                    // (Longer chords are wider: the row reads like bars.)
                    width: Math.max(rowLabel.implicitWidth + 4 * live.chipPadding, 44 * live.size)
                           * (rowSlot.modelData.bars > 1 ? Math.min(2, 1 + (rowSlot.modelData.bars - 1) * 0.25) : 1)
                    height: live.chordHeight
                    radius: Theme.radiusSmall
                    color: rowDragger.active ? Theme.accent : rowHover.hovered ? "#33f0bf45" : "#14ffffff"
                    border.color: rowDragger.active || rowHover.hovered ? Theme.chord : rowChip.current ? Theme.accent : "#33ffffff"
                    z: rowDragger.active ? 10 : 1
                    Drag.active: rowDragger.active
                    Drag.keys: ["chord"]
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: height / 2
                    Text {
                        id: rowLabel
                        anchors.centerIn: parent
                        visible: !rowInput.visible
                        text: rowSlot.modelData.name
                        color: rowDragger.active ? "white" : !rowSlot.modelData.understood ? Theme.textDim
                             : rowChip.current ? Theme.accent : Theme.chord
                        font.pixelSize: live.chordFont
                        font.bold: true
                        font.strikeout: !rowSlot.modelData.understood
                    }
                    // Its length, when set to other than a bar ("2 bars", "½").
                    Text {
                        objectName: "rowChordBars"
                        readonly property real bars: rowSlot.modelData.bars !== undefined ? rowSlot.modelData.bars : 0
                        visible: bars > 0 && Math.abs(bars - 1) > 1e-6 && !rowInput.visible
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 3
                        anchors.bottomMargin: 1
                        text: Math.abs(bars - 0.5) < 1e-6 ? "½" : (Number.isInteger(bars) ? bars : bars.toFixed(1)) + qsTr(" bars")
                        color: Theme.textDim
                        font.pixelSize: Math.max(9, live.chordFont * 0.45)
                    }
                    TextInput {
                        id: rowInput
                        visible: false
                        anchors.centerIn: parent
                        width: Math.max(contentWidth + 10, 44 * live.size)
                        horizontalAlignment: TextInput.AlignHCenter
                        color: Theme.chord
                        font.pixelSize: live.chordFont
                        font.bold: true
                        selectByMouse: true
                        Rectangle { anchors.fill: parent; anchors.margins: -2; z: -1; color: Theme.readoutBackground; border.color: Theme.accent; radius: Theme.radiusSmall }
                        function commit() {
                            if (!rowInput.visible) return // (saved once)
                            rowInput.visible = false
                            const name = rowInput.text.trim()
                            if (name !== rowSlot.modelData.name) live.doc.renameChordAt(live.chartLine, rowSlot.modelData.index, name)
                        }
                        onAccepted: rowInput.commit()
                        Keys.onEscapePressed: rowInput.visible = false
                        onActiveFocusChanged: if (!activeFocus && visible) rowInput.commit()
                    }
                    function rename() {
                        rowInput.text = rowSlot.modelData.name
                        rowInput.visible = true
                        rowInput.forceActiveFocus()
                        rowInput.selectAll()
                    }
                    HoverHandler { id: rowHover; cursorShape: Qt.OpenHandCursor }
                    DragHandler {
                        id: rowDragger
                        objectName: "rowChordDragger"
                        cursorShape: Qt.ClosedHandCursor
                        onActiveChanged: {
                            live.dragging = active
                            if (active) return
                            // Dropped where nothing takes it: back in its place.
                            if (rowChip.Drag.drop() === Qt.IgnoreAction) {
                                rowChip.x = 0
                                rowChip.y = 0
                            }
                        }
                    }
                    TapHandler {
                        onTapped: {
                            live.openRequested(live.chartLine)
                            rowChip.rename()
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: rowChipMenu.popup()
                    }
                    StageMenu {
                        id: rowChipMenu
                        StageMenuItem {
                            text: qsTr("How to play %1…").arg(rowSlot.modelData.name)
                            onTriggered: live.chordDiagramRequested(rowSlot.modelData.name)
                        }
                        StageMenuItem {
                            text: qsTr("Change %1…").arg(rowSlot.modelData.name)
                            onTriggered: rowChip.rename()
                        }
                        StageMenuItem {
                            text: qsTr("Remove %1").arg(rowSlot.modelData.name)
                            onTriggered: live.doc.renameChordAt(live.chartLine, rowSlot.modelData.index, "")
                        }
                        // How long it lasts (Practice and the timeline play it so): written as bar lines.
                        StageMenu {
                            id: lengthMenu
                            objectName: "rowChordLength"
                            title: qsTr("Length")
                            Instantiator {
                                model: [1, 2, 4, 6, 8]
                                delegate: StageMenuItem {
                                    required property int modelData
                                    text: modelData === 1 ? qsTr("Half a bar") : modelData === 2 ? qsTr("1 bar") : qsTr("%1 bars").arg(modelData / 2)
                                    checkable: true
                                    checked: Math.abs(rowSlot.modelData.bars * 2 - modelData) < 1e-6
                                    onTriggered: live.doc.setRowChordLength(live.chartLine, rowSlot.modelData.index, modelData)
                                }
                                onObjectAdded: (index, object) => lengthMenu.insertItem(index, object)
                                onObjectRemoved: (index, object) => lengthMenu.removeItem(object)
                            }
                        }
                        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
                        StageMenuItem {
                            text: qsTr("Delete this row of chords")
                            onTriggered: live.doc.deleteChartLine(live.chartLine)
                        }
                    }
                    ToolTip.visible: rowHover.hovered && !rowDragger.active
                    ToolTip.delay: 900
                    ToolTip.text: qsTr("Drag along the row or anywhere in the chart · click to change · right-click for more")
                }
            }
        }
        // A new chord typed at the end of the row.
        Rectangle {
            id: rowAdd
            objectName: "chartRowAdd"
            width: rowAddInput.visible ? rowAddInput.width + 8 : 34 * live.size
            height: live.chordHeight
            radius: Theme.radiusSmall
            color: "transparent"
            border.color: rowAddHover.hovered || rowAddInput.visible ? Theme.chord : "#33ffffff"
            Text {
                anchors.centerIn: parent
                visible: !rowAddInput.visible
                text: "+"
                color: rowAddHover.hovered ? Theme.chord : Theme.textDim
                font.pixelSize: live.chordFont
            }
            TextInput {
                id: rowAddInput
                visible: false
                anchors.centerIn: parent
                width: Math.max(contentWidth + 10, 44 * live.size)
                horizontalAlignment: TextInput.AlignHCenter
                color: Theme.chord
                font.pixelSize: live.chordFont
                font.bold: true
                function commit() {
                    if (!rowAddInput.visible) return
                    rowAddInput.visible = false
                    const name = rowAddInput.text.trim()
                    if (name !== "") live.doc.placeChordInRow(live.chartLine, live.rowChords.length, name)
                }
                onAccepted: rowAddInput.commit()
                Keys.onEscapePressed: rowAddInput.visible = false
                onActiveFocusChanged: if (!activeFocus && visible) rowAddInput.commit()
            }
            HoverHandler { id: rowAddHover; cursorShape: Qt.IBeamCursor }
            TapHandler {
                onTapped: {
                    rowAddInput.text = ""
                    rowAddInput.visible = true
                    rowAddInput.forceActiveFocus()
                }
            }
            ToolTip.visible: rowAddHover.hovered && !rowAddInput.visible
            ToolTip.text: qsTr("Type a chord at the end of the row")
        }
    }

    // The cells, centred; a line wider than the chart wraps.
    Flow {
        id: cellsFlow
        objectName: "chartCells"
        visible: !live.typingLine && !live.isRow // (a row: its chips instead)
        readonly property real natural: {
            let sum = 0
            for (let i = 0; i < cellsRepeater.count; ++i) {
                const item = cellsRepeater.itemAt(i)
                if (item !== null) sum += item.width
            }
            return sum
        }
        width: Math.min(live.width, Math.max(natural, 1))
        x: (live.width - width) / 2

        Repeater {
            id: cellsRepeater
            model: live.cells
            delegate: Item {
                id: cell
                objectName: "chartCell"
                required property var modelData
                required property int index
                readonly property var chords: cell.modelData.chords
                readonly property bool editingChord: chordInput.visible
                readonly property bool editingWord: wordInput.visible
                readonly property real gap: cell.modelData.space ? wordMetrics.advanceWidth(" ") : 0
                width: Math.max(word.implicitWidth, chordRow.implicitWidth + 4, live.open ? 18 * live.size : 0,
                                chordInput.visible ? chordInput.width : 0, wordInput.visible ? wordInput.width : 0) + cell.gap
                height: live.chordHeight + wordMetrics.height

                function editChord(index, name) {
                    chordInput.chordIndex = index
                    chordInput.text = name
                    chordInput.visible = true
                    chordInput.forceActiveFocus()
                    chordInput.selectAll()
                }
                function editWord() {
                    wordInput.text = cell.modelData.text
                    wordInput.visible = true
                    wordInput.forceActiveFocus()
                    wordInput.selectAll()
                }
                Connections {
                    target: live
                    function onCellEditAskedChanged() {
                        const asked = live.cellEditAsked
                        if (asked !== null && asked.cell === cell.index) cell.startEditing(asked.what)
                    }
                }
                // Its box ("chord") or its word ("word") typed in (Tab comes here).
                function startEditing(what) {
                    if (what === "chord") {
                        const chords = cell.chords
                        cell.editChord(chords.length > 0 ? chords[0].index : -1, chords.length > 0 ? chords[0].name : "")
                    } else {
                        cell.editWord()
                    }
                }

                // The chord box: its chords, or a bar to type one into.
                Item {
                    id: box
                    objectName: "chordBox"
                    width: cell.width - cell.gap
                    height: live.chordHeight
                    Rectangle {
                        objectName: "chordBar"
                        visible: live.open && cell.chords.length === 0 && !cell.editingChord
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 3 * live.size
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.max(parent.width - 4, 12)
                        height: 3
                        radius: 1.5
                        color: barHover.hovered ? Theme.chord : "#55ffffff"
                    }
                    HoverHandler { id: barHover; enabled: cell.chords.length === 0; cursorShape: Qt.IBeamCursor }
                    TapHandler {
                        enabled: cell.chords.length === 0
                        onTapped: {
                            live.openRequested(live.chartLine)
                            cell.editChord(-1, "")
                        }
                    }
                    Row {
                        id: chordRow
                        visible: !cell.editingChord
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        spacing: 2
                        Repeater {
                            model: cell.chords
                            // (A slot the Row places; the chip inside it moves freely when dragged.)
                            delegate: Item {
                                id: slot
                                required property var modelData
                                width: chip.width
                                height: chip.height
                                ChordDragSource {
                                    id: chip
                                    objectName: "chartChordChip"
                                    // (Named for what chord follow makes it, as on stage.)
                                    Item { objectName: chip.current ? "chartChordCurrent" : chip.next ? "chartChordNext" : "" }
                                    readonly property bool current: live.started && slot.modelData.steps.indexOf(live.currentStep) >= 0
                                    readonly property bool next: !chip.current
                                        && slot.modelData.steps.indexOf(live.started ? live.currentStep + 1 : 0) >= 0
                                    fromLine: live.chartLine
                                    chordIndex: slot.modelData.index
                                    chordName: slot.modelData.name
                                    width: label.implicitWidth + 2 * live.chipPadding
                                    height: live.chordHeight - 2
                                    radius: Theme.radiusSmall
                                    color: dragger.active ? Theme.accent : hover.hovered ? "#33f0bf45" : "transparent"
                                    border.color: dragger.active || hover.hovered ? Theme.chord : chip.next ? Theme.accent : "transparent"
                                    z: dragger.active ? 10 : 1
                                    Drag.active: dragger.active
                                    Drag.keys: ["chord"]
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: height / 2
                                    Text {
                                        id: label
                                        anchors.centerIn: parent
                                        text: slot.modelData.name
                                        color: dragger.active ? "white" : !slot.modelData.understood ? Theme.textDim
                                             : chip.current ? Theme.accent : Theme.chord
                                        font.pixelSize: live.chordFont
                                        font.bold: true
                                        font.strikeout: !slot.modelData.understood
                                    }
                                    HoverHandler { id: hover; cursorShape: Qt.OpenHandCursor }
                                    DragHandler {
                                        id: dragger
                                        objectName: "chordDragger"
                                        cursorShape: Qt.ClosedHandCursor
                                        onActiveChanged: {
                                            live.dragging = active
                                            if (active) return
                                            // Dropped where nothing takes it: back in its box.
                                            if (chip.Drag.drop() === Qt.IgnoreAction) {
                                                chip.x = 0
                                                chip.y = 0
                                            }
                                        }
                                    }
                                    TapHandler {
                                        onTapped: {
                                            live.openRequested(live.chartLine)
                                            cell.editChord(slot.modelData.index, slot.modelData.name)
                                        }
                                    }
                                    TapHandler {
                                        acceptedButtons: Qt.RightButton
                                        onTapped: chipMenu.popup()
                                    }
                                    StageMenu {
                                        id: chipMenu
                                        StageMenuItem {
                                            objectName: "chordHowToPlay"
                                            text: qsTr("How to play %1…").arg(slot.modelData.name)
                                            onTriggered: live.chordDiagramRequested(slot.modelData.name)
                                        }
                                        StageMenuItem {
                                            text: qsTr("Change %1…").arg(slot.modelData.name)
                                            onTriggered: cell.editChord(slot.modelData.index, slot.modelData.name)
                                        }
                                        StageMenuItem {
                                            text: qsTr("Remove %1").arg(slot.modelData.name)
                                            onTriggered: live.doc.renameChordAt(live.chartLine, slot.modelData.index, "")
                                        }
                                    }
                                    ToolTip.visible: hover.hovered && !dragger.active
                                    ToolTip.delay: 900
                                    ToolTip.text: qsTr("Click to change · drag onto another word · right-click: how to play it, remove")
                                }
                            }
                        }
                    }
                    // A chord typed in the box (a new one, or one changed; empty: removed).
                    TextInput {
                        id: chordInput
                        objectName: "chordEditInput"
                        property int chordIndex: -1
                        property bool done: false
                        visible: false
                        anchors.bottom: parent.bottom
                        width: Math.max(contentWidth + 10, 44 * live.size)
                        height: live.chordHeight - 2
                        z: 20
                        leftPadding: 4
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.chord
                        font.pixelSize: live.chordFont
                        font.bold: true
                        selectByMouse: true
                        Rectangle { anchors.fill: parent; z: -1; color: Theme.readoutBackground; border.color: Theme.accent; radius: Theme.radiusSmall }
                        function commit() {
                            if (!chordInput.visible) return // (saved once: Tab, then the focus leaving)
                            const name = chordInput.text.trim()
                            chordInput.visible = false
                            if (chordInput.chordIndex >= 0) {
                                const before = cell.chords.find(c => c.index === chordInput.chordIndex)
                                if (before === undefined || before.name !== name) live.doc.renameChordAt(live.chartLine, chordInput.chordIndex, name)
                            } else if (name !== "") {
                                live.doc.placeChordAt(live.chartLine, cell.modelData.at, name)
                            }
                        }
                        onAccepted: chordInput.commit()
                        Keys.onEscapePressed: chordInput.visible = false
                        Keys.onTabPressed: live.moveOn(cell.index, 1, "chord", chordInput.commit)
                        Keys.onBacktabPressed: live.moveOn(cell.index, -1, "chord", chordInput.commit)
                        onActiveFocusChanged: if (!activeFocus && visible) chordInput.commit()
                    }
                    // (A chord dragged here lands by the chart's drop layer: dropTargetAt.)
                }

                // The word: double-click to change it where it is.
                Text {
                    id: word
                    objectName: "chartWord"
                    visible: !cell.editingWord
                    y: live.chordHeight
                    text: cell.modelData.text
                    color: Theme.text
                    font.pixelSize: live.wordFont
                    TapHandler {
                        onDoubleTapped: {
                            live.openRequested(live.chartLine)
                            cell.editWord()
                        }
                    }
                }
                TextInput {
                    id: wordInput
                    objectName: "wordEditInput"
                    visible: false
                    y: live.chordHeight
                    width: Math.max(contentWidth + 6, 30 * live.size)
                    color: Theme.text
                    font.pixelSize: live.wordFont
                    selectByMouse: true
                    Rectangle { anchors.fill: parent; anchors.margins: -2; z: -1; color: Theme.readoutBackground; border.color: Theme.accent; radius: Theme.radiusSmall }
                    function commit() {
                        if (!wordInput.visible) return // (saved once)
                        wordInput.visible = false
                        if (wordInput.text !== cell.modelData.text) live.doc.setLineLyrics(live.chartLine, live.lyricsWith(cell.modelData, wordInput.text))
                    }
                    onAccepted: wordInput.commit()
                    Keys.onEscapePressed: wordInput.visible = false
                    Keys.onTabPressed: live.moveOn(cell.index, 1, "word", wordInput.commit)
                    Keys.onBacktabPressed: live.moveOn(cell.index, -1, "word", wordInput.commit)
                    onActiveFocusChanged: if (!activeFocus && visible) wordInput.commit()
                }
            }
        }
    }

    // The whole line typed as text (Enter: a new line after the caret;
    // Backspace at the start: onto the line above; Up/Down: the next line).
    Text {
        id: lineChords
        visible: live.typingLine
        anchors.horizontalCenter: parent.horizontalCenter
        height: live.chordHeight
        verticalAlignment: Text.AlignBottom
        text: live.cells.map(c => c.chords.map(ch => ch.name).join(" ")).filter(s => s !== "").join("   ")
        color: Theme.chord
        opacity: 0.6
        font.pixelSize: live.chordFont
        font.bold: true
    }
    TextInput {
        id: lineText
        objectName: "lineTextInput"
        visible: live.typingLine
        y: live.chordHeight
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(contentWidth + 8, 200 * live.size)
        horizontalAlignment: TextInput.AlignHCenter
        color: Theme.text
        font.pixelSize: live.wordFont
        selectByMouse: true
        Rectangle { anchors.fill: parent; anchors.margins: -3; z: -1; color: Theme.readoutBackground; border.color: Theme.accent; radius: Theme.radiusSmall }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: lineText.text === ""
            text: qsTr("Type the words…")
            color: Theme.textDim
            font: lineText.font
        }
        function save() {
            if (!live.typingLine) return // (saved once)
            live.typingLine = false
            if (lineText.text !== live.lyrics) live.doc.setLineLyrics(live.chartLine, lineText.text)
        }
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                event.accepted = true
                live.wantFocus(live.chartLine + 1, 0)
                live.typingLine = false
                live.doc.editLineAndSplit(live.chartLine, lineText.text, lineText.cursorPosition)
            } else if (event.key === Qt.Key_Escape) {
                event.accepted = true
                live.typingLine = false
            } else if (event.key === Qt.Key_Backspace && lineText.cursorPosition === 0 && lineText.selectedText === ""
                       && live.lineAbove === live.chartLine - 1) {
                event.accepted = true
                lineText.save()
                live.wantFocus(live.chartLine - 1, live.lengthAbove)
                live.doc.joinChartLine(live.chartLine)
            } else if (event.key === Qt.Key_Up && live.lineAbove >= 0) {
                event.accepted = true
                const cursor = lineText.cursorPosition
                lineText.save()
                live.wantFocus(live.lineAbove, cursor)
            } else if (event.key === Qt.Key_Down && live.lineBelow >= 0) {
                event.accepted = true
                const cursor = lineText.cursorPosition
                lineText.save()
                live.wantFocus(live.lineBelow, cursor)
            }
        }
        onActiveFocusChanged: if (!activeFocus && live.typingLine) lineText.save()
    }

    // The caret back in this line when the chart asks: a cell, or the line as text.
    function takeFocusIfAsked() {
        const asked = live.focusRequest
        if (asked === null || asked === undefined || asked.line !== live.chartLine) return
        if (asked.cell !== undefined) {
            live.focusTaken()
            if (asked.cell < 0 || asked.cell >= cellsRepeater.count) return
            live.openRequested(live.chartLine)
            live.cellEditAsked = { cell: asked.cell, what: asked.what } // (the cell takes it)
            return
        }
        live.typeLine(asked.cursor)
        live.focusTaken()
    }
    onFocusRequestChanged: takeFocusIfAsked()
    // (Once it is in the scene: a line built by an edit is not yet when completed.)
    Component.onCompleted: Qt.callLater(takeFocusIfAsked)
}
