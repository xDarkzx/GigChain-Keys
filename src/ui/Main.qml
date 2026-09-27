import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root

    required property DocumentController doc
    required property SetlistModel setlistModel
    required property ChannelModel channelModel
    required property SelectedChannel selectedChannel
    required property PluginListModel pluginModel
    required property EngineStatus engineStatus
    required property EditorService editorService
    required property EffectWindows effectWindows
    required property MasterBus masterBus
    required property SettingsController settings
    required property StartupProgress loading

    property bool performMode: false
    property bool sidePanelOpen: true
    property bool mixerOpen: true
    property bool keyboardOpen: true
    property string pendingAction: ""
    property string pendingPath: "" // a recent setlist waiting to be opened
    property bool closeConfirmed: false
    // Shortcuts must not fire while the user types in a text field.
    readonly property bool typing: activeFocusItem instanceof TextInput

    width: 1440
    height: 880
    visible: true
    // No Windows frame: the toolbar is the title bar, with its own lights.
    flags: Qt.Window | Qt.FramelessWindowHint
    title: (doc.dirty ? "● " : "") + doc.displayName + " — " + Branding.name
    color: Theme.background
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

    onPerformModeChanged: visibility = performMode ? Window.FullScreen : Window.Windowed

    // ------------------------------------------------------------- file flow
    function runPending() {
        const action = pendingAction
        pendingAction = ""
        if (action === "close") {
            closeConfirmed = true
            root.close()
        } else if (action === "new") {
            doc.newSetlist()
        } else if (action === "open") {
            openDialog.open()
        } else if (action === "openRecent") {
            doc.open(pendingPath)
        }
    }
    function openRecent(path) {
        pendingPath = path
        guarded("openRecent")
    }
    function guarded(action) {
        pendingAction = action
        if (doc.dirty)
            unsavedDialog.open()
        else
            runPending()
    }
    function save() {
        if (doc.filePath === "")
            saveDialog.open()
        else
            doc.save()
    }
    function saveThenContinue() {
        if (doc.filePath === "") {
            saveDialog.open() // continues in onAccepted
        } else if (doc.save()) {
            runPending()
        }
    }

    onClosing: (close) => {
        if (doc.dirty && !closeConfirmed) {
            close.accepted = false
            guarded("close")
            return
        }
        // Closing the main window ends the app even if a plugin left a
        // window of its own open (otherwise the process would linger unseen).
        Qt.callLater(Qt.quit)
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open setlist")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("%1 setlists (*%2)").arg(Branding.name).arg(Branding.setlistSuffix), qsTr("All files (*)")]
        onAccepted: root.doc.openUrl(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save setlist")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "gigchain.json"
        nameFilters: [qsTr("%1 setlists (*%2)").arg(Branding.name).arg(Branding.setlistSuffix)]
        onAccepted: {
            if (root.doc.saveAsUrl(selectedFile) && root.pendingAction !== "")
                root.runPending()
        }
        onRejected: root.pendingAction = ""
    }
    MessageDialog {
        id: unsavedDialog
        text: qsTr("This setlist has unsaved changes.")
        informativeText: qsTr("Save them first?")
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Save)
                root.saveThenContinue()
            else if (button === MessageDialog.Discard)
                root.runPending()
            else
                root.pendingAction = ""
        }
    }
    // While sounds load (opening a setlist, adding an instrument): the app
    // is busy for a moment, so say so instead of looking frozen.
    LoadingOverlay {
        progress: root.loading
    }

    SettingsDialog {
        id: settingsDialog
        objectName: "settingsDialog"
        settings: root.settings
        pluginModel: root.pluginModel
    }
    ChannelZoneDialog {
        id: zoneDialog
        objectName: "zoneDialog"
        doc: root.doc
        channel: root.selectedChannel
    }
    KnobDialog {
        id: knobDialog
        objectName: "knobDialog"
        doc: root.doc
        channel: root.selectedChannel
        engineStatus: root.engineStatus
    }
    Connections {
        target: root.doc
        function onChannelEditRequested(channel, page) {
            if (page === "knobs") knobDialog.open()
            else zoneDialog.open()
        }
    }

    // ------------------------------------------------------------- shortcuts
    Shortcut { sequences: ["Space", "Right"]; enabled: !root.typing; onActivated: root.doc.nextPatch() }
    Shortcut { sequence: "Left"; enabled: !root.typing; onActivated: root.doc.previousPatch() }
    Shortcut { sequence: "PgDown"; enabled: !root.typing; onActivated: root.doc.nextSong() }
    Shortcut { sequence: "PgUp"; enabled: !root.typing; onActivated: root.doc.previousSong() }
    Shortcut { sequence: "Tab"; enabled: !root.typing; onActivated: root.performMode = !root.performMode }
    Shortcut { sequence: "Esc"; enabled: root.performMode; onActivated: root.performMode = false }
    Shortcut { sequences: [StandardKey.New]; enabled: !root.performMode; onActivated: root.guarded("new") }
    Shortcut { sequences: [StandardKey.Open]; enabled: !root.performMode; onActivated: root.guarded("open") }
    Shortcut { sequences: [StandardKey.Save]; enabled: !root.performMode; onActivated: root.save() }
    Shortcut { sequence: "Ctrl+Shift+S"; enabled: !root.performMode; onActivated: saveDialog.open() }
    Shortcut { sequence: "Ctrl+,"; enabled: !root.performMode; onActivated: settingsDialog.open() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !root.performMode && !root.typing; onActivated: root.doc.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; enabled: !root.performMode && !root.typing; onActivated: root.doc.redo() }

    header: Toolbar {
        doc: root.doc
        engineStatus: root.engineStatus
        performMode: root.performMode
        sidePanelOpen: root.sidePanelOpen
        mixerOpen: root.mixerOpen
        keyboardOpen: root.keyboardOpen
        onToggleKeyboard: root.keyboardOpen = !root.keyboardOpen
        onToggleMode: root.performMode = !root.performMode
        onToggleSidePanel: root.sidePanelOpen = !root.sidePanelOpen
        onToggleMixer: root.mixerOpen = !root.mixerOpen
        onNewRequested: root.guarded("new")
        onOpenRequested: root.guarded("open")
        onOpenRecentRequested: (path) => root.openRecent(path)
        onSaveRequested: root.save()
        onSaveAsRequested: saveDialog.open()
        onSettingsRequested: settingsDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.performMode ? 1 : 0

            SplitView {
                orientation: Qt.Horizontal
                handle: StageSplitHandle { vertical: true }

                SidePanel {
                    visible: root.sidePanelOpen
                    SplitView.preferredWidth: Theme.sidePanelWidth
                    SplitView.minimumWidth: 200
                    doc: root.doc
                    setlistModel: root.setlistModel
                    pluginModel: root.pluginModel
                    editable: true
                }

                // The plugin above, the mixer below (drag the divider).
                SplitView {
                    SplitView.fillWidth: true
                    orientation: Qt.Vertical
                    handle: StageSplitHandle {}

                    MainArea {
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 200
                        doc: root.doc
                        editorService: root.editorService
                        engineStatus: root.engineStatus
                        onNewRequested: root.guarded("new")
                        onOpenRequested: root.guarded("open")
                        onOpenRecentRequested: (path) => root.openRecent(path)
                        // Plugin windows sit above Qt content: hide them while a dialog is up.
                        suspended: settingsDialog.visible || unsavedDialog.visible || zoneDialog.visible || knobDialog.visible
                    }
                    Mixer {
                        visible: root.mixerOpen
                        SplitView.preferredHeight: Theme.mixerHeight
                        SplitView.minimumHeight: 240
                        doc: root.doc
                        channelModel: root.channelModel
                        pluginModel: root.pluginModel
                        engineStatus: root.engineStatus
                        effectWindows: root.effectWindows
                        masterBus: root.masterBus
                    }
                }
            }

            PerformView {
                doc: root.doc
                setlistModel: root.setlistModel
                channelModel: root.channelModel
                mixerOpen: root.mixerOpen
                pluginModel: root.pluginModel
                engineStatus: root.engineStatus
                sidePanelOpen: root.sidePanelOpen
                effectWindows: root.effectWindows
                masterBus: root.masterBus
            }
        }

        // The keys being played, lit up (and playable with the mouse).
        KeyboardView {
            objectName: "keyboardView"
            Layout.fillWidth: true
            visible: root.keyboardOpen
            engineStatus: root.engineStatus
        }

        // Status line: what's next on the left, the audio setup in the
        // middle, the limiter and audio inputs on the right.
        StagePanel {
            id: statusBar
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            bar: true

            Label {
                objectName: "statusNext"
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing
                width: Math.max(0, statusCenter.x - x - Theme.spacing)
                elide: Text.ElideRight
                visible: root.doc.nextPatchLabel !== ""
                text: qsTr("Next: %1").arg(root.doc.nextPatchLabel)
                color: Theme.textDim
                font.pixelSize: Theme.smallFontSize
            }
            Label {
                id: statusCenter
                objectName: "statusAudio"
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - 2 * statusRight.width - 4 * Theme.spacing)
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                text: root.engineStatus.statusText
                color: Theme.textDim
                font.pixelSize: Theme.smallFontSize
            }
            Row {
                id: statusRight
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing
                spacing: 6
                Label {
                    visible: root.engineStatus.audioInputChannels > 0
                    text: qsTr("Inputs: %1").arg(root.engineStatus.audioInputChannels)
                    color: Theme.textDim
                    font.pixelSize: Theme.smallFontSize
                }
                // Lit while the master limiter is pulling the level down.
                Rectangle {
                    objectName: "statusLimiter"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    border.color: Theme.outline
                    color: root.engineStatus.limiting ? Theme.meterHigh : "#2a2e35"
                }
                Label {
                    text: qsTr("Limiter")
                    color: Theme.textDim
                    font.pixelSize: Theme.smallFontSize
                }
            }
        }
    }

    // Errors and engine notices: never silent, never in the way.
    NotificationWindow {
        notifications: root.doc.notifications
        owner: root
    }

    // Without the Windows frame the edges still resize the window.
    WindowResizeEdges {
        objectName: "resizeEdges"
        window: root
    }
}
