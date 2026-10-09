pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Logic-style fader with a dB scale and a level meter with peak hold.
// `volumeDb` is the fader position; `level` the linear signal peak (0..1).
Item {
    id: fader

    property real volumeDb: 0
    property real level: 0
    property real minDb: -60
    property real maxDb: 12
    signal volumeMoved(real volumeDb)

    readonly property real levelDb: level > 0 ? 20 * Math.log10(level) : -200
    readonly property real levelFraction: Math.max(0, Math.min(1, (levelDb - minDb) / (maxDb - minDb)))
    property real holdFraction: 0

    function fractionOf(db) { return (db - minDb) / (maxDb - minDb) }

    onLevelFractionChanged: {
        if (levelFraction >= holdFraction) {
            holdFraction = levelFraction
            holdTimer.restart()
        }
    }
    Timer {
        id: holdTimer
        interval: 1500
        onTriggered: fader.holdFraction = fader.levelFraction
    }

    implicitWidth: 64

    // dB scale on the left
    Repeater {
        model: [12, 6, 0, -6, -12, -24, -40, -60]
        delegate: Text {
            required property int modelData
            x: 0
            width: 18
            horizontalAlignment: Text.AlignRight
            y: track.y + track.height * (1 - fader.fractionOf(modelData)) - height / 2
            text: modelData > 0 ? "+" + modelData : String(modelData)
            color: modelData === 0 ? Theme.text : Theme.textDim
            font.pixelSize: 8
        }
    }

    Item {
        id: track
        x: 22
        y: 8
        width: 20
        height: fader.height - 16

        // groove: a slot cut into the panel (dark, its far edge catching the light)
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 6
            height: parent.height
            radius: 3
            color: Theme.faderGroove
            border.color: "#000000"
            Rectangle { x: parent.width - 1; y: 2; width: 1; height: parent.height - 4; color: Theme.bevelLight }
        }
        // 0 dB mark
        Rectangle {
            x: 0
            width: parent.width
            height: 1
            y: parent.height * (1 - fader.fractionOf(0))
            color: Theme.border
        }

        Slider {
            id: slider
            objectName: "faderSlider"
            anchors.fill: parent
            orientation: Qt.Vertical
            from: fader.minDb
            to: fader.maxDb
            // Follows the volume whenever it is not being dragged (a plain
            // binding would be cut by the first drag, and typed values
            // would no longer move the cap).
            Binding on value {
                value: fader.volumeDb
                when: !slider.pressed
                restoreMode: Binding.RestoreNone
            }
            focusPolicy: Qt.NoFocus
            padding: 0
            background: Item {}
            handle: Item {
                // The fader cap, as a console's: a shadow under it, brushed
                // metal with grip ridges, and a white line where it reads.
                x: (slider.width - width) / 2
                y: slider.visualPosition * (slider.availableHeight - height)
                width: 24
                height: 38
                Rectangle { x: 1; y: 3; width: parent.width; height: parent.height; radius: 3; color: Theme.shadow }
                Rectangle {
                    anchors.fill: parent
                    radius: 3
                    border.color: "#0d0d0e"
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.faderCapTop }
                        GradientStop { position: 0.48; color: Theme.faderCapMid }
                        GradientStop { position: 0.52; color: Theme.faderCapBottom }
                        GradientStop { position: 1.0; color: Theme.faderCapMid }
                    }
                    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: "#80ffffff" }
                    // grip ridges above and below the line
                    Repeater {
                        model: [6, 9, 12, 25, 28, 31]
                        delegate: Rectangle {
                            required property int modelData
                            x: 4
                            y: modelData
                            width: 16
                            height: 1
                            color: "#55000000"
                            Rectangle { y: 1; width: parent.width; height: 1; color: "#30ffffff" }
                        }
                    }
                    Rectangle { anchors.centerIn: parent; width: parent.width - 2; height: 2; color: "#ffffff" }
                }
            }
            onMoved: fader.volumeMoved(value)
            TapHandler {
                onDoubleTapped: fader.volumeMoved(0) // back to unity
            }
        }
    }

    // meter on the right: LED segments, lit up to the level (green, then
    // amber near 0 dB, red over it), the unlit ones faintly visible.
    Rectangle {
        id: meter
        x: track.x + track.width + 4
        y: track.y
        width: 9
        height: track.height
        radius: 2
        color: Theme.meterBackground
        border.color: "#000000"
        clip: true

        readonly property int segmentCount: Math.max(8, Math.floor((height - 2) / 3))
        Column {
            x: 1
            y: 1
            width: parent.width - 2
            spacing: 1
            Repeater {
                model: meter.segmentCount
                delegate: Rectangle {
                    required property int index
                    // From the top: segment 0 is the loudest.
                    readonly property real at: 1 - (index + 0.5) / meter.segmentCount
                    readonly property bool lit: at <= fader.levelFraction
                    readonly property color hue: at > fader.fractionOf(0) ? Theme.meterHigh
                                                 : at > fader.fractionOf(-9) ? Theme.meterMid : Theme.meterLow
                    width: meter.width - 2
                    height: (meter.height - 2) / meter.segmentCount - 1
                    color: lit ? hue : Qt.rgba(hue.r, hue.g, hue.b, 0.12)
                }
            }
        }
        Rectangle {
            // peak hold line
            visible: fader.holdFraction > 0.01
            width: parent.width
            height: 2
            y: parent.height * (1 - fader.holdFraction)
            color: fader.holdFraction > fader.fractionOf(0) ? Theme.meterHigh : Theme.text
        }
    }
}
