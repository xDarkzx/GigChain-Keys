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

    // After loading, the splash steps through every plugin found while it
    // stays up: `playhead` runs from 0 to the number of plugins.
    readonly property bool finishing: startup.glideMs > 0
    readonly property int pluginCount: startup.plugins.length
    property real playhead: 0
    readonly property int current: Math.min(pluginCount - 1, Math.floor(playhead))
    readonly property bool listing: finishing && playhead < pluginCount

    NumberAnimation {
        id: replay
        target: splash
        property: "playhead"
        from: 0
    }
    onFinishingChanged: {
        if (!finishing) return
        replay.to = pluginCount
        replay.duration = startup.glideMs * 0.9 // the last moment shows "Ready"
        replay.start()
    }

    Column {
        x: 60
        width: splash.width - 120
        y: splash.height - 82
        spacing: 6

        Text {
            objectName: "splashStep"
            width: parent.width
            text: splash.listing ? qsTr("Loading plugins (%1 of %2)").arg(splash.current + 1).arg(splash.pluginCount)
                                 : splash.startup.step
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
                objectName: "splashFill"
                // Always fills from the left.
                readonly property real fraction: splash.finishing
                    ? (splash.pluginCount > 0 ? Math.min(1, splash.playhead / splash.pluginCount) : 1)
                    : Math.max(0, splash.startup.progress)
                height: parent.height
                radius: 2
                color: "#4fb3d9" // the chain's blue
                width: parent.width * fraction
            }
        }
        Text {
            objectName: "splashDetail"
            width: parent.width
            // The plugin being scanned or loaded, then each plugin found.
            text: splash.listing ? splash.startup.plugins[splash.current] : splash.startup.detail
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
