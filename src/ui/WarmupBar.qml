pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The warm-up's controls along the Practice tab's top: the level, today's
// warm-up (or one exercise and its hands), the tempo and how many clean runs
// there are towards the next one.
RowLayout {
    id: bar

    required property WarmupController warmup

    readonly property var levelNames: [qsTr("Beginner"), qsTr("Intermediate"), qsTr("Pro")]
    readonly property var handNames: [qsTr("Right hand"), qsTr("Left hand"), qsTr("Both hands")]
    readonly property var current: bar.warmup.exercise >= 0 && bar.warmup.exercise < bar.warmup.exercises.length
                                   ? bar.warmup.exercises[bar.warmup.exercise] : null

    spacing: Theme.spacing

    // The level: the next one opens when every exercise of this one is passed.
    Row {
        spacing: -1
        Repeater {
            model: bar.levelNames
            delegate: StageButton {
                required property string modelData
                required property int index
                objectName: "warmupLevel" + index
                text: modelData
                checked: bar.warmup.level === index
                enabled: index <= bar.warmup.unlockedLevel
                tip: index <= bar.warmup.unlockedLevel ? "" : qsTr("Pass every %1 exercise to open this level").arg(bar.levelNames[index - 1])
                onClicked: bar.warmup.level = index
            }
        }
    }

    StageButton {
        objectName: "warmupStart"
        visible: !bar.warmup.routine
        text: qsTr("Start warm-up (%1 min)").arg(bar.warmup.routineMinutes)
        iconSource: "icons/player-play.svg"
        tone: "accent"
        onClicked: bar.warmup.startRoutine()
    }
    Label {
        objectName: "warmupStep"
        visible: bar.warmup.routine
        text: qsTr("Step %1 of %2").arg(bar.warmup.routineStep + 1).arg(bar.warmup.routineSteps)
        color: Theme.text
        font.bold: true
    }

    StageComboBox {
        objectName: "warmupExercise"
        Layout.preferredWidth: 220
        model: bar.warmup.exercises.map(e => e.name)
        currentIndex: bar.warmup.exercise
        displayText: currentIndex >= 0 ? currentText : qsTr("Pick an exercise")
        onActivated: (index) => bar.warmup.startExercise(index, 0)
    }
    Row {
        spacing: -1
        visible: bar.warmup.exercise >= 0
        Repeater {
            model: bar.handNames
            delegate: StageButton {
                required property string modelData
                required property int index
                objectName: "warmupHands" + index
                text: modelData
                checked: bar.warmup.hands === index
                onClicked: bar.warmup.startExercise(bar.warmup.exercise, index)
            }
        }
    }

    Item { Layout.fillWidth: true }

    // The tempo now, the target, and the clean runs towards the next tempo.
    Column {
        visible: bar.current !== null
        Label {
            anchors.right: parent.right
            text: bar.current !== null ? qsTr("%1 BPM  ·  target %2").arg(Math.round(bar.warmup.tempo)).arg(Math.round(bar.current.target)) : ""
            color: Theme.text
            font.bold: true
        }
        Row {
            anchors.right: parent.right
            spacing: 4
            Label { text: qsTr("Clean runs"); color: Theme.textDim; font.pixelSize: Theme.smallFontSize }
            Repeater {
                model: 3
                delegate: Rectangle {
                    required property int index
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10
                    height: 10
                    radius: 5
                    color: bar.current !== null && index < bar.current.cleanRuns ? Theme.meterLow : "transparent"
                    border.color: Theme.textDim
                }
            }
        }
    }
    StageButton {
        objectName: "warmupStop"
        visible: bar.warmup.playing
        iconSource: "icons/player-stop.svg"
        tip: qsTr("Stop this run")
        onClicked: bar.warmup.stop()
    }
}
