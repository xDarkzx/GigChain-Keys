pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// The count-in, big over the screen (Settings: the click button's menu turns
// it off): the beat counted (1 2 3 4) large in the middle, and a light for
// each beat of the bar under it, lit up to the one counted. It shows only
// while counting in, and lets every click through.
Item {
    id: overlay
    objectName: "countInOverlay"

    property int beat: 0  // 1, 2, 3... while counting in; 0: not counting
    property int beats: 4 // in the bar

    visible: beat > 0
    implicitWidth: 260
    implicitHeight: 260

    Rectangle {
        id: disc
        anchors.centerIn: parent
        width: 220
        height: 220
        radius: width / 2
        color: Qt.alpha("#000000", 0.62)
        border.color: overlay.beat === 1 ? Theme.accent : Theme.outline
        border.width: 3

        Label {
            objectName: "countInBeat"
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -14
            text: overlay.beat
            color: overlay.beat === 1 ? Theme.accent : "white"
            font.pixelSize: 120
            font.bold: true
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 34
            spacing: 10
            Repeater {
                model: overlay.beats
                delegate: Rectangle {
                    id: dot
                    required property int index
                    width: 12
                    height: 12
                    radius: 6
                    color: dot.index < overlay.beat ? (dot.index === 0 ? Theme.accent : "white") : Qt.alpha("#ffffff", 0.18)
                }
            }
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 22
            text: qsTr("Count-in")
            color: Theme.textDim
            font.pixelSize: Theme.smallFontSize
        }
        // Each beat lands with a pulse.
        scale: 1.0
        SequentialAnimation on scale {
            id: pulse
            running: false
            NumberAnimation { to: 1.06; duration: 60; easing.type: Easing.OutQuad }
            NumberAnimation { to: 1.0; duration: 140; easing.type: Easing.InQuad }
        }
    }
    onBeatChanged: if (beat > 0) pulse.restart()
}
