pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Practice mode: the song's chords as notes falling onto a keyboard,
// as the piano videos show them (docs/superpowers/specs/2026-10-04-practice-tab-design.md):
// glowing bars, sparks and a light where they land. Listen plays them
// through the patch; Play along lets the player play at the tempo; Wait for
// me waits at each chord for its keys. The keyboard lights the notes to
// play; a key pressed is green when right, red when not.
Rectangle {
    id: view
    objectName: "practiceView"

    required property PracticeController practice
    required property EngineStatus engineStatus

    color: "#05060a"

    // Leaving the mode pauses (its notes let go).
    onVisibleChanged: if (!visible) view.practice.pause()

    // The hands' colours: the left (the bass) blue, the right gold.
    readonly property color leftColour: "#36c5ff"
    readonly property color rightColour: "#ffb13b"
    readonly property color rightKey: "#3fdc6a"
    readonly property color wrongKey: "#ff4d4d"

    function isBlack(note) { return [1, 3, 6, 8, 10].indexOf(((note % 12) + 12) % 12) >= 0 }

    // The keys the song needs: C2 to C6 at least, wider when its notes go further.
    readonly property int neededLow: {
        let low = 36
        for (const n of view.practice.notes) low = Math.min(low, n.pitch)
        return low - (low % 12)
    }
    readonly property int neededHigh: {
        let high = 84
        for (const n of view.practice.notes) high = Math.max(high, n.pitch)
        return high + (11 - high % 12)
    }
    // Keys keep a piano's shape (as the keyboard below the Edit view): no
    // wider than this. A wide window shows more of the piano (up to its 88
    // keys, A0 to C8) rather than stretching them.
    readonly property real widestKey: 40
    readonly property real naturalKey: 34
    readonly property var whiteNotes: {
        const list = []
        for (let n = view.neededLow; n <= view.neededHigh; ++n) if (!view.isBlack(n)) list.push(n)
        const fits = Math.floor(view.width / view.naturalKey)
        let below = true
        while (list.length < fits) {
            const down = list[0] - (view.isBlack(list[0] - 1) ? 2 : 1)
            const up = list[list.length - 1] + (view.isBlack(list[list.length - 1] + 1) ? 2 : 1)
            const canDown = down >= 21
            const canUp = up <= 108
            if (!canDown && !canUp) break
            if ((below && canDown) || !canUp) list.unshift(down)
            else list.push(up)
            below = !below
        }
        return list
    }
    readonly property int lowest: view.whiteNotes.length > 0 ? view.whiteNotes[0] : 36
    readonly property int highest: view.whiteNotes.length > 0 ? view.whiteNotes[view.whiteNotes.length - 1] : 84
    readonly property real whiteWidth: Math.min(view.widestKey, view.width / Math.max(1, view.whiteNotes.length))
    readonly property real blackWidth: view.whiteWidth * 0.6
    // The board centred when the keys do not fill the width.
    readonly property real inset: Math.max(0, (view.width - view.whiteWidth * view.whiteNotes.length) / 2)
    function keyX(note) {
        if (!view.isBlack(note)) return view.inset + view.whiteNotes.indexOf(note) * view.whiteWidth
        return view.inset + (view.whiteNotes.indexOf(note - 1) + 1) * view.whiteWidth - view.blackWidth / 2
    }
    function keyWidth(note) { return view.isBlack(note) ? view.blackWidth : view.whiteWidth }

    readonly property var targets: view.practice.targetNotes
    readonly property var pressed: view.engineStatus.keyVelocities
    function isPressed(note) { return (view.pressed[note] || 0) > 0 }
    function isTarget(note) { return view.targets.indexOf(note) >= 0 }
    function isBass(note) { return view.targets.length > 0 && view.targets[0] === note }
    function isLanding(start, length) {
        return view.practice.position >= start - 1e-9 && view.practice.position < start + length - 1e-9
    }

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

            // A deep night sky, lighter towards the keys.
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#05060a" }
                    GradientStop { position: 1.0; color: "#121624" }
                }
            }
            // A faint lane at every C and F, as the videos show.
            Repeater {
                model: view.whiteNotes.filter(n => n % 12 === 0 || n % 12 === 5)
                delegate: Rectangle {
                    required property int modelData
                    x: view.keyX(modelData)
                    width: 1
                    height: falls.height
                    color: modelData % 12 === 0 ? "#22ffffff" : "#10ffffff"
                }
            }

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
                    color: "#14ffffff"
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

            // The glow along the line the notes land on.
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 18
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Qt.alpha(Theme.accent, 0.45) }
                }
            }

            // The notes: glowing bars, brighter as they land.
            Repeater {
                objectName: "practiceNotes"
                model: view.practice.notes
                delegate: Item {
                    id: fallingNote
                    required property var modelData
                    readonly property real noteTop: view.yOf(fallingNote.modelData.start + fallingNote.modelData.length)
                    readonly property real noteBottom: view.yOf(fallingNote.modelData.start)
                    readonly property color tone: fallingNote.modelData.left ? view.leftColour : view.rightColour
                    readonly property bool landing: view.isLanding(fallingNote.modelData.start, fallingNote.modelData.length)
                    objectName: "practiceNote"
                    x: view.keyX(fallingNote.modelData.pitch) + 1
                    width: view.keyWidth(fallingNote.modelData.pitch) - 2
                    y: fallingNote.noteTop
                    height: Math.max(4, fallingNote.noteBottom - fallingNote.noteTop - 3)
                    visible: fallingNote.noteBottom > 0 && fallingNote.noteTop < falls.height

                    // The glow around it.
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -7
                        radius: 12
                        color: fallingNote.tone
                        opacity: fallingNote.landing ? 0.28 : 0.10
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: 9
                        color: fallingNote.tone
                        opacity: fallingNote.landing ? 0.45 : 0.22
                    }
                    // The bar: lit from the left, a bright rim.
                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        border.width: 1
                        border.color: Qt.lighter(fallingNote.tone, 1.6)
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Qt.lighter(fallingNote.tone, fallingNote.landing ? 1.7 : 1.35) }
                            GradientStop { position: 0.5; color: fallingNote.landing ? Qt.lighter(fallingNote.tone, 1.25) : fallingNote.tone }
                            GradientStop { position: 1.0; color: Qt.darker(fallingNote.tone, 1.4) }
                        }
                    }
                    // A shine down its left side.
                    Rectangle {
                        x: 3
                        y: 4
                        width: Math.max(2, parent.width * 0.18)
                        height: Math.max(0, parent.height - 8)
                        radius: width / 2
                        color: "white"
                        opacity: 0.35
                    }
                }
            }

            // Where a note lands: a beam of light up from its key and sparks.
            Repeater {
                model: view.practice.notes
                delegate: Item {
                    id: spark
                    required property var modelData
                    readonly property color tone: spark.modelData.left ? view.leftColour : view.rightColour
                    objectName: "practiceSpark"
                    x: view.keyX(spark.modelData.pitch)
                    width: view.keyWidth(spark.modelData.pitch)
                    height: falls.height
                    visible: view.isLanding(spark.modelData.start, spark.modelData.length)

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width + 10
                        height: 140
                        radius: 6
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 0.7; color: Qt.alpha(spark.tone, 0.35) }
                            GradientStop { position: 1.0; color: Qt.alpha(Qt.lighter(spark.tone, 1.4), 0.85) }
                        }
                    }
                    // A flare where it touches the key.
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height - height / 2
                        width: parent.width * 1.8
                        height: 14
                        radius: 7
                        color: Qt.lighter(spark.tone, 1.6)
                        opacity: 0.8
                    }
                    Repeater {
                        model: 9
                        delegate: Rectangle {
                            id: particle
                            required property int index
                            readonly property real rise: 40 + Math.random() * 90
                            readonly property real drift: (Math.random() - 0.5) * 50
                            readonly property real home: spark.width / 2 - width / 2
                            width: 2 + Math.random() * 3
                            height: width
                            radius: width / 2
                            color: particle.index % 3 === 0 ? "white" : Qt.lighter(spark.tone, 1.5)
                            x: particle.home
                            y: spark.height
                            opacity: 0

                            SequentialAnimation {
                                running: spark.visible
                                loops: Animation.Infinite
                                PauseAnimation { duration: particle.index * 55 }
                                ParallelAnimation {
                                    NumberAnimation {
                                        target: particle; property: "y"
                                        from: spark.height - 2; to: spark.height - particle.rise
                                        duration: 520 + particle.index * 30; easing.type: Easing.OutQuad
                                    }
                                    NumberAnimation {
                                        target: particle; property: "x"
                                        from: particle.home; to: particle.home + particle.drift
                                        duration: 520 + particle.index * 30
                                    }
                                    NumberAnimation {
                                        target: particle; property: "opacity"
                                        from: 1; to: 0
                                        duration: 520 + particle.index * 30; easing.type: Easing.InQuad
                                    }
                                }
                            }
                        }
                    }
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
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; color: Qt.lighter(Theme.accent, 1.3) }

        // The keyboard: the notes to play lit, what is pressed green or red.
        Item {
            id: board
            objectName: "practiceKeyboard"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(150, view.whiteWidth * 4.2)

            // The key's lit colour ("": not lit).
            function keyColour(note) : string {
                if (view.isPressed(note)) return String(view.isTarget(note) ? view.rightKey : view.wrongKey)
                if (view.isTarget(note)) return String(view.isBass(note) ? view.leftColour : view.rightColour)
                return ""
            }
            Repeater {
                model: view.whiteNotes
                delegate: Rectangle {
                    id: whiteKey
                    required property int modelData
                    readonly property string lit: board.keyColour(whiteKey.modelData)
                    readonly property bool isLit: whiteKey.lit !== ""
                    objectName: "practiceKey" + modelData
                    x: view.keyX(modelData)
                    width: view.whiteWidth - 1
                    height: board.height
                    radius: 3
                    border.color: "#555"
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: whiteKey.isLit ? Qt.lighter(whiteKey.lit, 1.3) : "#ffffff" }
                        GradientStop { position: 1.0; color: whiteKey.isLit ? whiteKey.lit : "#d8d8d8" }
                    }
                    Text {
                        visible: whiteKey.modelData % 12 === 0
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        bottomPadding: 4
                        text: "C" + (Math.floor(whiteKey.modelData / 12) - 1)
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
                    id: blackKey
                    required property int modelData
                    readonly property string lit: board.keyColour(blackKey.modelData)
                    readonly property bool isLit: blackKey.lit !== ""
                    objectName: "practiceKey" + modelData
                    x: view.keyX(modelData)
                    width: view.blackWidth
                    height: board.height * 0.62
                    radius: 2
                    border.color: "#000"
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: blackKey.isLit ? Qt.lighter(blackKey.lit, 1.2) : "#3a3d44" }
                        GradientStop { position: 1.0; color: blackKey.isLit ? Qt.darker(blackKey.lit, 1.2) : "#0b0c0e" }
                    }
                }
            }
        }
    }
}
