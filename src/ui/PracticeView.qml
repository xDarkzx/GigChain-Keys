pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Practice tab: the song's chords as notes falling onto a keyboard,
// Synthesia-style (docs/superpowers/specs/2026-10-04-practice-tab-design.md).
// Listen plays them through the patch; Play along lets the player play at
// the tempo; Wait for me waits at each chord for its keys. The keyboard
// lights the notes to play; a key pressed is green when right, red when not.
Rectangle {
    id: view
    objectName: "practiceView"

    required property PracticeController practice
    required property EngineStatus engineStatus

    color: Theme.performBackground

    // Leaving the tab pauses (its notes let go).
    onVisibleChanged: if (!visible) view.practice.pause()

    // The keys shown: C2 to C6, wider when the song's notes go further.
    readonly property int lowest: {
        let low = 36
        for (const n of view.practice.notes) low = Math.min(low, n.pitch)
        return low - (low % 12)
    }
    readonly property int highest: {
        let high = 84
        for (const n of view.practice.notes) high = Math.max(high, n.pitch)
        return high + (11 - high % 12)
    }
    function isBlack(note) { return [1, 3, 6, 8, 10].indexOf(((note % 12) + 12) % 12) >= 0 }
    readonly property var whiteNotes: {
        const list = []
        for (let n = view.lowest; n <= view.highest; ++n) if (!view.isBlack(n)) list.push(n)
        return list
    }
    readonly property real whiteWidth: board.width / Math.max(1, view.whiteNotes.length)
    readonly property real blackWidth: view.whiteWidth * 0.6
    function keyX(note) {
        if (!view.isBlack(note)) return view.whiteNotes.indexOf(note) * view.whiteWidth
        return (view.whiteNotes.indexOf(note - 1) + 1) * view.whiteWidth - view.blackWidth / 2
    }
    function keyWidth(note) { return view.isBlack(note) ? view.blackWidth : view.whiteWidth }

    readonly property var targets: view.practice.targetNotes
    readonly property var pressed: view.engineStatus.keyVelocities
    function isPressed(note) { return (view.pressed[note] || 0) > 0 }
    function isTarget(note) { return view.targets.indexOf(note) >= 0 }

    // Two bars ahead are seen falling.
    readonly property real beatsAhead: view.practice.beatsPerBar * 2
    readonly property real pxPerBeat: falls.height / Math.max(1, view.beatsAhead)
    function yOf(beat) { return falls.height - (beat - view.practice.position) * view.pxPerBeat }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Play, the mode, the speed, the loop; the chord now and next.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.barTop }
                GradientStop { position: 1.0; color: Theme.barBottom }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingLarge
                anchors.rightMargin: Theme.spacingLarge
                spacing: Theme.spacing
                StageButton {
                    objectName: "practicePlay"
                    iconSource: view.practice.playing ? "icons/player-pause.svg" : "icons/player-play.svg"
                    text: view.practice.playing ? qsTr("Pause") : qsTr("Play")
                    tone: "accent"
                    enabled: view.practice.chords.length > 0
                    onClicked: view.practice.playing ? view.practice.pause() : view.practice.play()
                }
                StageButton {
                    objectName: "practiceStop"
                    iconSource: "icons/player-stop.svg"
                    tip: qsTr("Stop, and back to the start")
                    enabled: view.practice.chords.length > 0
                    onClicked: view.practice.stop()
                }
                Item { Layout.preferredWidth: Theme.spacing }
                Row {
                    spacing: -1
                    Repeater {
                        model: [qsTr("Listen"), qsTr("Play along"), qsTr("Wait for me")]
                        delegate: StageButton {
                            required property string modelData
                            required property int index
                            objectName: "practiceMode" + index
                            text: modelData
                            checked: view.practice.mode === index
                            tip: index === 0 ? qsTr("It plays the song through your instrument while the notes fall")
                               : index === 1 ? qsTr("The notes fall at the tempo: you play them")
                                             : qsTr("Each chord waits until you hold all its notes")
                            onClicked: view.practice.mode = index
                        }
                    }
                }
                Item { Layout.preferredWidth: Theme.spacing }
                Label { text: qsTr("Speed"); color: Theme.textDim }
                Slider {
                    id: speedSlider
                    objectName: "practiceSpeed"
                    Layout.preferredWidth: 130
                    from: 0.25
                    to: 1.0
                    stepSize: 0.05
                    snapMode: Slider.SnapAlways
                    value: view.practice.speed
                    focusPolicy: Qt.NoFocus
                    onMoved: view.practice.speed = value
                }
                Label {
                    text: Math.round(view.practice.speed * 100) + "%"
                    color: Theme.text
                    Layout.preferredWidth: 40
                }
                Label { text: qsTr("Loop"); color: Theme.textDim }
                StageComboBox {
                    id: loopBox
                    objectName: "practiceLoop"
                    Layout.preferredWidth: 160
                    model: [qsTr("Whole song")].concat(view.practice.sections.map(s => s.name))
                    currentIndex: {
                        const at = view.practice.sections.findIndex(s => s.section === view.practice.loopSection)
                        return at + 1
                    }
                    onActivated: (index) => view.practice.loopSection = index === 0 ? -1 : view.practice.sections[index - 1].section
                }
                Item { Layout.fillWidth: true }
                Column {
                    Label {
                        anchors.right: parent.right
                        text: view.practice.waiting ? qsTr("Play it…") : qsTr("Now")
                        color: view.practice.waiting ? Theme.accent : Theme.textDim
                        font.pixelSize: Theme.smallFontSize
                    }
                    Label {
                        objectName: "practiceNow"
                        anchors.right: parent.right
                        text: view.practice.nowChord !== "" ? view.practice.nowChord : "–"
                        color: Theme.chord
                        font.pixelSize: 26
                        font.bold: true
                    }
                }
                Column {
                    Layout.leftMargin: Theme.spacingLarge
                    Label { text: qsTr("Next"); color: Theme.textDim; font.pixelSize: Theme.smallFontSize }
                    Label {
                        objectName: "practiceNext"
                        text: view.practice.nextChord !== "" ? view.practice.nextChord : "–"
                        color: Theme.text
                        font.pixelSize: 20
                        font.bold: true
                    }
                }
            }
        }

        // The notes falling onto the line above the keys.
        Item {
            id: falls
            objectName: "practiceFalls"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // A song without chords: nothing to practise; where to add them.
            Label {
                visible: view.practice.chords.length === 0
                anchors.centerIn: parent
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("This song has no chords to practise yet.\nType or paste them in the Chart tab.")
                color: Theme.textDim
                font.pixelSize: Theme.fontSize + 6
            }

            // A line at every bar; a section's name where it starts.
            Repeater {
                model: Math.ceil(view.practice.length / Math.max(1, view.practice.beatsPerBar)) + 1
                delegate: Rectangle {
                    required property int index
                    readonly property real beat: index * view.practice.beatsPerBar
                    x: 0
                    width: falls.width
                    height: 1
                    y: view.yOf(beat)
                    visible: y >= 0 && y <= falls.height
                    color: "#1affffff"
                }
            }
            Repeater {
                model: view.practice.sections
                delegate: Item {
                    id: mark
                    required property var modelData
                    width: falls.width
                    height: 22
                    y: view.yOf(mark.modelData.start) - height
                    visible: y > -height && y < falls.height
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.accent; opacity: 0.6 }
                    Text {
                        // (At the right: the bass notes fall at the left.)
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        text: mark.modelData.name
                        color: Theme.accent
                        font.pixelSize: Theme.fontSize + 2
                        font.bold: true
                        font.capitalization: Font.AllUppercase
                    }
                }
            }

            // The notes.
            Repeater {
                objectName: "practiceNotes"
                model: view.practice.notes
                delegate: Rectangle {
                    id: fallingNote
                    required property var modelData
                    readonly property real noteTop: view.yOf(fallingNote.modelData.start + fallingNote.modelData.length)
                    readonly property real noteBottom: view.yOf(fallingNote.modelData.start)
                    objectName: "practiceNote"
                    x: view.keyX(fallingNote.modelData.pitch) + 1
                    width: view.keyWidth(fallingNote.modelData.pitch) - 2
                    y: fallingNote.noteTop
                    height: Math.max(4, fallingNote.noteBottom - fallingNote.noteTop - 3)
                    visible: fallingNote.noteBottom > 0 && fallingNote.noteTop < falls.height
                    radius: 4
                    color: fallingNote.modelData.left ? Theme.accent : Theme.chord
                    border.color: Qt.lighter(color, 1.3)
                }
            }
            // The chords' names, beside their notes.
            Repeater {
                model: view.practice.chords
                delegate: Text {
                    id: chordName
                    required property var modelData
                    readonly property real landing: view.yOf(chordName.modelData.start)
                    x: Math.max(4, view.keyX(chordName.modelData.low) - width - 8)
                    y: chordName.landing - height - 2
                    visible: chordName.landing > 0 && chordName.y < falls.height
                    text: chordName.modelData.name
                    color: Theme.chord
                    font.pixelSize: 22
                    font.bold: true
                    style: Text.Outline
                    styleColor: "black"
                }
            }
        }

        // The line the notes land on.
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; color: Theme.accent }

        // The keyboard: the notes to play lit, what is pressed green or red.
        Item {
            id: board
            objectName: "practiceKeyboard"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(150, view.whiteWidth * 5)

            function keyColour(note, black) {
                const down = view.isPressed(note)
                const wanted = view.isTarget(note)
                if (down) return wanted ? "#3fbf5f" : "#d9534f"
                if (wanted) return note < 48 ? Theme.accent : Theme.chord
                return black ? "#15171b" : "#f2f2f2"
            }
            Repeater {
                model: view.whiteNotes
                delegate: Rectangle {
                    required property int modelData
                    objectName: "practiceKey" + modelData
                    x: view.keyX(modelData)
                    width: view.whiteWidth - 1
                    height: board.height
                    radius: 3
                    color: board.keyColour(modelData, false)
                    border.color: "#555"
                    Text {
                        visible: parent.modelData % 12 === 0
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        bottomPadding: 4
                        text: "C" + (Math.floor(parent.modelData / 12) - 1)
                        color: "#777"
                        font.pixelSize: 10
                    }
                }
            }
            Repeater {
                model: {
                    const list = []
                    for (let n = view.lowest; n <= view.highest; ++n) if (view.isBlack(n)) list.push(n)
                    return list
                }
                delegate: Rectangle {
                    required property int modelData
                    objectName: "practiceKey" + modelData
                    x: view.keyX(modelData)
                    width: view.blackWidth
                    height: board.height * 0.62
                    radius: 2
                    color: board.keyColour(modelData, true)
                    border.color: "#000"
                }
            }
        }
    }
}
