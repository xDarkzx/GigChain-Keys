pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// The on-screen keyboard, as instruments show it: the keys light up as they
// are played (brighter the harder), with the pitch bend and mod wheel bars
// and a sustain light on the left. Clicking a key plays it.
StagePanel {
    id: board

    required property EngineStatus engineStatus

    // 88 keys: A0 to C8.
    readonly property int lowest: 21
    readonly property int highest: 108
    function isBlack(note) {
        const k = note % 12
        return k === 1 || k === 3 || k === 6 || k === 8 || k === 10
    }
    readonly property var whiteNotes: {
        const list = []
        for (let n = lowest; n <= highest; ++n) if (!isBlack(n)) list.push(n)
        return list
    }
    // Each black key with the white key it sits after.
    readonly property var blackNotes: {
        const list = []
        let white = -1
        for (let n = lowest; n <= highest; ++n) {
            if (isBlack(n)) list.push({ note: n, after: white })
            else ++white
        }
        return list
    }
    readonly property var velocities: engineStatus.keyVelocities
    // A key's colour played at `velocity`: tinted blue, more the harder.
    function lit(base, velocity) {
        const strength = velocity > 0 ? 0.55 + (0.45 * velocity / 127) : 0
        return Qt.tint(base, Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, strength))
    }

    // A white key is about 4.2 times as tall as it is wide (as on screen
    // keyboards in instruments). The keyboard grows taller with the window,
    // up to a point; beyond that the keys keep their shape and the keyboard
    // sits in the middle instead of stretching.
    readonly property real keyRatio: 4.2
    readonly property int shortest: 96
    readonly property int tallest: 160

    bar: true
    implicitHeight: Math.max(shortest, Math.min(tallest, ((width - 90) / whiteNotes.length) * keyRatio + 12))

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 8

        // ---------------------------------------------- wheels and pedal
        Row {
            Layout.fillHeight: true
            spacing: 6

            // Pitch bend: moves up or down from the centre line.
            Column {
                height: parent.height
                spacing: 2
                Rectangle {
                    id: bendSlot
                    objectName: "pitchBendBar"
                    width: 16
                    height: parent.height - 14
                    radius: 3
                    color: Theme.readoutBackground
                    border.color: Theme.outline
                    readonly property real bend: board.engineStatus.pitchBend
                    Rectangle { // the centre line
                        x: 2; width: parent.width - 4; height: 1
                        y: parent.height / 2
                        color: Theme.textDim
                        opacity: 0.5
                    }
                    Rectangle {
                        x: 2
                        width: parent.width - 4
                        readonly property real half: (parent.height - 4) / 2
                        height: Math.max(2, Math.abs(bendSlot.bend) * half)
                        y: bendSlot.bend >= 0 ? parent.height / 2 - height : parent.height / 2
                        radius: 2
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Theme.accentTop }
                            GradientStop { position: 1.0; color: Theme.accentBottom }
                        }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Pitch")
                    color: Theme.textDim
                    font.pixelSize: 9
                }
            }

            // Mod wheel: fills from the bottom.
            Column {
                height: parent.height
                spacing: 2
                Rectangle {
                    objectName: "modWheelBar"
                    width: 16
                    height: parent.height - 14
                    radius: 3
                    color: Theme.readoutBackground
                    border.color: Theme.outline
                    Rectangle {
                        x: 2
                        width: parent.width - 4
                        height: Math.max(0, board.engineStatus.modWheel * (parent.height - 4))
                        y: parent.height - 2 - height
                        radius: 2
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Theme.accentTop }
                            GradientStop { position: 1.0; color: Theme.accentBottom }
                        }
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Mod")
                    color: Theme.textDim
                    font.pixelSize: 9
                }
            }

            // Sustain pedal light.
            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Rectangle {
                    objectName: "sustainLight"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 12
                    height: 12
                    radius: 6
                    border.color: Theme.outline
                    color: board.engineStatus.sustain ? Theme.meterLow : "#2a2e35"
                }
                Text {
                    text: qsTr("Sus")
                    color: Theme.textDim
                    font.pixelSize: 9
                }
            }
        }

        StageDivider { vertical: true; Layout.fillHeight: true }

        // ---------------------------------------------- the keys
        Item {
            id: keyArea
            Layout.fillWidth: true
            Layout.fillHeight: true

        Item {
            id: keys
            objectName: "keyboardKeys"
            // As wide as the room allows without the keys losing their shape.
            readonly property real whiteWidth: Math.min(keyArea.width / board.whiteNotes.length, keyArea.height / board.keyRatio)
            readonly property real blackWidth: whiteWidth * 0.62
            width: whiteWidth * board.whiteNotes.length
            height: keyArea.height
            anchors.horizontalCenter: parent.horizontalCenter

            Repeater {
                model: board.whiteNotes
                delegate: Rectangle {
                    id: white
                    required property int modelData
                    required property int index
                    readonly property int velocity: board.velocities[modelData] || 0
                    objectName: "key" + modelData
                    x: index * keys.whiteWidth
                    width: keys.whiteWidth - 1
                    height: keys.height
                    radius: 2
                    border.color: Theme.outline
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: board.lit("#d9dce1", white.velocity) }
                        GradientStop { position: 0.85; color: board.lit("#f2f3f5", white.velocity) }
                        GradientStop { position: 1.0; color: board.lit("#b9bcc2", white.velocity) }
                    }
                    // Octave names on the C keys (middle C is C4).
                    Text {
                        visible: white.modelData % 12 === 0 && keys.whiteWidth >= 9
                        font.pixelSize: Math.max(8, Math.min(12, keys.whiteWidth * 0.4))
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 3
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "C" + (Math.floor(white.modelData / 12) - 1)
                        color: white.velocity > 0 ? "white" : "#5b6068"
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: board.engineStatus.playNote(white.modelData, true)
                        onReleased: board.engineStatus.playNote(white.modelData, false)
                        onCanceled: board.engineStatus.playNote(white.modelData, false)
                    }
                }
            }

            Repeater {
                model: board.blackNotes
                delegate: Rectangle {
                    id: black
                    required property var modelData
                    readonly property int velocity: board.velocities[modelData.note] || 0
                    objectName: "key" + modelData.note
                    x: (modelData.after + 1) * keys.whiteWidth - (keys.blackWidth / 2) - 0.5
                    width: keys.blackWidth
                    height: keys.height * 0.62
                    radius: 2
                    border.color: Theme.outline
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: board.lit("#3a3d43", black.velocity) }
                        GradientStop { position: 0.9; color: board.lit("#141518", black.velocity) }
                        GradientStop { position: 1.0; color: board.lit("#08090b", black.velocity) }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: board.engineStatus.playNote(black.modelData.note, true)
                        onReleased: board.engineStatus.playNote(black.modelData.note, false)
                        onCanceled: board.engineStatus.playNote(black.modelData.note, false)
                    }
                }
            }
        }
        }
    }
}
