pragma ComponentBehavior: Bound

import QtQuick

// Pads Pro's window (BuiltinPanel: its top bar, its values), as Saw Stack
// Pro's and Piano Pro's. On the left the eight pads as tiles (each named for
// what it is: a click plays that one), on the right the Tone and Shimmer pad
// (drag the dot: darker or brighter across, more shimmer up). Under them the
// sound (movement, bloom, tail, width, touch) and the space (reverb, tone,
// shimmer as knobs, for MIDI Learn above).
BuiltinPanel {
    id: panel

    namePrefix: "padsPro"
    logoText: "PADS"
    masterId: macro("Level")
    masterKind: "paddb"
    categories: [["Warm", "ANALOG"], ["Lush", "ANALOG"], ["Dark", "ANALOG"], ["Air", "AMBIENT"], ["Shimmer", "AMBIENT"],
                 ["Choir", "VOICE"], ["Strings", "ENSEMBLE"]]
    otherCategory: "ORGAN"

    // What each pad is, under its name (as the research notes have it).
    readonly property var about: [
        qsTr("Juno: saw, breathing pulse, sub, chorus"),
        qsTr("Jupiter-8 and Prophet: detuned, wide"),
        qsTr("OB-Xa, filter closed: round and deep"),
        qsTr("Breath and open air, a little shimmer"),
        qsTr("Warm pad, climbing an octave up"),
        qsTr("CS-80 choir: an \"aah\" and vibrato"),
        qsTr("String machine: saws and ensemble"),
        qsTr("Hymn organ held as a pad")
    ]
    readonly property var tileColours: ["#ffb547", "#4cc3ff", "#4f7dff", "#7be0d6", "#b28cff", "#ff7a9c", "#7be07a", "#c6e05a"]

    // ================= The pads.
    BuiltinCard {
        id: padsCard
        owner: panel
        title: qsTr("PADS")
        x: 14
        y: panel.contentTop
        width: 640
        height: 300
        headerRight: Text {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            text: qsTr("Click a pad to play it")
            color: panel.muted
            font.family: Theme.fontFamily
            font.pixelSize: 9
        }
        Grid {
            anchors { fill: parent; topMargin: -2 }
            columns: 4
            spacing: 8
            Repeater {
                model: panel.typeNames
                delegate: Rectangle {
                    id: tile
                    required property string modelData
                    required property int index
                    readonly property bool chosen: panel.typeIndex === index
                    readonly property color hue: panel.tileColours[index % panel.tileColours.length]
                    objectName: "padsProTile_" + index
                    width: (padsCard.width - 28 - 3 * 8) / 4
                    height: 112
                    radius: 10
                    color: chosen ? Qt.rgba(hue.r, hue.g, hue.b, 0.16) : (tileArea.containsMouse ? "#1d212a" : panel.well)
                    border.color: chosen ? hue : panel.cardEdge
                    border.width: chosen ? 2 : 1
                    // A slow glow while it is the one playing.
                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 10 }
                        height: 3
                        radius: 1.5
                        color: tile.hue
                        opacity: tile.chosen ? 1 : 0.35
                        SequentialAnimation on opacity {
                            running: tile.chosen && panel.visible
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.45; duration: 1800; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1.0; duration: 1800; easing.type: Easing.InOutSine }
                        }
                    }
                    Column {
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
                        spacing: 6
                        Text {
                            text: panel.category(tile.modelData)
                            color: tile.hue
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 1.2
                        }
                        Text {
                            text: tile.modelData
                            color: panel.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            text: panel.about[tile.index] || ""
                            color: panel.muted
                            font.family: Theme.fontFamily
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        id: tileArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.chooseType(tile.index)
                    }
                }
            }
        }
    }

    // ================= Tone and Shimmer, on one pad.
    BuiltinCard {
        id: xyCard
        owner: panel
        title: qsTr("TONE · SHIMMER")
        x: padsCard.x + padsCard.width + 12
        y: padsCard.y
        width: panel.frameWidth - 14 - x
        height: padsCard.height
        PadXY {
            objectName: "padsProXY"
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: -2 }
            width: Math.min(parent.width - 8, parent.height + 2)
            height: width
            colour: panel.fxColour
            well: panel.well
            edge: panel.cardEdge
            muted: panel.muted
            xLabel: qsTr("Tone")
            yLabel: qsTr("Shimmer")
            xValue: Math.max(0, panel.value(panel.macro("Tone")))
            yValue: Math.max(0, panel.value(panel.macro("Shimmer")))
            onMoved: (x, y) => {
                panel.set(panel.macro("Tone"), x)
                panel.set(panel.macro("Shimmer"), y)
            }
        }
    }

    // ================= The sound and the space.
    BuiltinCard {
        id: soundCard
        owner: panel
        title: qsTr("SOUND")
        x: padsCard.x
        y: padsCard.y + padsCard.height + 12
        width: padsCard.width
        height: panel.frameHeight - 14 - y
        Row {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 2
            spacing: 26
            BuiltinKnob { objectName: "padsProKnob_Motion"; owner: panel; pid: panel.macro("Motion"); text: "MOTION"; size: 54 }
            BuiltinKnob { objectName: "padsProKnob_Attack"; owner: panel; pid: panel.macro("Attack"); text: "BLOOM"; kind: "padtime"; size: 54 }
            BuiltinKnob { objectName: "padsProKnob_Release"; owner: panel; pid: panel.macro("Release"); text: "TAIL"; kind: "padtime"; size: 54 }
            BuiltinKnob { objectName: "padsProKnob_Width"; owner: panel; pid: panel.macro("Width"); text: "WIDTH"; size: 54 }
            BuiltinKnob { objectName: "padsProKnob_Velocity"; owner: panel; pid: panel.macro("Velocity"); text: "TOUCH"; size: 54 }
        }
    }
    BuiltinCard {
        owner: panel
        title: qsTr("SPACE")
        colour: panel.fxColour
        x: xyCard.x
        y: soundCard.y
        width: xyCard.width
        height: soundCard.height
        Row {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 2
            spacing: 22
            BuiltinKnob { objectName: "padsProKnob_Reverb"; owner: panel; pid: panel.macro("Reverb"); text: "REVERB"; colour: panel.fxColour; size: 54 }
            BuiltinKnob { objectName: "padsProKnob_Tone"; owner: panel; pid: panel.macro("Tone"); text: "TONE"; colour: panel.fxColour; size: 54 }
            BuiltinKnob { objectName: "padsProKnob_Shimmer"; owner: panel; pid: panel.macro("Shimmer"); text: "SHIMMER"; colour: panel.fxColour; size: 54 }
        }
    }
}
