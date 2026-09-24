import QtQuick
import QtQuick.Window

// Shown while OpenStage starts: opens audio, scans plugins and loads the
// last setlist's sounds. Drawn here; no image files.
Window {
    id: splash

    required property StartupProgress startup
    property string version: Qt.application.version

    width: 560
    height: 320
    visible: true
    flags: Qt.SplashScreen | Qt.FramelessWindowHint
    color: "transparent"

    Rectangle {
        anchors.fill: parent
        radius: 10
        border.color: Theme.border
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1f2430" }
            GradientStop { position: 1.0; color: Theme.background }
        }
        clip: true

        // A keyboard along the bottom edge.
        Row {
            id: keys
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 2
            opacity: 0.35
            Repeater {
                model: 28
                Rectangle { width: 18; height: 70; radius: 2; color: "#e6e8ec" }
            }
        }
        Repeater {
            // Black keys: two, then three, per octave of seven white keys.
            model: 28
            Rectangle {
                required property int index
                readonly property int inOctave: index % 7
                visible: inOctave !== 2 && inOctave !== 6 && index < 27
                x: keys.x + (index + 1) * 20 - 6
                y: keys.y
                width: 11
                height: 42
                radius: 2
                color: "#14161a"
                opacity: 0.9
            }
        }

        Column {
            x: 36
            y: 38
            spacing: 6
            Row {
                spacing: 10
                Rectangle { width: 6; height: 44; radius: 3; color: Theme.accent }
                Text {
                    text: "OpenStage"
                    color: Theme.text
                    font.pixelSize: 40
                    font.weight: Font.DemiBold
                    font.family: Theme.fontFamily
                }
            }
            Text {
                text: qsTr("Live performance host  ·  version %1").arg(splash.version)
                color: Theme.textDim
                font.pixelSize: Theme.fontSize
                font.family: Theme.fontFamily
            }
        }

        Column {
            x: 36
            width: parent.width - 72
            y: 150
            spacing: 8
            Text {
                objectName: "splashStep"
                width: parent.width
                text: splash.startup.step
                color: Theme.text
                font.pixelSize: Theme.fontSize
                font.family: Theme.fontFamily
                elide: Text.ElideRight
            }
            Rectangle {
                width: parent.width
                height: 4
                radius: 2
                color: Theme.border
                Rectangle {
                    // Indeterminate steps show a short sliding bar.
                    readonly property bool known: splash.startup.progress >= 0
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                    width: known ? parent.width * splash.startup.progress : parent.width * 0.25
                    x: known ? 0 : parent.width * 0.375
                }
            }
            Text {
                width: parent.width
                text: splash.startup.detail
                color: Theme.textDim
                font.pixelSize: Theme.smallFontSize
                font.family: Theme.fontFamily
                elide: Text.ElideMiddle
            }
        }
    }
}
