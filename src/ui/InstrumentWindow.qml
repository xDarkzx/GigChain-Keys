pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// An instrument in a window of its own (InstrumentWindows), to put on any
// screen, as MainStage's and Gig Performer's plug-in windows. Its header:
// Link (follow the selected channel, or stay on this one), which channel and
// song it shows, MIDI Learn, On top, Dock. The window takes the plugin's own
// size, as DAWs do, and again when a linked window moves to another plugin,
// unless it is maximised (then the plugin sits in the middle). The linked
// window opens where it was left, unless that screen is gone.
// (An ApplicationWindow, as the main window: its font and palette are the app's.)
ApplicationWindow {
    id: win

    required property EditorService service
    required property InstrumentWindows windows
    required property DocumentController doc
    required property EngineStatus engineStatus
    property Window owner: null

    readonly property bool builtin: service.builtinSelected && !service.shownElsewhere
    // The plugin's own size (the app's own instruments: their panel's).
    readonly property size wanted: builtin ? Qt.size(1080, 640) : editorHost.editorSize
    property rect normal: Qt.rect(0, 0, 0, 0) // where it is when not maximised (remembered)
    property bool placed: false

    objectName: service.linked ? "instrumentWindowLinked" : "instrumentWindowKept"
    title: (service.channelName !== "" ? service.channelName + " — " : "") + doc.currentSongName + " — " + Branding.name
    color: Theme.background
    flags: Qt.Window | (windows.onTop ? Qt.WindowStaysOnTopHint : 0)
    // (Its own window, not the main window's: any screen, its own taskbar button.)
    transientParent: null
    minimumWidth: 420
    minimumHeight: 240
    width: 900
    height: 620
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSize
    palette {
        window: Theme.background
        windowText: Theme.text
        base: Theme.panel
        alternateBase: Theme.panelRaised
        text: Theme.text
        button: Theme.panelRaised
        buttonText: Theme.text
        highlight: Theme.accent
        highlightedText: Theme.accentText
        placeholderText: Theme.textDim
        mid: Theme.border
        dark: Theme.border
        light: Theme.panelRaised
        midlight: Theme.panelRaised
        shadow: "#000000"
        toolTipBase: Theme.panelRaised
        toolTipText: Theme.text
        brightText: Theme.text
        link: Theme.accent
    }

    // Its X: closed (a linked window's instrument goes back to the tab).
    onClosing: (close) => {
        close.accepted = false
        win.windows.close(win.service)
    }

    function fitToPlugin() {
        if (!placed || visibility !== Window.Windowed || wanted.width <= 0 || wanted.height <= 0) return
        // (Never bigger than its screen: a plugin that cannot shrink is cut off.)
        width = Math.max(minimumWidth, Math.min(wanted.width, Screen.width - 32))
        height = Math.max(minimumHeight, Math.min(wanted.height + headerBar.height, Screen.height - 96))
    }
    onWantedChanged: fitToPlugin()

    function remember() {
        if (visibility === Window.Windowed) normal = Qt.rect(x, y, width, height)
        if (service.linked && placed && visible) saveTimer.restart()
    }
    onXChanged: remember()
    onYChanged: remember()
    onWidthChanged: remember()
    onHeightChanged: remember()
    onVisibilityChanged: remember()
    Timer {
        id: saveTimer
        interval: 400
        onTriggered: win.windows.rememberGeometry(win.normal, win.visibility === Window.Maximized)
    }

    Component.onCompleted: {
        let maximized = false
        const saved = service.linked ? windows.restoredGeometry() : Qt.rect(0, 0, 0, 0)
        if (saved.width > 0) {
            x = saved.x
            y = saved.y
            width = saved.width
            height = saved.height
            maximized = windows.restoredMaximized()
        } else if (owner !== null) {
            // Over the main window's middle, each new one a little further on.
            const step = 28 * (windows.windows.length % 8)
            x = owner.x + (owner.width - width) / 2 + step
            y = owner.y + (owner.height - height) / 2 + step
        }
        normal = Qt.rect(x, y, width, height)
        placed = true
        if (maximized) showMaximized()
        else show()
        if (saved.width <= 0) fitToPlugin()
        requestActivate()
    }

    Connections {
        target: win.windows
        function onRaiseRequested(window) {
            if (window !== win.service) return
            if (win.visibility === Window.Minimized) win.showNormal()
            win.raise()
            win.requestActivate()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------------ header
        StagePanel {
            id: headerBar
            objectName: "instrumentWindowHeader"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight + 2 * Theme.spacing
            bar: true

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing
                anchors.rightMargin: Theme.spacing
                spacing: Theme.spacing

                StageButton {
                    objectName: "instrumentLink"
                    text: qsTr("Link")
                    iconSource: "icons/link.svg"
                    checkable: true
                    checked: win.service.linked
                    enabled: win.service.linked || !win.windows.linkedOpen
                    tip: win.service.linked ? qsTr("Follows the channel you select. Click to keep this window on %1").arg(win.service.channelName || qsTr("this channel"))
                                            : win.windows.linkedOpen ? qsTr("Another window already follows the channel you select")
                                                                    : qsTr("Kept on %1. Click to follow the channel you select").arg(win.service.channelName)
                    onClicked: win.windows.setLinked(win.service, !win.service.linked)
                }
                Label {
                    objectName: "instrumentWindowChannel"
                    text: win.service.channelName !== "" ? win.service.channelName : qsTr("No channel")
                    color: Theme.text
                    font.bold: true
                    font.pixelSize: Theme.fontSize + 2
                }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: win.engineStatus.learningMapping
                          ? (win.engineStatus.learnedParameter === ""
                             ? qsTr("MIDI Learn: move the knob you want in the plugin…")
                             : qsTr("MIDI Learn: %1 — now turn a knob on your keyboard…").arg(win.engineStatus.learnedParameter))
                          : win.doc.currentSongName + (win.service.linked ? qsTr("  ·  follows the selected channel") : "")
                    color: win.engineStatus.learningMapping ? Theme.accent : Theme.textDim
                }
                StageButton {
                    objectName: "instrumentWindowLearn"
                    text: win.engineStatus.learningMapping ? qsTr("Stop MIDI Learn") : qsTr("MIDI Learn")
                    iconSource: "icons/plus.svg"
                    checked: win.engineStatus.learningMapping
                    enabled: win.service.channelIndex >= 0 && !win.service.shownElsewhere && (editorHost.hasEditor || win.builtin)
                    tip: qsTr("Move a knob in the plugin, then a knob or fader on your keyboard: that knob moves it from now on")
                    onClicked: win.engineStatus.learningMapping ? win.engineStatus.cancelMappingLearn()
                                                                : win.engineStatus.startMappingLearn(win.service.channelIndex, -1)
                }
                StageButton {
                    objectName: "instrumentOnTop"
                    iconSource: "icons/pin.svg"
                    checkable: true
                    checked: win.windows.onTop
                    tip: win.windows.onTop ? qsTr("On top of every other window. Click to let other windows cover it")
                                           : qsTr("Keep it on top of every other window")
                    onClicked: win.windows.onTop = !win.windows.onTop
                }
                StageButton {
                    objectName: "instrumentDock"
                    text: qsTr("Reattach")
                    tone: "accent"
                    tip: qsTr("Reattach it to the main window's Instrument tab (Ctrl+Shift+D)")
                    onClicked: win.windows.dock(win.service)
                }
            }
        }

        // ----------------------------------------------------------- content
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Loaded only while shown: when the window closes, the panel is
            // unloaded first, whole (a Loader takes it down quietly; the
            // window going would leave its rows reading a panel gone).
            Loader {
                anchors.fill: parent
                active: win.builtin && win.service.builtinId === "builtin:saw-stack-pro"
                sourceComponent: SawStackPanel { service: win.service }
            }
            Loader {
                anchors.fill: parent
                active: win.builtin && win.service.builtinId === "builtin:piano-pro"
                sourceComponent: PianoProPanel { service: win.service }
            }
            Loader {
                anchors.fill: parent
                active: win.builtin && win.service.builtinId === "builtin:pads-pro"
                sourceComponent: PadsProPanel { service: win.service }
            }
            Loader {
                anchors.fill: parent
                active: win.builtin && win.service.builtinId === "builtin:soundfont"
                sourceComponent: SoundFontPanel { service: win.service }
            }

            // Nothing to show: why (no channel, not in this song), or where it is.
            Column {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacingLarge
                visible: !editorHost.hasEditor && !win.builtin
                spacing: Theme.spacingLarge
                Label {
                    objectName: "instrumentWindowEmpty"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: win.service.shownElsewhere ? qsTr("%1 is in another window").arg(win.service.channelName)
                                                     : editorHost.emptyReason
                    color: Theme.textDim
                    font.pixelSize: Theme.headerFontSize
                }
                StageButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: win.service.shownElsewhere
                    text: qsTr("Show that window")
                    onClicked: win.windows.showWindowOf(win.service)
                }
            }

            PluginEditorHost {
                id: editorHost
                objectName: "instrumentWindowHost"
                anchors.fill: parent
                service: win.service
            }
        }
    }
}
