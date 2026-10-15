pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GigChain.Ui

// The panel bar, as a Yamaha's registration and part buttons or a Nord's
// program and section buttons: the song's sounds (the one playing lit),
// the playing sound's parts (lit when on), and ASSIGN: tap a button, then
// press a pad, button, footswitch or key on the keyboard; it then does it.
Rectangle {
    id: bar
    objectName: "panelBar"
    required property PanelController panel
    required property DocumentController doc

    readonly property bool assigning: panel.assigning
    readonly property int armedKind: panel.armed.kind
    readonly property int armedIndex: panel.armed.index
    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    function noteName(n) { return noteNames[n % 12] + (Math.floor(n / 12) - 1) }

    implicitHeight: column.implicitHeight + 12
    gradient: Gradient {
        GradientStop { position: 0; color: Theme.panelTop }
        GradientStop { position: 1; color: Theme.panelBottom }
    }
    Rectangle { anchors { left: parent.left; right: parent.right; top: parent.top } height: 1; color: Theme.metalShine }

    // One panel button: a name, a light, and what on the keyboard does it.
    component PanelButton: Rectangle {
        id: button
        property string label
        property string assigned
        property bool lit
        property bool led: false // a part: an LED (on / off) rather than a lit face
        property bool armed: false
        property bool assignable: true
        signal pressed()
        signal forget()

        implicitWidth: Math.max(96, nameText.implicitWidth + (led ? 34 : 22))
        implicitHeight: 46
        radius: 5
        border.width: 1
        border.color: bar.assigning && assignable ? Theme.metalMid : Theme.wellBorder
        gradient: Gradient {
            GradientStop { position: 0; color: button.lit && !button.led ? Theme.accentTop : Theme.buttonTop }
            GradientStop { position: 1; color: button.lit && !button.led ? Theme.accentBottom : Theme.buttonBottom }
        }
        opacity: bar.assigning && !assignable ? 0.4 : 1
        Rectangle { // armed: an amber outline, pulsing, waiting for the pad
            anchors.fill: parent
            visible: button.armed
            radius: parent.radius
            color: "transparent"
            border.width: 2
            border.color: Theme.ledAmber
            SequentialAnimation on opacity {
                running: button.armed
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.25; duration: 450 }
                NumberAnimation { from: 0.25; to: 1; duration: 450 }
            }
        }

        Rectangle { // a part's LED
            visible: button.led
            width: 9; height: 9; radius: 4.5
            anchors { left: parent.left; leftMargin: 10; top: parent.top; topMargin: 11 }
            color: button.lit ? Theme.ledGreen : Theme.ledOff
            border.color: Theme.wellBorder
        }
        Text {
            id: nameText
            anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 7 }
            anchors.leftMargin: button.led ? 24 : 10
            anchors.rightMargin: 10
            text: button.label
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSize
            font.bold: button.lit
            color: button.lit && !button.led ? Theme.accentText : Theme.text
        }
        Text {
            anchors { left: nameText.left; right: parent.right; rightMargin: 8; bottom: parent.bottom; bottomMargin: 5 }
            text: button.armed ? qsTr("Press a pad…") : (button.assigned !== "" ? button.assigned : (bar.assigning && button.assignable ? qsTr("Tap to assign") : ""))
            elide: Text.ElideRight
            font.pixelSize: Theme.tinyFontSize
            color: button.armed ? Theme.ledAmber : Theme.textDim
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    if (button.assigned !== "") forgetMenu.popup()
                    return
                }
                button.pressed()
            }
        }
        StageMenu {
            id: forgetMenu
            StageMenuItem { text: qsTr("Forget %1").arg(button.assigned); onTriggered: button.forget() }
        }
        ToolTip.visible: hover.hovered && button.assigned !== "" && !bar.assigning
        ToolTip.text: qsTr("On your keyboard: %1 (right-click to forget it)").arg(button.assigned)
        HoverHandler { id: hover }
    }

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 6 }
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            // ASSIGN: like a Yamaha's Memory or a Nord's Shift.
            StageButton {
                objectName: "panelAssign"
                text: bar.assigning ? qsTr("Done") : qsTr("Assign")
                checkable: true
                checked: bar.assigning
                highlighted: bar.assigning
                tip: qsTr("Assign your keyboard's pads, buttons or keys: tap a sound or a part, then press the pad that should do it")
                onClicked: bar.panel.assigning = !bar.panel.assigning
            }

            Label { text: qsTr("SOUNDS"); color: Theme.textDim; font.pixelSize: Theme.tinyFontSize; font.bold: true; Layout.leftMargin: 6 }
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                contentWidth: soundsRow.implicitWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: soundsRow
                    spacing: 6
                    Repeater {
                        model: bar.panel.sounds
                        delegate: PanelButton {
                            required property var modelData
                            required property int index
                            objectName: "panelSound" + index
                            label: modelData.name
                            lit: modelData.active
                            assigned: modelData.assigned
                            assignable: modelData.assignable
                            armed: bar.assigning && bar.armedKind === bar.panel.soundButton && bar.armedIndex === index
                            onPressed: {
                                if (bar.assigning) { if (assignable) bar.panel.arm(bar.panel.soundButton, index) }
                                else bar.panel.pickSound(index)
                            }
                            onForget: bar.panel.clearAssignment(bar.panel.soundButton, index)
                        }
                    }
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 40; color: Theme.wellBorder }

            Label { text: qsTr("PARTS"); color: Theme.textDim; font.pixelSize: Theme.tinyFontSize; font.bold: true }
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                contentWidth: partsRow.implicitWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Row {
                    id: partsRow
                    spacing: 6
                    Repeater {
                        model: bar.panel.parts
                        delegate: PanelButton {
                            required property var modelData
                            required property int index
                            objectName: "panelPart" + index
                            led: true
                            label: modelData.keyLow > 0 || modelData.keyHigh < 127
                                   ? modelData.name + "  " + bar.noteName(modelData.keyLow) + "–" + bar.noteName(modelData.keyHigh)
                                   : modelData.name
                            lit: modelData.on
                            assigned: modelData.assigned
                            assignable: modelData.assignable
                            armed: bar.assigning && bar.armedKind === bar.panel.partButton && bar.armedIndex === index
                            onPressed: {
                                if (bar.assigning) { if (assignable) bar.panel.arm(bar.panel.partButton, index) }
                                else bar.panel.togglePart(index)
                            }
                            onForget: bar.panel.clearAssignment(bar.panel.partButton, index)
                        }
                    }
                }
            }
        }

        Label {
            objectName: "panelHint"
            visible: bar.assigning
            Layout.fillWidth: true
            text: bar.armedIndex >= 0
                  ? qsTr("Now press the pad, button, footswitch or key on your keyboard that should do it.")
                  : qsTr("Tap a sound or a part, then press the pad on your keyboard that should do it. A sound's pad picks it in every song; a part's pad turns it on and off. Done when finished.")
            color: Theme.ledAmber
            font.pixelSize: Theme.smallFontSize
            wrapMode: Text.Wrap
        }
    }
}
