pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GigChain.Ui

// Sustain Pads, as Sunday Keys' Tonic: one unit right across (framed as the
// panel bar), its sections side by side: ACTIVATE (lit while it sounds), the
// PAD (the pad's name on a display, and the key), the twelve KEYS (tap one:
// the pad fades over to it; tap it again: it fades out), FILTER · SHIMMER on
// one square, MOTION, FADE and OCTAVE, and the VOLUME with its meter. The
// same unit in the main window and in its own window (`roomy`: bigger).
Item {
    id: unit
    objectName: "sustainPadsBar"

    required property SustainPadsController pads
    property SettingsController settings: null // its outputs (null: the mix only)
    property bool roomy: false                  // in its own window: bigger keys and pad
    property PluginListModel pluginModel: null  // for loading a VST instrument (null: Pads Pro only)
    // A VST instrument plays the pad: its patch, filter and movement are its own (in its window).
    readonly property bool vst: pads.instrumentId !== ""
    function openInstrumentWindow() { unit.pads.openInstrumentWindow(unit.Window.window) }
    signal detachRequested()
    signal attachRequested()
    property bool inWindow: false

    readonly property int keyWidth: roomy ? 58 : 44
    readonly property int keyHeight: roomy ? 52 : 38
    readonly property int padSize: roomy ? 150 : 2 * keyHeight + 4

    implicitHeight: row.implicitHeight + 16
    implicitWidth: row.implicitWidth + 2 * Theme.spacing + 8

    // The unit.
    Rectangle {
        id: card
        anchors.fill: parent
        anchors.leftMargin: unit.inWindow ? 0 : Theme.spacing
        anchors.rightMargin: unit.inWindow ? 0 : Theme.spacing
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

    // A key of the twelve: a hardware key with a light strip along its top
    // (green: playing; dim amber: picked but off), its name, and a dot under
    // it on the song's own key.
    component PadKey: Rectangle {
        id: key
        required property int keyIndex
        readonly property bool picked: unit.pads.key === keyIndex
        readonly property bool playing: picked && unit.pads.active
        readonly property bool songs: unit.pads.songKeyIndex === keyIndex
        objectName: "sustainKey" + keyIndex
        implicitWidth: unit.keyWidth
        implicitHeight: unit.keyHeight
        radius: Theme.radius
        border.color: playing ? Theme.ledGreen : Theme.outline
        gradient: Gradient {
            GradientStop { position: 0; color: keyMouse.pressed ? Theme.buttonBottom : Theme.buttonTop }
            GradientStop { position: 1; color: Theme.buttonBottom }
        }
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelLight }
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 5 }
            height: 3
            radius: 1.5
            color: key.playing ? Theme.ledGreen : key.picked ? Qt.darker(Theme.ledAmber, 1.8) : Theme.ledOff
        }
        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 2
            text: unit.pads.keyNames[key.keyIndex]
            font.pixelSize: unit.roomy ? Theme.fontSize + 3 : Theme.fontSize
            font.bold: true
            color: key.playing ? Theme.text : Theme.textDim
        }
        Rectangle {
            visible: key.songs
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 4 }
            width: 4
            height: 4
            radius: 2
            color: Theme.lcdAccent
        }
        MouseArea {
            id: keyMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: unit.pads.playKey(key.keyIndex)
        }
        ToolTip.visible: keyMouse.containsMouse
        ToolTip.delay: 600
        ToolTip.text: key.playing ? qsTr("Playing in %1: tap to fade it out").arg(unit.pads.keyNames[key.keyIndex])
                                  : qsTr("Play the pad in %1").arg(unit.pads.keyNames[key.keyIndex])
                                    + (key.songs ? qsTr(" (the song's key)") : "")
    }

    // A knob: drag up or right, scroll; double-click: back to where it starts.
    component PadKnob: Item {
        id: knob
        property real position: 0 // 0-1
        property real home: 0.5
        property string label
        property string readout
        property color ledColor: Theme.ledBlue
        signal turned(real position)
        implicitWidth: 58
        implicitHeight: face.height + caption.height + 4
        KnobFace {
            id: face
            anchors.horizontalCenter: parent.horizontalCenter
            width: unit.roomy ? 44 : 36
            height: width
            position: knob.position
            ledColor: knob.ledColor
        }
        Text {
            id: caption
            anchors { horizontalCenter: parent.horizontalCenter; top: face.bottom; topMargin: 3 }
            text: knobMouse.containsMouse || knobMouse.pressed ? knob.readout : knob.label
            color: knobMouse.pressed ? Theme.text : Theme.engraved
            font.pixelSize: Theme.tinyFontSize
            font.bold: true
            font.letterSpacing: 1
        }
        MouseArea {
            id: knobMouse
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeVerCursor
            property point start
            property real from: 0
            onPressed: (mouse) => {
                start = Qt.point(mouse.x, mouse.y)
                from = knob.position
            }
            onPositionChanged: (mouse) => {
                if (!pressed) return
                const moved = (mouse.x - start.x) - (mouse.y - start.y)
                knob.turned(Math.max(0, Math.min(1, from + moved / 150)))
            }
            onDoubleClicked: knob.turned(knob.home)
            onWheel: (wheel) => knob.turned(Math.max(0, Math.min(1, knob.position + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))))
        }
    }

    // The fade time on its knob: 0.1 s .. 30 s, evenly in feel (a log scale).
    function fadeToPosition(seconds) { return Math.log(seconds / 0.1) / Math.log(300) }
    function positionToFade(p) { return 0.1 * Math.pow(300, p) }

    RowLayout {
        id: row
        anchors { left: card.left; right: card.right; verticalCenter: card.verticalCenter }
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
        spacing: Theme.spacing

        StripSection {
            label: qsTr("SUSTAIN PADS")
            Layout.alignment: Qt.AlignTop
            Rectangle {
                id: activate
                objectName: "sustainActivate"
                implicitWidth: unit.roomy ? 108 : 92
                implicitHeight: 2 * unit.keyHeight + 4
                radius: Theme.radius
                border.color: unit.pads.active ? Theme.ledGreen : Theme.outline
                border.width: unit.pads.active ? 2 : 1
                gradient: Gradient {
                    GradientStop { position: 0; color: activateMouse.pressed ? Theme.buttonBottom : Theme.buttonTop }
                    GradientStop { position: 1; color: Theme.buttonBottom }
                }
                Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelLight }
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    // Its light: green while it sounds, pulsing gently.
                    Rectangle {
                        id: activeLight
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 14
                        height: 14
                        radius: 7
                        border.color: "#000000"
                        color: unit.pads.active ? Theme.ledGreen : Theme.ledOff
                        SequentialAnimation on opacity {
                            running: unit.pads.active
                            loops: Animation.Infinite
                            NumberAnimation { from: 1; to: 0.55; duration: 1400; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 0.55; to: 1; duration: 1400; easing.type: Easing.InOutSine }
                            onRunningChanged: if (!running) activeLight.opacity = 1
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: unit.pads.active ? qsTr("ON") : qsTr("ACTIVATE")
                        font.pixelSize: Theme.smallFontSize + 1
                        font.bold: true
                        font.letterSpacing: 1
                        color: unit.pads.active ? Theme.text : Theme.textDim
                    }
                }
                MouseArea {
                    id: activateMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: unit.pads.active = !unit.pads.active
                }
                ToolTip.visible: activateMouse.containsMouse
                ToolTip.delay: 600
                ToolTip.text: unit.pads.active ? qsTr("Fade the pad out (over the fade time)")
                                               : qsTr("Fade the pad in, in %1").arg(unit.pads.keyNames[unit.pads.key])
            }
        }

        // The pad on a display: its name (step through with the arrows, or
        // click it for the list), the key under it, and whether it follows
        // the song.
        StripSection {
            label: qsTr("PAD")
            Layout.alignment: Qt.AlignTop
            LcdPanel {
                implicitWidth: unit.roomy ? 200 : 170
                implicitHeight: 2 * unit.keyHeight + 4
                Text {
                    objectName: "sustainPadName"
                    anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 8 }
                    horizontalAlignment: Text.AlignHCenter
                    leftPadding: unit.vst ? 6 : 34
                    rightPadding: unit.vst ? 6 : 34
                    text: unit.vst ? unit.pads.instrumentName : unit.pads.presetNames[unit.pads.preset]
                    color: Theme.lcdText
                    font.pixelSize: unit.roomy ? Theme.titleFontSize + 4 : Theme.titleFontSize
                    font.bold: true
                    elide: Text.ElideRight
                    MouseArea {
                        id: nameMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: unit.vst ? unit.openInstrumentWindow() : presetMenu.popup()
                    }
                    ToolTip.visible: nameMouse.containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: unit.vst ? qsTr("Open %1's window to choose its pad").arg(unit.pads.instrumentName)
                                           : qsTr("Choose the pad")
                    StageMenu {
                        id: presetMenu
                        Instantiator {
                            model: unit.pads.presetNames
                            delegate: StageMenuItem {
                                id: presetItem
                                required property string modelData
                                required property int index
                                text: presetItem.modelData
                                checkable: true
                                checked: unit.pads.preset === presetItem.index
                                onTriggered: unit.pads.preset = presetItem.index
                            }
                            onObjectAdded: (i, object) => presetMenu.insertItem(i, object)
                            onObjectRemoved: (i, object) => presetMenu.removeItem(object)
                        }
                    }
                }
                Text {
                    objectName: "sustainKeyShown"
                    anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 8 }
                    text: (unit.pads.active ? qsTr("in %1") : qsTr("%1 · off")).arg(unit.pads.keyNames[unit.pads.key])
                          + (unit.pads.octave > 0 ? "  +8va" : unit.pads.octave < 0 ? "  -8va" : "")
                    color: unit.pads.active ? Theme.lcdAccent : Theme.lcdTextDim
                    font.pixelSize: Theme.smallFontSize + 1
                    font.bold: true
                }
                StageButton {
                    objectName: "sustainPreviousPad"
                    visible: !unit.vst
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 4 }
                    iconSource: "icons/chevron-left.svg"
                    flat: true
                    enabled: unit.pads.preset > 0
                    tip: qsTr("The pad before (it fades over)")
                    onClicked: unit.pads.preset = unit.pads.preset - 1
                }
                StageButton {
                    objectName: "sustainNextPad"
                    visible: !unit.vst
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 4 }
                    iconSource: "icons/chevron-right.svg"
                    flat: true
                    enabled: unit.pads.preset < unit.pads.presetNames.length - 1
                    tip: qsTr("The next pad (it fades over)")
                    onClicked: unit.pads.preset = unit.pads.preset + 1
                }
            }
        }

        // The twelve keys: the black keys' row above the white keys', as on a keyboard.
        StripSection {
            label: qsTr("KEY")
            Layout.alignment: Qt.AlignTop
            Column {
                spacing: 4
                Row {
                    spacing: 4
                    leftPadding: (unit.keyWidth + 4) / 2
                    Repeater {
                        model: [1, 3, -1, 6, 8, 10]
                        delegate: Item {
                            id: blackSlot
                            required property int modelData
                            implicitWidth: unit.keyWidth
                            implicitHeight: unit.keyHeight
                            PadKey {
                                objectName: blackSlot.modelData >= 0 ? "sustainKey" + keyIndex : "" // (the gap between E and F)
                                visible: blackSlot.modelData >= 0
                                keyIndex: Math.max(blackSlot.modelData, 0)
                                anchors.fill: parent
                            }
                        }
                    }
                }
                Row {
                    spacing: 4
                    Repeater {
                        model: [0, 2, 4, 5, 7, 9, 11]
                        delegate: PadKey {
                            required property int modelData
                            keyIndex: modelData
                        }
                    }
                }
            }
        }

        StripSection {
            label: qsTr("FILTER · SHIMMER")
            visible: !unit.vst // (Pads Pro's: a VST's filter is in its own window)
            Layout.alignment: Qt.AlignTop
            PadXY {
                objectName: "sustainXY"
                implicitWidth: unit.padSize
                implicitHeight: unit.padSize
                xValue: unit.pads.tone
                yValue: unit.pads.shimmer
                xLabel: qsTr("Filter")
                yLabel: qsTr("Shimmer")
                colour: Theme.ledBlue
                onMoved: (across, up) => {
                    unit.pads.tone = across
                    unit.pads.shimmer = up
                }
            }
        }

        StripSection {
            label: qsTr("SOUND")
            Layout.alignment: Qt.AlignTop
            Row {
                spacing: 2
                PadKnob {
                    objectName: "sustainMotion"
                    visible: !unit.vst // (Pads Pro's: a VST moves in its own window)
                    label: qsTr("MOTION")
                    readout: Math.round(unit.pads.motion * 100) + "%"
                    position: unit.pads.motion
                    onTurned: (p) => unit.pads.motion = p
                }
                PadKnob {
                    objectName: "sustainFade"
                    label: qsTr("FADE")
                    readout: unit.pads.fadeSeconds < 10 ? unit.pads.fadeSeconds.toFixed(1) + " s" : Math.round(unit.pads.fadeSeconds) + " s"
                    position: unit.fadeToPosition(unit.pads.fadeSeconds)
                    home: unit.fadeToPosition(4)
                    ledColor: Theme.ledAmber
                    onTurned: (p) => unit.pads.fadeSeconds = unit.positionToFade(p)
                }
            }
            // The octave: down, as it is, up.
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: -1
                Repeater {
                    model: [-1, 0, 1]
                    delegate: StageButton {
                        id: octaveButton
                        required property int modelData
                        objectName: "sustainOctave" + (modelData < 0 ? "Down" : modelData > 0 ? "Up" : "Home")
                        implicitWidth: 36
                        text: modelData < 0 ? "−1" : modelData > 0 ? "+1" : "0" // octaves
                        checked: unit.pads.octave === octaveButton.modelData
                        tip: modelData < 0 ? qsTr("An octave down") : modelData > 0 ? qsTr("An octave up") : qsTr("Where it sits")
                        onClicked: unit.pads.octave = octaveButton.modelData
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // Its own window, and the song's key.
        Column {
            Layout.alignment: Qt.AlignTop
            spacing: 4
            StageButton {
                objectName: unit.inWindow ? "sustainAttach" : "sustainDetach"
                iconSource: "icons/app-window.svg"
                text: unit.inWindow ? qsTr("Reattach") : qsTr("Detach")
                tip: unit.inWindow ? qsTr("Back under the main area") : qsTr("Into a window of its own, to drag onto another screen")
                onClicked: unit.inWindow ? unit.attachRequested() : unit.detachRequested()
            }
            StageButton {
                objectName: "sustainFollowSong"
                text: qsTr("Song key")
                checkable: true
                checked: unit.pads.followSongKey
                tip: unit.pads.songKey !== "" ? qsTr("Follow the song's key (%1) when a song is picked").arg(unit.pads.songKey)
                                              : qsTr("Follow the song's key (from its chart) when a song is picked")
                onClicked: unit.pads.followSongKey = !unit.pads.followSongKey
            }
        }
    }
}
