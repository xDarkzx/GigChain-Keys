import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The main area: the selected channel's own plugin window, as big as it can
// be (the toolbar already names the song and patch). The plugin does the
// heavy lifting here. Above it, MIDI Learn: the plugin draws its own window
// (its right-click is its own), so a knob in it is learned from here: click
// MIDI Learn, move the knob in the plugin, then a knob on the keyboard.
Rectangle {
    id: area

    required property DocumentController doc
    required property EngineStatus engineStatus
    required property EditorService editorService
    required property InstrumentWindows instrumentWindows
    property bool suspended: false
    // The instrument is in a window of its own (InstrumentWindows): not here.
    readonly property bool detached: editorService.shownElsewhere || instrumentWindows.linkedOpen

    color: Theme.background

    PayloadDropArea {
        anchors.fill: parent
        keys: ["instrument"]
        onPayloadDropped: (payload) => area.doc.addChannel(payload.pluginId, payload.name)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            objectName: "pluginLearnBar"
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            visible: (editorHost.hasEditor || area.editorService.builtinSelected) && !area.detached
            spacing: Theme.spacing
            StageButton {
                objectName: "pluginMidiLearn"
                text: area.engineStatus.learningMapping ? qsTr("Stop MIDI Learn") : qsTr("MIDI Learn")
                iconSource: "icons/plus.svg"
                checked: area.engineStatus.learningMapping
                tip: qsTr("Move a knob in the plugin, then a knob or fader on your keyboard: that knob moves it from now on")
                onClicked: area.engineStatus.learningMapping ? area.engineStatus.cancelMappingLearn()
                                                             : area.engineStatus.startMappingLearn(area.doc.selectedChannel, -1)
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.textDim
                text: qsTr("Click MIDI Learn, move a knob in the plugin, then turn a knob on your keyboard.")
                visible: !area.engineStatus.learningMapping
            }
            // The same instrument with the same settings in several songs is
            // loaded once: a change here changes them all, unless this song
            // takes its own copy.
            Label {
                objectName: "instrumentSharing"
                visible: area.doc.selectedInstrumentSongs > 1
                color: Theme.accent
                text: qsTr("Shared by %n songs: changes apply to all", "", area.doc.selectedInstrumentSongs)
            }
            StageButton {
                objectName: "instrumentOwnCopy"
                visible: area.doc.selectedInstrumentSongs > 1
                text: qsTr("Own copy for this song")
                tip: qsTr("Give this song a copy of its own, to change it without changing the other songs (uses more memory)")
                onClicked: area.doc.unshareInstrument(area.doc.selectedChannel)
            }
            // Into a window of its own, to drag to another screen (MainStage's
            // and Gig Performer's plug-in windows).
            StageButton {
                objectName: "instrumentPopOut"
                text: qsTr("Detach")
                iconSource: "icons/app-window.svg"
                tip: qsTr("Detach the instrument into a window of its own, to drag onto another screen (Ctrl+Shift+D)")
                onClicked: area.instrumentWindows.popOut()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // The app's own instruments: their windows are the app's.
            SawStackPanel {
                anchors.fill: parent
                visible: area.editorService.builtinId === "builtin:saw-stack-pro" && !area.detached
                service: area.editorService
                engineStatus: area.engineStatus
            }
            PianoProPanel {
                anchors.fill: parent
                visible: area.editorService.builtinId === "builtin:piano-pro" && !area.detached
                service: area.editorService
                engineStatus: area.engineStatus
            }
            PadsProPanel {
                anchors.fill: parent
                visible: area.editorService.builtinId === "builtin:pads-pro" && !area.detached
                service: area.editorService
                engineStatus: area.engineStatus
            }
            SoundFontPanel {
                anchors.fill: parent
                visible: area.editorService.builtinId === "builtin:soundfont" && !area.detached
                service: area.editorService
            }

            // Shown only where no plugin window covers the area.
            Label {
                anchors.centerIn: parent
                visible: !editorHost.hasEditor && !area.editorService.builtinSelected && !area.detached
                text: editorHost.emptyReason
                color: Theme.textDim
                font.pixelSize: Theme.headerFontSize
            }

            // In a window of its own: say so, with the way to it and back.
            Column {
                objectName: "instrumentDetached"
                anchors.centerIn: parent
                visible: area.detached
                spacing: Theme.spacingLarge
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: area.editorService.shownElsewhere && area.editorService.channelName !== ""
                          ? qsTr("%1 is in its own window").arg(area.editorService.channelName)
                          : qsTr("The instrument is in its own window")
                    color: Theme.text
                    font.pixelSize: Theme.headerFontSize
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Drag that window to any screen. It follows the channel you select, unless its Link is off.")
                    color: Theme.textDim
                }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.spacing
                    StageButton {
                        objectName: "instrumentShowWindow"
                        text: qsTr("Show window")
                        iconSource: "icons/app-window.svg"
                        tone: "accent"
                        onClicked: area.editorService.shownElsewhere ? area.instrumentWindows.showWindowOf(area.editorService)
                                                                     : area.instrumentWindows.popOut()
                    }
                    StageButton {
                        objectName: "instrumentBringBack"
                        text: qsTr("Reattach")
                        onClicked: area.editorService.shownElsewhere ? area.instrumentWindows.bringBack(area.editorService)
                                                                     : area.instrumentWindows.toggle()
                    }
                }
            }

            PluginEditorHost {
                id: editorHost
                objectName: "pluginEditorHost"
                anchors.fill: parent
                service: area.editorService
                suspended: area.suspended || area.detached
            }

        }
    }
}
