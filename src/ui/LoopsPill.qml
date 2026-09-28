pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The loops at a glance while the looper strip is out of sight: "⟳ 2",
// green while loops play, dim when they are all stopped, a red dot while
// one records. It never blinks. Click it for the list of loops.
Rectangle {
    id: pill

    required property LoopController loops

    readonly property bool playing: loops.playingCount > 0

    implicitWidth: row.implicitWidth + 16
    implicitHeight: 24
    radius: height / 2
    color: playing ? "#1f3b27" : Theme.buttonBottom
    border.color: playing ? "#3fb950" : Theme.outline

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: "icons/loop.svg"
            sourceSize: Qt.size(14, 14)
            opacity: pill.playing ? 1.0 : 0.6
        }
        Text {
            objectName: "loopsPillCount"
            anchors.verticalCenter: parent.verticalCenter
            text: pill.playing ? pill.loops.playingCount : pill.loops.loopCount
            color: pill.playing ? "#9fe0a8" : Theme.textDim
            font.pixelSize: Theme.smallFontSize
            font.bold: true
        }
        Rectangle {
            objectName: "loopsPillRecording"
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.loops.recording
            width: 7
            height: 7
            radius: 3.5
            color: "#e5484d"
        }
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: list.open() }
    ToolTip.visible: hover.hovered && !list.visible
    ToolTip.text: pill.loops.playingCount === 1 ? qsTr("1 loop playing: click for the list")
                  : pill.playing ? qsTr("%1 loops playing: click for the list").arg(pill.loops.playingCount)
                               : qsTr("Loops recorded, none playing: click for the list")

    Popup {
        id: list
        objectName: "loopsList"
        y: pill.height + 4
        padding: 8
        background: Rectangle {
            color: Theme.menuBackground
            border.color: Theme.outline
            radius: Theme.radiusCard
        }
        contentItem: Column {
            spacing: 4
            Repeater {
                // By count: the loops' states change many times a second,
                // their number seldom (rows are not rebuilt under the mouse).
                model: pill.loops.loopCount
                delegate: Row {
                    id: entry
                    required property int index
                    readonly property var modelData: index < pill.loops.allLoops.length ? pill.loops.allLoops[index]
                                                                                        : ({ id: "", name: "", state: "empty", bar: 0, bars: 0 })
                    spacing: 8
                    readonly property bool sounding: ["playing", "overdubArmed", "overdubbing", "stopArmed"].indexOf(modelData.state) >= 0
                    StageButton {
                        iconSource: entry.sounding ? "icons/player-stop.svg" : "icons/player-play.svg"
                        checked: entry.sounding
                        enabled: entry.modelData.state !== "recording" && entry.modelData.state !== "armed"
                        onClicked: pill.loops.playStopLoop(entry.modelData.id)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 140
                        elide: Text.ElideRight
                        text: entry.modelData.name
                        color: Theme.text
                        font.pixelSize: Theme.fontSize
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modelData.state === "recording" ? qsTr("REC")
                              : entry.sounding && entry.modelData.bars > 0 ? entry.modelData.bar + "/" + entry.modelData.bars
                              : entry.sounding ? qsTr("playing") : qsTr("stopped")
                        color: entry.sounding ? "#3fb950" : Theme.textDim
                        font.pixelSize: Theme.smallFontSize
                    }
                }
            }
            StageButton {
                text: qsTr("Stop all")
                onClicked: {
                    pill.loops.stopAll()
                    list.close()
                }
            }
        }
    }
}
