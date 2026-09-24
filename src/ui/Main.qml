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
    required property SettingsController settings

    property bool performMode: false
    property bool sidePanelOpen: true
    property bool mixerOpen: true
    property string pendingAction: ""
    property bool closeConfirmed: false
    // Shortcuts must not fire while the user types in a text field.
    readonly property bool typing: activeFocusItem instanceof TextInput

    width: 1440
    height: 880
    visible: true
    title: (doc.dirty ? "● " : "") + doc.displayName + " — OpenStage"
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
        }
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
        // Closing the main window ends OpenStage even if a plugin left a
        // window of its own open (otherwise the process would linger unseen).
        Qt.callLater(Qt.quit)
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open setlist")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("OpenStage setlists (*.openstage.json)"), qsTr("All files (*)")]
        onAccepted: root.doc.openUrl(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save setlist")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "openstage.json"
        nameFilters: [qsTr("OpenStage setlists (*.openstage.json)")]
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
    SettingsDialog {
        id: settingsDialog
        objectName: "settingsDialog"
        settings: root.settings
        pluginModel: root.pluginModel
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

    header: Toolbar {
        doc: root.doc
        engineStatus: root.engineStatus
        performMode: root.performMode
        sidePanelOpen: root.sidePanelOpen
        mixerOpen: root.mixerOpen
        onToggleMode: root.performMode = !root.performMode
        onToggleSidePanel: root.sidePanelOpen = !root.sidePanelOpen
        onToggleMixer: root.mixerOpen = !root.mixerOpen
        onNewRequested: root.guarded("new")
        onOpenRequested: root.guarded("open")
        onSaveRequested: root.save()
        onSaveAsRequested: saveDialog.open()
        onSettingsRequested: settingsDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Errors and engine notices: never silent.
        Rectangle {
            objectName: "messageBanner"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 34 : 0
            visible: root.doc.lastError !== ""
            color: Theme.danger
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing * 2
                anchors.rightMargin: Theme.spacing
                Label {
                    text: root.doc.lastError
                    color: "white"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                ToolButton {
                    text: "✕"
                    onClicked: root.doc.clearError()
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.performMode ? 1 : 0

            SplitView {
                orientation: Qt.Horizontal

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

                    PluginArea {
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 200
                        doc: root.doc
                        editorService: root.editorService
                        // Plugin windows sit above Qt content: hide them while a dialog is up.
                        suspended: settingsDialog.visible || unsavedDialog.visible
                    }
                    Mixer {
                        visible: root.mixerOpen
                        SplitView.preferredHeight: Theme.mixerHeight
                        SplitView.minimumHeight: 240
                        doc: root.doc
                        channelModel: root.channelModel
                        pluginModel: root.pluginModel
                        engineStatus: root.engineStatus
                    }
                }
            }

            PerformView {
                doc: root.doc
                setlistModel: root.setlistModel
                pluginModel: root.pluginModel
                engineStatus: root.engineStatus
                sidePanelOpen: root.sidePanelOpen
            }
        }

        // Status line: the audio setup the engine is using.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Theme.panelRaised
            Label {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.spacing
                text: root.engineStatus.statusText
                color: Theme.textDim
                font.pixelSize: Theme.smallFontSize
            }
        }
    }
}
