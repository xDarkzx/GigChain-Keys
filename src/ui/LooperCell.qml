pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// One channel's loop, above its mixer strip: ● Record and ⟳ Loop, lit by
// what the loop is doing, a ring round ⟳ filling as the loop plays, and its
// bar count. Right-click (or hold) ⟳ for Undo layer and Clear.
Rectangle {
    id: cell

    // From LoopController.channelLoops: {state, progress, bar, bars, layers}.
    property var loop: ({ state: "empty", progress: 0, bar: 0, bars: 0, layers: 0 })
    property bool selected: false // the channel the keyboard's looper buttons act on

    signal recordPressed()
    signal playStopPressed()
    signal undoRequested()
    signal clearRequested()

    readonly property string loopState: loop ? loop.state : "empty"
    readonly property int layers: loop ? loop.layers : 0
    readonly property int bar: loop ? loop.bar : 0
    readonly property int bars: loop ? loop.bars : 0
    readonly property bool recordingBase: loopState === "recording" || loopState === "closing"
    readonly property bool layering: loopState === "overdubbing"
    readonly property bool waiting: loopState === "armed" || loopState === "overdubArmed" || loopState === "startArmed" || loopState === "stopArmed"
    readonly property bool sounding: loopState === "playing" || loopState === "overdubArmed" || loopState === "overdubbing" || loopState === "stopArmed"
    readonly property bool hasLoop: sounding || loopState === "stopped" || loopState === "startArmed"
    readonly property color recordRed: "#e5484d"
    readonly property color layerOrange: "#f0913a"
    readonly property color playGreen: "#3fb950"

    width: Theme.stripWidth
    height: 58
    radius: Theme.radiusCard
    color: Theme.readoutBackground
    border.color: selected ? Theme.accent : Theme.outline
    border.width: selected ? 2 : 1

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 6
        spacing: 10

        // ● Record: a red ring while it waits for the bar, red while
        // recording, orange for a layer.
        Rectangle {
            id: recordButton
            objectName: "loopRecord"
            width: 30
            height: 30
            radius: 15
            readonly property color lit: cell.loopState === "overdubArmed" || cell.layering ? cell.layerOrange : cell.recordRed
            color: cell.recordingBase || cell.layering ? lit : (recordArea.pressed ? Theme.buttonDownTop : Theme.buttonBottom)
            border.width: cell.loopState === "armed" || cell.loopState === "overdubArmed" ? 2 : 1
            border.color: cell.loopState === "armed" || cell.loopState === "overdubArmed" ? lit : Theme.outline
            Rectangle {
                anchors.centerIn: parent
                width: 12
                height: 12
                radius: 6
                color: cell.recordingBase || cell.layering ? "white" : recordButton.lit
                opacity: recordArea.containsMouse || cell.recordingBase || cell.layering || cell.waiting ? 1.0 : 0.75
            }
            MouseArea {
                id: recordArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: cell.recordPressed()
            }
            ToolTip.visible: recordArea.containsMouse
            ToolTip.text: cell.hasLoop ? qsTr("Record a layer on top (again: end it)")
                                       : cell.recordingBase ? qsTr("Close the loop") : qsTr("Record a loop of this channel")
        }

        // ⟳ Loop: grey when empty, dim when stopped, green while playing,
        // with the ring showing where the loop is.
        Item {
            id: loopButton
            objectName: "loopPlay"
            width: 30
            height: 30
            Rectangle {
                anchors.fill: parent
                radius: 15
                color: cell.sounding ? Qt.darker(cell.playGreen, 1.6) : (loopArea.pressed ? Theme.buttonDownTop : Theme.buttonBottom)
                border.width: cell.loopState === "startArmed" || cell.loopState === "stopArmed" ? 2 : 1
                border.color: cell.loopState === "startArmed" || cell.loopState === "stopArmed" ? cell.playGreen : Theme.outline
            }
            Canvas {
                id: ring
                objectName: "loopRing"
                anchors.fill: parent
                visible: cell.sounding
                readonly property real progress: cell.loop ? cell.loop.progress : 0
                onProgressChanged: requestPaint()
                onVisibleChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.lineWidth = 3
                    ctx.lineCap = "round"
                    ctx.strokeStyle = cell.playGreen
                    ctx.beginPath()
                    ctx.arc(width / 2, height / 2, width / 2 - 2, -Math.PI / 2, -Math.PI / 2 + Math.max(0.02, progress) * 2 * Math.PI)
                    ctx.stroke()
                }
            }
            Image {
                anchors.centerIn: parent
                source: "icons/loop.svg"
                sourceSize: Qt.size(18, 18)
                opacity: cell.sounding ? 1.0 : cell.hasLoop ? 0.7 : 0.3
            }
            MouseArea {
                id: loopArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                pressAndHoldInterval: 600
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) loopMenu.popup()
                    else cell.playStopPressed()
                }
                onPressAndHold: loopMenu.popup()
            }
            ToolTip.visible: loopArea.containsMouse && !loopMenu.visible
            ToolTip.text: cell.hasLoop ? qsTr("Play / stop the loop (right-click: undo a layer, clear)") : qsTr("No loop yet: record one")
            StageMenu {
                id: loopMenu
                objectName: "loopMenu"
                StageMenuItem {
                    text: qsTr("Undo last layer")
                    enabled: cell.layers > 0 || cell.layering
                    onTriggered: cell.undoRequested()
                }
                StageMenuItem {
                    objectName: "loopClear"
                    text: qsTr("Clear this loop")
                    enabled: cell.loopState !== "empty"
                    onTriggered: cell.clearRequested()
                }
            }
        }
    }

    // What it is doing, in a word: its bar while it plays.
    Text {
        objectName: "loopStatus"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        text: cell.loopState === "armed" ? qsTr("next bar")
              : cell.recordingBase ? qsTr("REC")
              : cell.layering ? qsTr("LAYER %1").arg(cell.layers + 1)
              : cell.sounding && cell.bars > 0 ? cell.bar + "/" + cell.bars
              : cell.sounding ? qsTr("playing")
              : cell.hasLoop ? qsTr("stopped")
              : ""
        color: cell.recordingBase ? cell.recordRed : cell.layering ? cell.layerOrange : cell.sounding ? cell.playGreen : Theme.textDim
        font.pixelSize: Theme.tinyFontSize
        font.bold: true
    }
}
