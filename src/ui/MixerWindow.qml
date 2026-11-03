pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// The mixer in a window of its own (MixerLayout.detached): every strip,
// fader, the loop station, aux and master, to put on another screen. Closing
// it (or the toolbar's Mixer menu) puts it back. Opens where it was left,
// unless that screen is gone (then over the main window).
ApplicationWindow {
    id: win

    required property MixerLayout layout
    required property DocumentController doc
    required property ChannelModel channelModel
    required property PluginListModel pluginModel
    required property EngineStatus engineStatus
    property EffectWindows effectWindows: null
    property InstrumentWindows instrumentWindows: null
    property MasterBus masterBus: null
    property MasterBus auxBus: null
    property LoopController loops: null
    property bool editable: true
    property Window owner: null
    property rect normal: Qt.rect(0, 0, 0, 0) // where it is when not maximised (remembered)
    property bool placed: false

    objectName: "mixerWindow"
    title: qsTr("Mixer — %1").arg(Branding.name)
    color: Theme.background
    flags: Qt.Window
    transientParent: null // (its own window: any screen, its own taskbar button)
    minimumWidth: 480
    minimumHeight: 320
    width: 1100
    height: Theme.mixerHeight + Theme.looperHeight + 20 // a whole strip, the loop station over it
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

    // Its X: back into the main window.
    onClosing: (close) => {
        close.accepted = false
        win.layout.detached = false
    }

    function remember() {
        if (visibility === Window.Windowed) normal = Qt.rect(x, y, width, height)
        if (placed && visible) saveTimer.restart()
    }
    onXChanged: remember()
    onYChanged: remember()
    onWidthChanged: remember()
    onHeightChanged: remember()
    onVisibilityChanged: remember()
    Timer {
        id: saveTimer
        interval: 400
        onTriggered: win.layout.rememberGeometry(win.normal, win.visibility === Window.Maximized)
    }

    Component.onCompleted: {
        const saved = layout.restoredGeometry()
        let maximized = false
        if (saved.width > 0) {
            x = saved.x
            y = saved.y
            width = saved.width
            height = saved.height
            maximized = layout.restoredMaximized()
        } else if (owner !== null) {
            x = owner.x + (owner.width - width) / 2
            y = owner.y + (owner.height - height) / 2
        }
        normal = Qt.rect(x, y, width, height)
        placed = true
        if (maximized) showMaximized()
        else show()
        requestActivate()
    }

    Mixer {
        anchors.fill: parent
        tall: true // (make the window taller: longer faders)
        editable: win.editable
        doc: win.doc
        channelModel: win.channelModel
        pluginModel: win.pluginModel
        engineStatus: win.engineStatus
        effectWindows: win.effectWindows
        instrumentWindows: win.instrumentWindows
        masterBus: win.masterBus
        auxBus: win.auxBus
        loops: win.loops
    }
}
