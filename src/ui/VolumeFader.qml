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

        // groove
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 4
            height: parent.height
            radius: 2
            color: Theme.faderGroove
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
            anchors.fill: parent
            orientation: Qt.Vertical
            from: fader.minDb
            to: fader.maxDb
            value: fader.volumeDb
            focusPolicy: Qt.NoFocus
            padding: 0
            background: Item {}
            handle: Rectangle {
                // the fader cap
                x: (slider.width - width) / 2
                y: slider.visualPosition * (slider.availableHeight - height)
                width: 22
                height: 34
                radius: 3
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.faderCapTop }
                    GradientStop { position: 0.5; color: Theme.faderCapMid }
                    GradientStop { position: 1.0; color: Theme.faderCapBottom }
                }
                border.color: "#111"
                Rectangle { anchors.centerIn: parent; width: parent.width - 4; height: 2; color: "#f0f0f0" }
            }
            onMoved: fader.volumeMoved(value)
            TapHandler {
                onDoubleTapped: fader.volumeMoved(0) // back to unity
            }
        }
    }

    // meter on the right
    Rectangle {
        id: meter
        x: track.x + track.width + 4
        y: track.y
        width: 8
        height: track.height
        radius: 2
        color: Theme.meterBackground
        clip: true

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: parent.height * fader.levelFraction
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.meterHigh }
                GradientStop { position: 0.18; color: Theme.meterMid }
                GradientStop { position: 0.35; color: Theme.meterLow }
                GradientStop { position: 1.0; color: Theme.meterLow }
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
