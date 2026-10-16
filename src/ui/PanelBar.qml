pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GigChain.Ui

// The panel, as a Yamaha's registration and part buttons or a Nord's
// program and section buttons: a unit right across (framed as the loop
// station), and in it recessed sections
// side by side from the left: the song's SOUNDS (the one playing lit), the
// playing sound's PARTS (lit when on), and ASSIGN (tap a button, then press
// a pad, button, footswitch or key on the keyboard: it then does it).
Item {
    id: bar
    objectName: "panelBar"
    required property PanelController panel
    required property DocumentController doc
    property PluginListModel pluginModel: null // for + on PARTS: an instrument added (null: no +)
    property int renaming: -1                  // the sound whose name is being typed (-1: none)

    function renameAt(index) { renaming = index }

    readonly property bool assigning: panel.assigning
    readonly property int armedKind: panel.armed.kind
    readonly property int armedIndex: panel.armed.index
    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    function noteName(n) { return noteNames[n % 12] + (Math.floor(n / 12) - 1) }

    readonly property int buttonWidth: 132
    readonly property int buttonHeight: 46

    implicitHeight: column.implicitHeight + 10 + card.anchors.topMargin + card.anchors.bottomMargin

    // The unit, inset as the loop station below it is (in line with it).
    Rectangle {
        id: card
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        radius: Theme.radiusCard
        border.color: Theme.outline
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.panelTop }
            GradientStop { position: 1.0; color: Theme.panelBottom }
        }
        Rectangle { x: 2; y: 1; width: parent.width - 4; height: 1; color: Theme.bevelLight }
    }

    // One panel button: a hardware key with a light strip along its top
    // (blue: the sound playing; green: a part on), its name, and the pad
    // that does it under the name.
    component PanelButton: Rectangle {
        id: button
        property string label
        property string pad      // "PAD C1", or ""
        property string assigned // its full description, for the tooltip
        property bool lit
        property color light: Theme.ledBlue
        property bool armed: false
        property bool assignable: true
        property bool editing: false     // its name being typed (a sound's)
        property bool isSound: false     // a sound: rename, duplicate, delete in its menu
        signal pressed()
        signal forget()
        signal renameRequested()
        signal renamed(string name)
        signal duplicateRequested()
        signal deleteRequested()

        implicitWidth: bar.buttonWidth
        implicitHeight: bar.buttonHeight
        radius: Theme.radius
        border.color: Theme.outline
        gradient: Gradient {
            GradientStop { position: 0; color: mouse.pressed ? Theme.buttonBottom : Theme.buttonTop }
            GradientStop { position: 1; color: Theme.buttonBottom }
        }
        opacity: bar.assigning && !assignable ? 0.35 : 1
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelLight }

        // The light strip.
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 5 }
            height: 3
            radius: 1.5
            color: button.lit ? button.light : Theme.ledOff
        }
        Text {
            anchors { left: parent.left; right: parent.right; leftMargin: 8; rightMargin: 8 }
            y: button.pad !== "" || button.armed || (bar.assigning && button.assignable) ? 12 : (parent.height - height) / 2 + 2
            horizontalAlignment: Text.AlignHCenter
            text: button.label
            elide: Text.ElideRight
            font.pixelSize: Theme.smallFontSize + 1
            font.bold: true
            color: button.lit ? Theme.text : Theme.textDim
        }
        Text {
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 5 }
            width: parent.width - 12
            horizontalAlignment: Text.AlignHCenter
            visible: text !== ""
            text: button.armed ? qsTr("PRESS A PAD") : (button.pad !== "" ? button.pad : (bar.assigning && button.assignable ? qsTr("TAP TO ASSIGN") : ""))
            elide: Text.ElideRight
            font.pixelSize: Theme.tinyFontSize - 1
            font.bold: true
            font.letterSpacing: 1
            color: button.armed ? Theme.ledAmber : Theme.engraved
        }
        // Armed: an amber outline, pulsing, waiting for the pad.
        Rectangle {
            anchors.fill: parent
            visible: button.armed
            radius: parent.radius
            color: "transparent"
            border.width: 2
            border.color: Theme.ledAmber
            SequentialAnimation on opacity {
                running: button.armed
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.3; duration: 450 }
                NumberAnimation { from: 0.3; to: 1; duration: 450 }
            }
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor
            onClicked: (event) => {
                if (event.button === Qt.RightButton) {
                    if (button.isSound || button.assigned !== "") buttonMenu.popup()
                    return
                }
                button.pressed()
            }
            onDoubleClicked: (event) => { if (event.button === Qt.LeftButton && button.isSound && !bar.assigning) button.renameRequested() }
        }
        StageMenu {
            id: buttonMenu
            StageMenuItem { text: qsTr("Rename…"); visible: button.isSound; height: visible ? implicitHeight : 0; onTriggered: button.renameRequested() }
            StageMenuItem { text: qsTr("Duplicate"); visible: button.isSound; height: visible ? implicitHeight : 0; onTriggered: button.duplicateRequested() }
            StageMenuItem { text: qsTr("Delete"); visible: button.isSound; height: visible ? implicitHeight : 0; onTriggered: button.deleteRequested() }
            StageMenuItem {
                text: qsTr("Forget %1").arg(button.pad)
                visible: button.assigned !== ""
                height: visible ? implicitHeight : 0
                onTriggered: button.forget()
            }
        }
        // Its name typed in place (Enter keeps it, Escape leaves it as it was).
        TextField {
            id: nameField
            anchors.fill: parent
            anchors.margins: 3
            visible: button.editing
            text: button.label
            horizontalAlignment: TextInput.AlignHCenter
            font.pixelSize: Theme.smallFontSize + 1
            font.bold: true
            selectByMouse: true
            onVisibleChanged: if (visible) { text = button.label; selectAll(); forceActiveFocus() }
            onAccepted: button.renamed(text)
            Keys.onEscapePressed: button.renamed(button.label)
            onActiveFocusChanged: if (!activeFocus && visible) button.renamed(text)
        }
        HoverHandler { id: hover }
        ToolTip.visible: hover.hovered && button.assigned !== "" && !bar.assigning
        ToolTip.text: qsTr("On your keyboard: %1. Right-click to forget it.").arg(button.assigned)
    }

    // + at the end of a section: a key of the same height, its sign engraved.
    component AddButton: Rectangle {
        id: add
        property string tip
        signal clicked()
        implicitWidth: 40
        implicitHeight: bar.buttonHeight
        visible: !bar.assigning
        radius: Theme.radius
        color: addMouse.containsMouse ? Theme.buttonTop : "transparent"
        border.color: Theme.metalDark
        Text {
            anchors.centerIn: parent
            text: "+"
            font.pixelSize: Theme.titleFontSize + 4
            color: addMouse.containsMouse ? Theme.text : Theme.engraved
        }
        MouseArea {
            id: addMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: add.clicked()
        }
        ToolTip.visible: addMouse.containsMouse
        ToolTip.text: add.tip
    }

    ColumnLayout {
        id: column
        anchors { left: card.left; right: card.right; top: card.top }
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
        anchors.topMargin: 5
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            // The sections, side by side from the left (scrolling if a song has many sounds).
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: sections.implicitHeight + 2
                contentWidth: sections.implicitWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentWidth > width
                Row {
                    id: sections
                    spacing: Theme.spacing

                    StripSection {
                        label: qsTr("SOUNDS")
                        Row {
                            spacing: 4
                            Repeater {
                                model: bar.panel.sounds
                                delegate: PanelButton {
                                    required property var modelData
                                    required property int index
                                    objectName: "panelSound" + index
                                    isSound: true
                                    label: modelData.name
                                    lit: modelData.active
                                    pad: modelData.pad
                                    assigned: modelData.assigned
                                    assignable: modelData.assignable
                                    editing: bar.renaming === index
                                    armed: bar.assigning && bar.armedKind === bar.panel.soundButton && bar.armedIndex === index
                                    onPressed: {
                                        if (bar.assigning) { if (assignable) bar.panel.arm(bar.panel.soundButton, index) }
                                        else bar.panel.pickSound(index)
                                    }
                                    onForget: bar.panel.clearAssignment(bar.panel.soundButton, index)
                                    onRenameRequested: bar.renameAt(index)
                                    onRenamed: (name) => {
                                        bar.renaming = -1
                                        if (name !== label) bar.panel.renameSound(index, name)
                                    }
                                    onDuplicateRequested: bar.panel.duplicateSound(index)
                                    onDeleteRequested: bar.panel.deleteSound(index)
                                }
                            }
                            // + a sound: a copy of the one playing (to change), or an empty one.
                            AddButton {
                                objectName: "panelAddSound"
                                tip: qsTr("Add a sound to this song")
                                onClicked: addSoundMenu.popup(this, 0, height)
                                StageMenu {
                                    id: addSoundMenu
                                    readonly property string playing: bar.panel.sounds.find(s => s.active)?.name ?? ""
                                    StageMenuItem {
                                        objectName: "panelAddSoundCopy"
                                        text: qsTr("Copy of “%1”").arg(addSoundMenu.playing)
                                        visible: addSoundMenu.playing !== ""
                                        height: visible ? implicitHeight : 0
                                        onTriggered: { const at = bar.panel.addSound(true); if (at >= 0) bar.renameAt(at) }
                                    }
                                    StageMenuItem {
                                        objectName: "panelAddSoundEmpty"
                                        text: qsTr("Empty sound")
                                        onTriggered: { const at = bar.panel.addSound(false); if (at >= 0) bar.renameAt(at) }
                                    }
                                }
                            }
                        }
                    }

                    StripSection {
                        label: qsTr("PARTS")
                        Row {
                            spacing: 4
                            Repeater {
                                model: bar.panel.parts
                                delegate: PanelButton {
                                    required property var modelData
                                    required property int index
                                    objectName: "panelPart" + index
                                    light: Theme.ledGreen
                                    label: modelData.keyLow > 0 || modelData.keyHigh < 127
                                           ? modelData.name + " " + bar.noteName(modelData.keyLow) + "–" + bar.noteName(modelData.keyHigh)
                                           : modelData.name
                                    lit: modelData.on
                                    pad: modelData.pad
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
                            // + a part: an instrument added to this sound (as the mixer's + Instrument).
                            AddButton {
                                objectName: "panelAddPart"
                                visible: bar.pluginModel !== null
                                tip: qsTr("Add an instrument to this sound")
                                onClicked: {
                                    partPicker.shared = bar.doc.otherSongsInstruments()
                                    partPicker.popup(this, 0, height)
                                }
                                InstrumentPickerMenu {
                                    id: partPicker
                                    pluginModel: bar.pluginModel
                                    onPicked: (pluginId, name) => bar.doc.addChannel(pluginId, name)
                                    onPickedShared: (song, patch, channel) => bar.doc.addSharedChannel(song, patch, channel)
                                }
                            }
                        }
                    }
                }
            }

            // ASSIGN: like a Yamaha's Memory or a Nord's Shift.
            StripSection {
                label: qsTr("ASSIGN")
                Layout.alignment: Qt.AlignTop
                StageButton {
                    objectName: "panelAssign"
                    implicitWidth: 96
                    implicitHeight: bar.buttonHeight
                    text: bar.assigning ? qsTr("Done") : qsTr("Pads…")
                    checkable: true
                    checked: bar.assigning
                    highlighted: bar.assigning
                    tip: qsTr("Put your sounds and parts on your keyboard's pads, buttons or keys: tap a button here, then press the pad that should do it")
                    onClicked: bar.panel.assigning = !bar.panel.assigning
                }
            }
        }

        // While assigning: what to do now.
        Text {
            objectName: "panelHint"
            visible: bar.assigning
            Layout.fillWidth: true
            Layout.bottomMargin: 2
            horizontalAlignment: Text.AlignHCenter
            text: bar.armedIndex >= 0
                  ? qsTr("Now press the pad, button, footswitch or key on your keyboard that should do it.")
                  : qsTr("Tap a sound or a part above, then press the pad on your keyboard for it. Done when finished.")
            color: Theme.ledAmber
            font.pixelSize: Theme.smallFontSize
            wrapMode: Text.Wrap
        }
    }
}
