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
    required property LoopController loops
    required property EditorService editorService
    required property EffectWindows effectWindows
    required property MasterBus masterBus
    property MasterBus auxBus: null // the shared effects the channels' Send knobs feed
    required property SettingsController settings
    required property PracticeController practice
    required property WarmupController warmup
    required property StartupProgress loading

    property bool performMode: false
    // Practice: a mode of its own beside Edit and Perform (the song's chords
    // falling onto a keyboard, PracticeView). Never with performMode.
    property bool practiceMode: false
    property bool sidePanelOpen: true
    // The mixer and the keyboard, open in Edit; in Perform closed unless
    // asked for (the chart is what is read on stage, as MainStage's Perform
    // mode shows only its layout). Each mode keeps its own.
    property bool editMixerOpen: true
    property bool editKeyboardOpen: true
    property bool performMixerOpen: false
    property bool performKeyboardOpen: false
    readonly property bool mixerOpen: performMode ? performMixerOpen : editMixerOpen
    // (Practice shows its own keyboard.)
    readonly property bool keyboardOpen: practiceMode ? false : performMode ? performKeyboardOpen : editKeyboardOpen
    property string pendingAction: ""
    property string pendingPath: "" // a recent setlist waiting to be opened
    property bool closeConfirmed: false
    // Shortcuts must not fire while the user types in a text field.
    readonly property bool typing: activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit
    // Nor while a menu or dialog has the keyboard (its arrows are its own).
    readonly property bool inPopup: {
        for (let item = activeFocusItem; item !== null; item = item.parent) {
            if (item === Overlay.overlay) return true
        }
        return false
    }
    readonly property bool keysFree: !typing && !inPopup

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

    // Edit / Perform / Practice.
    function editMode() {
        practiceMode = false
        performMode = false
    }
    function toggleMode() { // Edit and Perform (Tab); from Practice: Perform
        practiceMode = false
        performMode = !performMode
    }
    function enterPractice() {
        performMode = false
        practiceMode = true
    }

    // Help: the guide at a page ("": the page for what is on screen), About.
    function openHelp(topic) {
        const page = topic !== "" ? topic
                   : practiceMode ? "practice"
                   : performMode ? "perform"
                   : "getting-started"
        helpWindow.show(page)
    }
    function openAbout() { aboutDialog.open() }

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
        nameFilters: [qsTr("%1 setlists (*%2 *%3)").arg(Branding.name).arg(Branding.setlistSuffix).arg(Branding.jsonSetlistSuffix),
                      qsTr("All files (*)")]
        onAccepted: root.doc.openUrl(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save setlist")
        fileMode: FileDialog.SaveFile
        defaultSuffix: Branding.setlistSuffix.substring(1)
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
    LoopControlsDialog {
        id: loopControlsDialog
        objectName: "loopControlsDialog"
        loops: root.loops
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
            else if (page === "zone") zoneDialog.open()
            // ("plugin-learn": the Instrument tab learns, see MainArea.)
        }
    }

    // ------------------------------------------------------------- shortcuts
    // Playing (Edit and Perform; never while typing). The same as the pedals
    // and pads learned in Settings. docs/help/shortcuts.md lists them all.
    function playStop() {
        if (root.practiceMode) {
            if (root.practice.playing) root.practice.pause()
            else root.practice.play()
        } else {
            root.engineStatus.playPauseTrack()
        }
    }
    Shortcut { objectName: "keyPlay"; sequence: "Space"; enabled: root.keysFree; onActivated: root.playStop() }
    Shortcut { sequence: "Right"; enabled: root.keysFree; onActivated: root.doc.nextPatch() }
    Shortcut { sequence: "Left"; enabled: root.keysFree; onActivated: root.doc.previousPatch() }
    Shortcut { sequences: ["Down", "PgDown"]; enabled: root.keysFree; onActivated: root.doc.nextSong() }
    Shortcut { sequences: ["Up", "PgUp"]; enabled: root.keysFree; onActivated: root.doc.previousSong() }
    // The song's live controls (on the next bar line, or at the part's end).
    Shortcut { sequence: "N"; enabled: root.keysFree && !root.practiceMode; onActivated: root.doc.nextPart() }
    Shortcut { sequence: "Shift+N"; enabled: root.keysFree && !root.practiceMode; onActivated: root.doc.repeatPart() }
    Shortcut { sequence: "H"; enabled: root.keysFree && !root.practiceMode; onActivated: root.doc.holdPart() }
    Shortcut { sequence: "Shift+Space"; enabled: root.keysFree && !root.practiceMode; onActivated: root.doc.stopAtEndOfPart() }
    Shortcut { sequence: "T"; enabled: root.keysFree; onActivated: root.engineStatus.tapTempo() }
    Shortcut { sequence: "C"; enabled: root.keysFree; onActivated: root.engineStatus.clickOn = !root.engineStatus.clickOn }
    Shortcut { sequence: "M"; enabled: root.keysFree; onActivated: root.engineStatus.masterMuted = !root.engineStatus.masterMuted }
    Shortcut { sequence: "P"; enabled: root.keysFree; onActivated: root.engineStatus.panic() }
    // The loop station, on the selected channel.
    Shortcut {
        sequence: "R"
        enabled: root.keysFree && !root.practiceMode && root.doc.selectedChannel >= 0
        onActivated: root.loops.record(root.doc.selectedChannel)
    }
    Shortcut {
        sequence: "L"
        enabled: root.keysFree && !root.practiceMode && root.doc.selectedChannel >= 0
        onActivated: root.loops.playStop(root.doc.selectedChannel)
    }
    Shortcut { sequence: "Shift+L"; enabled: root.keysFree && !root.practiceMode; onActivated: root.loops.stopAll() }
    // A Mac's window keys (the window has no frame of its own): ⌘Q quits (asking
    // about unsaved changes first), ⌘M minimises.
    Shortcut { sequences: [StandardKey.Quit]; onActivated: root.close() }
    Shortcut { sequence: "Ctrl+M"; enabled: Theme.mac; onActivated: root.showMinimized() }
    Shortcut { sequence: "Ctrl+Shift+N"; enabled: !root.performMode && !root.typing; onActivated: root.doc.addSong() }
    Shortcut { sequences: [StandardKey.HelpContents, "F1"]; onActivated: root.openHelp("") }
    Shortcut { sequence: "Tab"; enabled: root.keysFree; onActivated: root.toggleMode() }
    Shortcut { sequence: "Esc"; enabled: (root.performMode || root.practiceMode) && !learnBanner.visible; onActivated: root.editMode() }
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
        practiceMode: root.practiceMode
        sidePanelOpen: root.sidePanelOpen
        mixerOpen: root.mixerOpen
        keyboardOpen: root.keyboardOpen
        loops: root.loops
        onToggleKeyboard: {
            if (root.performMode) root.performKeyboardOpen = !root.performKeyboardOpen
            else root.editKeyboardOpen = !root.editKeyboardOpen
        }
        onLoopControlsRequested: loopControlsDialog.open()
        onToggleMode: root.toggleMode()
        onEditRequested: root.editMode()
        onPracticeRequested: root.enterPractice()
        onToggleSidePanel: root.sidePanelOpen = !root.sidePanelOpen
        onToggleMixer: {
            if (root.performMode) root.performMixerOpen = !root.performMixerOpen
            else root.editMixerOpen = !root.editMixerOpen
        }
        onNewRequested: root.guarded("new")
        onOpenRequested: root.guarded("open")
        onOpenRecentRequested: (path) => root.openRecent(path)
        onSaveRequested: root.save()
        onSaveAsRequested: saveDialog.open()
        onSettingsRequested: settingsDialog.open()
        onHelpRequested: (topic) => root.openHelp(topic)
        onAboutRequested: root.openAbout()
        onFreeInstrumentsRequested: freeInstrumentsDialog.open()
    }

    HelpWindow { id: helpWindow }
    AboutDialog {
        id: aboutDialog
        onGuideRequested: root.openHelp("")
    }
    FreeInstrumentsDialog { id: freeInstrumentsDialog }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.practiceMode ? 2 : root.performMode ? 1 : 0

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
                    id: editSplit
                    SplitView.fillWidth: true
                    orientation: Qt.Vertical
                    handle: StageSplitHandle {}

                    MainArea {
                        id: mainArea
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
                                   || loopControlsDialog.visible || aboutDialog.visible || freeInstrumentsDialog.visible
                    }
                    Mixer {
                        visible: root.mixerOpen
                        // A whole strip, but never more than about half the room (a small screen keeps its chart).
                        SplitView.preferredHeight: Math.min(Theme.mixerHeight + (root.loops.stripVisible ? Theme.looperHeight : 0),
                                                            editSplit.height * 0.6)
                        SplitView.minimumHeight: 240
                        doc: root.doc
                        channelModel: root.channelModel
                        pluginModel: root.pluginModel
                        engineStatus: root.engineStatus
                        effectWindows: root.effectWindows
                        masterBus: root.masterBus
                        auxBus: root.auxBus
                        loops: root.loops
                    }
                }
            }

            PerformView {
                doc: root.doc
                settings: root.settings
                // "Add lyrics & chords": back to Edit, on the Chart tab.
                onEditChartRequested: {
                    root.performMode = false
                    mainArea.currentTab = 0
                }
                setlistModel: root.setlistModel
                channelModel: root.channelModel
                mixerOpen: root.mixerOpen
                pluginModel: root.pluginModel
                engineStatus: root.engineStatus
                sidePanelOpen: root.sidePanelOpen
                effectWindows: root.effectWindows
                masterBus: root.masterBus
                auxBus: root.auxBus
                loops: root.loops
            }

            PracticeView {
                practice: root.practice
                warmup: root.warmup
                engineStatus: root.engineStatus
                doc: root.doc
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

    // Learning a keyboard knob for a fader, a pan or the master: say what
    // to do until a knob is moved (Esc or Cancel calls it off).
    Rectangle {
        id: learnBanner
        objectName: "knobLearnBanner"
        // A mixer knob, or a plugin's knob learned from the Instrument tab
        // (the Knobs dialog says it in its own words).
        readonly property bool mixer: root.engineStatus.learningMixerKnob >= 0
        readonly property bool plugin: root.engineStatus.learningMapping && !knobDialog.visible
        visible: mixer || plugin
        anchors.horizontalCenter: parent.horizontalCenter
        y: 4 // over the top bar: a plugin's own window (drawn by the plugin) cannot cover it there
        z: 50
        width: learnRow.implicitWidth + 32
        height: 48
        radius: Theme.radiusCard
        color: Theme.accentBottom
        border.color: Theme.outline
        Row {
            id: learnRow
            anchors.centerIn: parent
            spacing: 16
            Label {
                objectName: "knobLearnText"
                anchors.verticalCenter: parent.verticalCenter
                text: learnBanner.mixer ? qsTr("MIDI Learn: move a knob or fader on your keyboard…")
                      : root.engineStatus.learnedParameter === ""
                        ? qsTr("MIDI Learn: move the knob you want in the plugin…")
                          + (root.engineStatus.learnedKnob !== "" ? "  (" + root.engineStatus.learnedKnob + ")" : "")
                        : qsTr("MIDI Learn: %1 — now turn a knob on your keyboard…").arg(root.engineStatus.learnedParameter)
                color: "white"
                font.pixelSize: Theme.fontSize + 2
                font.bold: true
            }
            StageButton {
                objectName: "knobLearnCancel"
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Cancel")
                onClicked: root.cancelLearning()
            }
        }
    }
    function cancelLearning() {
        root.engineStatus.cancelMixerKnobLearn()
        root.engineStatus.cancelMappingLearn()
    }
    Shortcut {
        sequence: "Esc"
        enabled: learnBanner.visible
        onActivated: root.cancelLearning()
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
