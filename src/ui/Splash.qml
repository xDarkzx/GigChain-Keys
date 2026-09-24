import QtQuick
import QtQuick.Window

// Shown while the app starts: opens audio, scans plugins and loads the last
// setlist's sounds. The picture is branding.cmake's PRODUCT_SPLASH_IMAGE;
// the loading bar and the plugin being scanned sit in its dark lower part.
Window {
    id: splash

    required property StartupProgress startup

    width: art.implicitWidth
    height: art.implicitHeight
    visible: true
    // In front of every other window while it is up.
    flags: Qt.SplashScreen | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent" // the picture's rounded corners show the desktop
    title: Branding.name

    Image {
        id: art
        objectName: "splashImage"
        source: "qrc:/branding/splash.png"
        smooth: true
    }

    Column {
        x: 60
        width: splash.width - 120
        y: splash.height - 78
        spacing: 6

        Text {
            objectName: "splashStep"
            width: parent.width
            text: splash.startup.step
            color: "#c9d6e6"
            font.pixelSize: 12
            font.family: Theme.fontFamily
            elide: Text.ElideRight
        }
        Rectangle {
            objectName: "splashBar"
            width: parent.width
            height: 4
            radius: 2
            color: "#26344a"
            Rectangle {
                // Steps without a known length show a short centred bar.
                readonly property bool known: splash.startup.progress >= 0
                height: parent.height
                radius: 2
                color: "#4fb3d9" // the chain's blue
                width: known ? parent.width * splash.startup.progress : parent.width * 0.25
                x: known ? 0 : parent.width * 0.375
                // At the end the bar glides to full while the splash stays up.
                Behavior on width {
                    enabled: splash.startup.glideMs > 0
                    NumberAnimation { duration: splash.startup.glideMs; easing.type: Easing.InOutQuad }
                }
            }
        }
        Text {
            objectName: "splashDetail"
            width: parent.width
            text: splash.startup.detail // the plugin being scanned or loaded
            color: "#7f93ad"
            font.pixelSize: 11
            font.family: Theme.fontFamily
            elide: Text.ElideMiddle
        }
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 22
        y: 16
        text: "v" + Branding.version
        color: "#5d728c"
        font.pixelSize: 11
        font.family: Theme.fontFamily
    }
}
