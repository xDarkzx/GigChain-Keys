import QtQuick

// A playable keyboard: press keys with the mouse to hear the current patch
// (and to see incoming notes later). Two octaves around middle C by default.
Item {
    id: keyboard

    required property EngineStatus engineStatus
    property int firstNote: 48 // C3
    property int octaves: 3

    readonly property var whiteOffsets: [0, 2, 4, 5, 7, 9, 11]
    readonly property var blackOffsets: [1, 3, -1, 6, 8, 10, -1] // after each white key; -1 = none
    readonly property int whiteCount: octaves * 7 + 1
    readonly property real whiteWidth: width / whiteCount

    function noteName(n) {
        const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        return names[n % 12] + (Math.floor(n / 12) - 2)
    }

    Repeater {
        model: keyboard.whiteCount
        delegate: Rectangle {
            required property int index
            readonly property int note: keyboard.firstNote + Math.floor(index / 7) * 12 + keyboard.whiteOffsets[index % 7]
            x: index * keyboard.whiteWidth
            width: keyboard.whiteWidth - 1
            height: keyboard.height
            radius: 3
            color: whiteArea.pressed ? Theme.accent : Theme.keyWhite
            Text {
                visible: parent.note % 12 === 0
                text: keyboard.noteName(parent.note)
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: 4
                font.pixelSize: 10
                color: "#555"
            }
            MouseArea {
                id: whiteArea
                anchors.fill: parent
                onPressed: keyboard.engineStatus.playNote(parent.note, true)
                onReleased: keyboard.engineStatus.playNote(parent.note, false)
                onCanceled: keyboard.engineStatus.playNote(parent.note, false)
            }
        }
    }

    Repeater {
        model: keyboard.whiteCount - 1
        delegate: Rectangle {
            required property int index
            readonly property int offset: keyboard.blackOffsets[index % 7]
            readonly property int note: keyboard.firstNote + Math.floor(index / 7) * 12 + offset
            visible: offset >= 0
            x: (index + 1) * keyboard.whiteWidth - width / 2
            width: keyboard.whiteWidth * 0.6
            height: keyboard.height * 0.6
            radius: 2
            color: blackArea.pressed ? Theme.accent : Theme.keyBlack
            MouseArea {
                id: blackArea
                anchors.fill: parent
                enabled: parent.visible
                onPressed: keyboard.engineStatus.playNote(parent.note, true)
                onReleased: keyboard.engineStatus.playNote(parent.note, false)
                onCanceled: keyboard.engineStatus.playNote(parent.note, false)
            }
        }
    }
}
