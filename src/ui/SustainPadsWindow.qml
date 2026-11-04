pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Sustain Pads in a window of its own (SustainPadsController.detached), to
// put on another screen: the same unit, bigger. Closing it (or Reattach, or
// the toolbar's Pads menu) puts it back under the main area. Opens where it
// was left, unless that screen is gone (then over the main window).
ApplicationWindow {
    id: win

    required property SustainPadsController pads
    property SettingsController settings: null
    property PluginListModel pluginModel: null
    property Window owner: null
    property rect normal: Qt.rect(0, 0, 0, 0) // where it is (remembered)
    property bool placed: false

    objectName: "sustainPadsWindow"
    title: qsTr("Sustain Pads — %1").arg(Branding.name)
    color: Theme.background
    flags: Qt.Window
    transientParent: null // (its own window: any screen, its own taskbar button)
    minimumWidth: Math.min(unit.implicitWidth, 900)
    minimumHeight: unit.implicitHeight
    width: unit.implicitWidth
    height: unit.implicitHeight
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
        win.pads.detached = false
    }

    function remember() {
        if (visibility === Window.Windowed) normal = Qt.rect(x, y, width, height)
        if (placed && visible) saveTimer.restart()
    }
    onXChanged: remember()
    onYChanged: remember()
    onWidthChanged: remember()
    onHeightChanged: remember()
    Timer {
        id: saveTimer
        interval: 400
        onTriggered: win.pads.rememberGeometry(win.normal)
    }

    Component.onCompleted: {
        const saved = pads.restoredGeometry()
        if (saved.width > 0) {
            x = saved.x
            y = saved.y
            width = Math.max(saved.width, minimumWidth)
            height = Math.max(saved.height, minimumHeight)
        } else if (owner !== null) {
            x = owner.x + (owner.width - width) / 2
            y = owner.y + owner.height - height - 80
        }
        normal = Qt.rect(x, y, width, height)
        placed = true
        show()
        requestActivate()
    }

    Flickable {
        anchors.fill: parent
        contentWidth: Math.max(width, unit.implicitWidth)
        contentHeight: Math.max(height, unit.implicitHeight)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width || contentHeight > height
        SustainPadsBar {
            id: unit
            width: Math.max(win.width, implicitWidth)
            height: Math.max(win.height, implicitHeight)
            pads: win.pads
            settings: win.settings
            pluginModel: win.pluginModel
            roomy: true
            inWindow: true
            onAttachRequested: win.pads.detached = false
        }
    }
}
