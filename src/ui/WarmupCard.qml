pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Over the falling notes while warming up: what to play (and how) during a
// run; the score after it (stars, the notes, the timing, the hands, one
// thing to do next); the level's exercises before starting; and "done" at
// the end of today's warm-up.
Item {
    id: card

    required property WarmupController warmup

    readonly property var handNames: [qsTr("Right hand"), qsTr("Left hand"), qsTr("Both hands")]
    readonly property var current: card.warmup.exercise >= 0 && card.warmup.exercise < card.warmup.exercises.length
                                   ? card.warmup.exercises[card.warmup.exercise] : null
    readonly property var result: card.warmup.result
    readonly property bool scored: card.result.stars !== undefined && !card.warmup.playing
    readonly property bool played: (card.result.right || 0) > 0
    // The run was with both hands: how far apart they landed (-1: one hand).
    readonly property bool bothHands: card.result.handsApartMs !== undefined && card.result.handsApartMs >= 0

    function stars(n) { return "★".repeat(n) + "☆".repeat(3 - n) }

    // While playing: the exercise, the hands, how to play it.
    Rectangle {
        objectName: "warmupPlaying"
        visible: card.warmup.playing && card.current !== null
        x: 16
        y: 16
        width: Math.min(card.width - 32, playingText.implicitWidth + 28)
        height: playingText.implicitHeight + 20
        radius: Theme.radiusCard
        color: "#cc0b0e16"
        border.color: Theme.outline
        Column {
            id: playingText
            anchors.centerIn: parent
            spacing: 4
            Label {
                text: card.current !== null ? card.current.name + "  ·  " + card.handNames[card.warmup.hands] : ""
                color: "white"
                font.pixelSize: Theme.fontSize + 4
                font.bold: true
            }
            Label {
                width: Math.min(560, implicitWidth)
                text: card.current !== null ? card.current.tip : ""
                color: Theme.textDim
                wrapMode: Text.Wrap
            }
        }
    }

    // A panel in the middle: the start, a run's score, or done.
    Rectangle {
        objectName: "warmupPanel"
        visible: !card.warmup.playing && (card.scored || card.warmup.exercise < 0 || card.warmup.finishedToday)
        anchors.centerIn: parent
        width: Math.min(card.width - 40, 620)
        height: panelColumn.implicitHeight + 40
        radius: Theme.radiusDialog
        color: "#ee10131c"
        border.color: Theme.outline

        ColumnLayout {
            id: panelColumn
            anchors.fill: parent
            anchors.margins: 20
            spacing: Theme.spacing

            // ---- Done for today
            Label {
                visible: card.warmup.finishedToday
                text: qsTr("Warm-up done!")
                color: Theme.chord
                font.pixelSize: 28
                font.bold: true
            }
            Label {
                visible: card.warmup.finishedToday
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Your fingers are ready. Come back tomorrow: a few minutes every day does more than an hour once a week.")
                color: Theme.text
            }

            // ---- A run's score
            Label {
                objectName: "warmupStars"
                visible: card.scored && !card.warmup.finishedToday
                text: card.stars(card.result.stars || 0)
                color: Theme.chord
                font.pixelSize: 40
            }
            Label {
                visible: card.scored && !card.warmup.finishedToday && card.current !== null
                text: card.current !== null ? card.current.name + "  ·  " + card.handNames[card.result.hands || 0] : ""
                color: Theme.text
                font.pixelSize: Theme.fontSize + 2
                font.bold: true
            }
            GridLayout {
                visible: card.scored && !card.warmup.finishedToday
                columns: 2
                columnSpacing: 24
                rowSpacing: 4
                Label { text: qsTr("Notes"); color: Theme.textDim }
                Label {
                    objectName: "warmupNotesScore"
                    text: qsTr("%1 of %2 right").arg(card.result.right || 0).arg(card.result.notes || 0)
                          + ((card.result.missed || 0) > 0 ? qsTr(", %1 missed").arg(card.result.missed) : "")
                          + ((card.result.extra || 0) > 0 ? qsTr(", %1 extra").arg(card.result.extra) : "")
                    color: (card.result.missed || 0) > 0 ? Theme.danger : Theme.text
                }
                // (Timing and evenness only from notes played.)
                Label { text: qsTr("Timing"); color: Theme.textDim; visible: card.played }
                Label {
                    visible: card.played
                    text: qsTr("%1 ms from the beat").arg(Math.round(card.result.timingMs || 0))
                          + (Math.abs(card.result.driftMs || 0) >= 10 ? ((card.result.driftMs > 0) ? qsTr(" (behind)") : qsTr(" (ahead)")) : "")
                    color: Theme.text
                }
                Label { text: qsTr("Evenness"); color: Theme.textDim; visible: card.played }
                Label { text: qsTr("±%1 ms").arg(Math.round(card.result.evennessMs || 0)); color: Theme.text; visible: card.played }
                Label { text: qsTr("Hands together"); color: Theme.textDim; visible: card.bothHands }
                Label {
                    visible: card.bothHands
                    text: qsTr("%1 ms apart").arg(Math.round(card.result.handsApartMs || 0))
                    color: Theme.text
                }
            }
            Label {
                objectName: "warmupTip"
                visible: card.scored && !card.warmup.finishedToday
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: card.result.tip || ""
                color: "white"
                font.pixelSize: Theme.fontSize + 1
            }
            Label {
                objectName: "warmupNews"
                visible: card.scored && (card.result.tempoUp || card.result.passed || card.result.levelUp)
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: card.result.levelUp ? qsTr("Level passed! The next level is open.")
                      : card.result.passed ? qsTr("Exercise passed at its target tempo!")
                      : card.result.tempoUp ? qsTr("Three clean runs: up to %1 BPM.").arg(Math.round(card.warmup.tempo)) : ""
                color: Theme.meterLow
                font.bold: true
            }

            // ---- Before starting: the level's exercises
            Label {
                visible: card.warmup.exercise < 0 && !card.warmup.finishedToday
                text: qsTr("Warm up")
                color: "white"
                font.pixelSize: 28
                font.bold: true
            }
            Label {
                visible: card.warmup.exercise < 0 && !card.warmup.finishedToday
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("About %1 minutes. Each exercise right hand, then left hand, then both. Play along with the falling notes: "
                           + "three clean runs with both hands and the tempo goes up. Finger numbers: 1 is the thumb, 5 the little finger.")
                      .arg(card.warmup.routineMinutes)
                color: Theme.text
            }
            Repeater {
                model: card.warmup.exercise < 0 && !card.warmup.finishedToday ? card.warmup.exercises : []
                delegate: RowLayout {
                    id: row
                    required property var modelData
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.name
                        color: Theme.text
                    }
                    Label { text: Math.round(row.modelData.tempo) + " BPM"; color: Theme.textDim }
                    Label { text: row.modelData.passed ? qsTr("Passed") : card.stars(row.modelData.stars); color: row.modelData.passed ? Theme.meterLow : Theme.chord }
                    StageButton {
                        text: qsTr("Play")
                        onClicked: card.warmup.startExercise(row.modelData.index, 0)
                    }
                }
            }

            // ---- What next
            RowLayout {
                Layout.topMargin: Theme.spacing
                spacing: Theme.spacing
                Item { Layout.fillWidth: true }
                StageButton {
                    objectName: "warmupAgain"
                    visible: card.scored && !card.warmup.finishedToday
                    text: qsTr("Again")
                    onClicked: card.warmup.again()
                }
                StageButton {
                    objectName: "warmupNext"
                    visible: card.scored && !card.warmup.finishedToday
                    tone: "accent"
                    text: card.warmup.routine ? qsTr("Next") : qsTr("Next: %1").arg(card.handNames[(card.warmup.hands + 1) % 3])
                    onClicked: card.warmup.next()
                }
                StageButton {
                    objectName: "warmupBegin"
                    visible: card.warmup.exercise < 0 || card.warmup.finishedToday
                    tone: "accent"
                    text: card.warmup.finishedToday ? qsTr("Warm up again") : qsTr("Start warm-up")
                    onClicked: card.warmup.startRoutine()
                }
            }
        }
    }
}
