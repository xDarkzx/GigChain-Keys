pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Enhance, beside the OTT in the app's own instruments' effects: a round
// button showing its setting (A-D) and lit when on. A click switches it on
// and opens its dial (Soundgoodizer's face: the four settings and one big
// knob, with its light to switch it off again). Its parameters are found by
// name ("Enhance", "Enhance Mode", "Enhance On").
Item {
    id: enhance

    required property var owner // the instrument's window (BuiltinPanel)
    property int size: 46
    readonly property string prefix: owner.namePrefix
    readonly property int onId: owner.macro("Enhance On")
    readonly property bool lit: owner.value(onId) >= 0.5
    readonly property int mode: Math.max(0, owner.choiceOf("Enhance Mode"))
    readonly property var modeNames: owner.choicesOf("Enhance Mode")
    readonly property color colour: owner.fxColour
    implicitWidth: Math.max(size + 12, 56)
    implicitHeight: size + 22

    Rectangle {
        id: face
        objectName: enhance.prefix + "Enhance"
        readonly property bool lit: enhance.lit
        width: enhance.size
        height: enhance.size
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        color: faceArea.containsMouse ? "#232832" : "#1c2028"
        border.width: 4
        border.color: enhance.lit ? enhance.colour : enhance.owner.track
        Behavior on border.color { ColorAnimation { duration: 120 } }
        Text {
            anchors.centerIn: parent
            text: enhance.modeNames[enhance.mode] || ""
            color: enhance.lit ? enhance.owner.text : enhance.owner.muted
            font.family: Theme.fontFamily
            font.pixelSize: enhance.size * 0.36
            font.weight: Font.Black
        }
        MouseArea {
            id: faceArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (!enhance.lit) enhance.owner.set(enhance.onId, 1)
                dial.open()
            }
        }
    }
    Text {
        anchors { top: face.bottom; topMargin: 5; horizontalCenter: parent.horizontalCenter }
        text: "ENHANCE"
        color: faceArea.containsMouse ? enhance.colour : enhance.owner.muted
        font.family: Theme.fontFamily
        font.pixelSize: 10
        font.weight: Font.DemiBold
        font.letterSpacing: 0.8
    }

    // The dial: its light, the four settings, the amount.
    Popup {
        id: dial
        objectName: enhance.prefix + "EnhanceDial"
        x: (enhance.width - width) / 2
        y: -height - 8
        width: 236
        padding: 14
        background: Rectangle { color: "#1a1e26"; border.color: enhance.owner.cardEdge; radius: 12 }
        contentItem: Column {
            spacing: 12
            Item {
                id: header
                objectName: enhance.prefix + "EnhanceOn"
                readonly property bool lit: enhance.lit
                width: parent.width
                height: 20
                Rectangle {
                    id: light
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    width: 12; height: 12; radius: 6
                    color: enhance.lit ? enhance.colour : "#3a404c"
                    border.color: enhance.lit ? Qt.lighter(enhance.colour, 1.3) : "transparent"
                }
                Text {
                    anchors { left: light.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    text: qsTr("ENHANCE")
                    color: enhance.lit ? enhance.owner.text : enhance.owner.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                }
                Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: enhance.lit ? qsTr("Click the light: off") : qsTr("Click the light: on")
                    color: enhance.owner.muted
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: enhance.owner.set(enhance.onId, enhance.lit ? 0 : 1)
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 8
                opacity: enhance.lit ? 1 : 0.35
                Repeater {
                    model: enhance.modeNames
                    delegate: BuiltinPill {
                        required property string modelData
                        required property int index
                        objectName: enhance.prefix + "EnhanceMode_" + index
                        owner: enhance.owner
                        colour: enhance.colour
                        text: modelData
                        implicitWidth: 40
                        implicitHeight: 28
                        selected: enhance.mode === index
                        onActivated: enhance.owner.choose("Enhance Mode", index)
                    }
                }
            }
            BuiltinKnob {
                objectName: enhance.prefix + "Knob_Enhance"
                anchors.horizontalCenter: parent.horizontalCenter
                opacity: enhance.lit ? 1 : 0.35
                owner: enhance.owner
                pid: enhance.owner.macro("Enhance")
                text: "AMOUNT"
                colour: enhance.colour
                size: 80
            }
        }
    }
}
