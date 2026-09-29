pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Settings, laid out like Audacity 4's Preferences: pages on the left, the
// page on the right, Reset / Cancel / OK at the bottom. Nothing changes until
// OK; a problem stays on screen with its cause.
StageDialog {
    id: dialog

    required property SettingsController settings
    required property PluginListModel pluginModel

    property int page: 0

    title: qsTr("Settings")
    width: Math.min(880, (parent ? parent.width : 880) - 32)
    height: Math.min(640, (parent ? parent.height : 640) - 32)

    onAboutToShow: settings.load()

    Timer {
        interval: 1500
        repeat: true
        running: dialog.visible
        onTriggered: dialog.settings.refreshMidi()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // page list
            ListView {
                id: pages
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                Layout.topMargin: 8
                interactive: false
                model: [qsTr("General"), qsTr("Audio"), qsTr("MIDI"), qsTr("Plugins")]
                delegate: ItemDelegate {
                    id: pageRow
                    required property int index
                    required property string modelData
                    width: ListView.view.width
                    implicitHeight: 34
                    onClicked: dialog.page = index
                    contentItem: Text {
                        leftPadding: 12
                        text: pageRow.modelData
                        color: dialog.page === pageRow.index ? "white" : Theme.text
                        font.pixelSize: Theme.fontSize
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        id: pageBack
                        readonly property bool chosen: dialog.page === pageRow.index
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        radius: Theme.radiusSmall
                        border.color: chosen || pageRow.hovered ? Theme.outline : "transparent"
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: pageBack.chosen ? Theme.accentTop : (pageRow.hovered ? Theme.buttonHoverTop : "transparent") }
                            GradientStop { position: 1.0; color: pageBack.chosen ? Theme.accentBottom : (pageRow.hovered ? Theme.buttonHoverBottom : "transparent") }
                        }
                    }
                }
            }
            StageDivider { vertical: true; Layout.fillHeight: true }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: dialog.page

                // ------------------------------------------------ General
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        SettingsSection { title: qsTr("When %1 starts").arg(Branding.name) }
                        CheckBox {
                            objectName: "reopenLastSetlist"
                            Layout.leftMargin: 14
                            text: qsTr("Open the last setlist I used")
                            checked: dialog.settings.reopenLastSetlist
                            onToggled: dialog.settings.reopenLastSetlist = checked
                            focusPolicy: Qt.NoFocus
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("Handy on a gig night: its sounds load behind the splash screen, ready to play. "
                                       + "Off, you start on the start screen and pick a setlist (or make a new one).")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                    }
                }

                // ------------------------------------------------ Audio
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 14
                        SettingsSection { title: qsTr("Audio output") }
                        SettingsRow {
                            label: qsTr("Driver")
                            StageComboBox {
                                objectName: "driverBox"
                                // This system's drivers ([{id, name}]): WASAPI and ASIO on
                                // Windows; PulseAudio, JACK and ALSA on Linux.
                                model: dialog.settings.drivers
                                textRole: "name"
                                currentIndex: dialog.settings.drivers.findIndex(d => d.id === dialog.settings.driver)
                                onActivated: (i) => dialog.settings.driver = dialog.settings.drivers[i].id
                            }
                        }
                        SettingsRow {
                            label: qsTr("Device")
                            StageComboBox {
                                objectName: "deviceBox"
                                implicitWidth: 360
                                model: dialog.settings.devices
                                currentIndex: dialog.settings.devices.indexOf(dialog.settings.device)
                                onActivated: (i) => dialog.settings.device = dialog.settings.devices[i]
                            }
                        }
                        SettingsSection { title: qsTr("Audio inputs") }
                        SettingsRow {
                            label: qsTr("Input device")
                            StageComboBox {
                                objectName: "inputDeviceBox"
                                implicitWidth: 360
                                model: [qsTr("None")].concat(dialog.settings.inputDevices)
                                currentIndex: dialog.settings.inputDevice === "" ? 0 : dialog.settings.inputDevices.indexOf(dialog.settings.inputDevice) + 1
                                onActivated: (i) => dialog.settings.inputDevice = i === 0 ? "" : dialog.settings.inputDevices[i - 1]
                            }
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("A vocal mic or a guitar through effects: choose the device it is plugged into, then add an "
                                       + "audio input channel in the mixer. With ASIO it is the same device as the output.")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        SettingsSection { title: qsTr("Quality and latency") }
                        SettingsRow {
                            label: qsTr("Sample rate")
                            StageComboBox {
                                model: dialog.settings.sampleRates.map((r) => (r / 1000) + " kHz")
                                currentIndex: dialog.settings.sampleRates.indexOf(dialog.settings.sampleRate)
                                onActivated: (i) => dialog.settings.sampleRate = dialog.settings.sampleRates[i]
                            }
                        }
                        SettingsRow {
                            label: qsTr("Buffer size")
                            StageComboBox {
                                model: dialog.settings.bufferSizes.map((b) => b + qsTr(" samples"))
                                currentIndex: dialog.settings.bufferSizes.indexOf(dialog.settings.bufferFrames)
                                onActivated: (i) => dialog.settings.bufferFrames = dialog.settings.bufferSizes[i]
                            }
                        }
                        SettingsRow {
                            label: qsTr("Latency")
                            Label {
                                text: qsTr("%1 ms per buffer — lower feels tighter, higher is safer against clicks")
                                          .arg(dialog.settings.latencyMs.toFixed(1))
                                color: Theme.textDim
                            }
                        }
                        SettingsSection { title: qsTr("Safety limiter") }
                        CheckBox {
                            objectName: "limiterEnabled"
                            Layout.leftMargin: 14
                            text: qsTr("Never let the output go past the ceiling")
                            checked: dialog.settings.limiterEnabled
                            onToggled: dialog.settings.limiterEnabled = checked
                            focusPolicy: Qt.NoFocus
                        }
                        SettingsRow {
                            label: qsTr("Ceiling")
                            StageComboBox {
                                objectName: "limiterCeiling"
                                enabled: dialog.settings.limiterEnabled
                                model: dialog.settings.limiterCeilings.map((c) => c.toFixed(1) + " dB")
                                currentIndex: dialog.settings.limiterCeilings.indexOf(dialog.settings.limiterCeilingDb)
                                onActivated: (i) => dialog.settings.limiterCeilingDb = dialog.settings.limiterCeilings[i]
                            }
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("The last thing before your audio interface: a patch that is too hot or a synth that runs away "
                                       + "never reaches the sound desk louder than this. Below the ceiling your sound is untouched. "
                                       + "The LIM light on the Master strip shows when it steps in.")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        SettingsSection { title: qsTr("Running now") }
                        Label {
                            Layout.leftMargin: 20
                            Layout.fillWidth: true
                            text: dialog.settings.running
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        Item { Layout.preferredHeight: 8 }
                    }
                }

                // ------------------------------------------------ MIDI
                // Like REAPER's MIDI devices: one row per input, Mode and Channel per row.
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 8
                        SettingsSection { title: qsTr("MIDI inputs") }

                        // header
                        RowLayout {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            visible: dialog.settings.midiInputs.length > 0
                            spacing: 12
                            Label { Layout.fillWidth: true; text: qsTr("Device"); color: Theme.textDim; font.pixelSize: Theme.smallFontSize }
                            Label { Layout.preferredWidth: 140; text: qsTr("Mode"); color: Theme.textDim; font.pixelSize: Theme.smallFontSize }
                            Label { Layout.preferredWidth: 150; text: qsTr("Channel"); color: Theme.textDim; font.pixelSize: Theme.smallFontSize }
                        }
                        Repeater {
                            model: dialog.settings.midiInputs
                            delegate: RowLayout {
                                id: midiRow
                                required property var modelData
                                required property int index
                                objectName: "midiRow" + index
                                Layout.leftMargin: 20
                                Layout.rightMargin: 20
                                spacing: 12
                                Label {
                                    Layout.fillWidth: true
                                    text: midiRow.modelData.name
                                    color: midiRow.modelData.enabled ? Theme.text : Theme.textDim
                                    elide: Text.ElideRight
                                }
                                StageComboBox {
                                    Layout.preferredWidth: 140
                                    implicitWidth: 140
                                    model: [qsTr("Enabled"), qsTr("Disabled")]
                                    currentIndex: midiRow.modelData.enabled ? 0 : 1
                                    onActivated: (i) => dialog.settings.setMidiInputEnabled(midiRow.modelData.name, i === 0)
                                }
                                StageComboBox {
                                    Layout.preferredWidth: 150
                                    implicitWidth: 150
                                    enabled: midiRow.modelData.enabled
                                    model: [qsTr("All channels")].concat(Array.from({ length: 16 }, (_, c) => qsTr("Channel %1").arg(c + 1)))
                                    currentIndex: midiRow.modelData.channel
                                    onActivated: (i) => dialog.settings.setMidiInputChannel(midiRow.modelData.name, i)
                                }
                            }
                        }
                        Label {
                            Layout.leftMargin: 20
                            visible: dialog.settings.midiInputs.length === 0
                            text: qsTr("No MIDI inputs found. Plug in a keyboard — it appears here by itself.")
                            color: Theme.textDim
                        }
                        Label {
                            Layout.topMargin: 8
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("Enable only the port your keys play on. Many keyboards show a second port "
                                       + "for DAW control (the Impact GXP61's \"MIDIIN2\"): leave it disabled, or "
                                       + "every note can arrive twice. %1 remembers your choice; inputs it "
                                       + "has not seen before stay disabled.").arg(Branding.name)
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }

                        SettingsSection { title: qsTr("MIDI clock") }
                        SettingsRow {
                            label: qsTr("Send clock to")
                            StageComboBox {
                                objectName: "clockOutputBox"
                                implicitWidth: 360
                                model: [qsTr("Nowhere")].concat(dialog.settings.midiOutputs)
                                currentIndex: dialog.settings.clockOutput === "" ? 0 : dialog.settings.midiOutputs.indexOf(dialog.settings.clockOutput) + 1
                                onActivated: (i) => dialog.settings.clockOutput = i === 0 ? "" : dialog.settings.midiOutputs[i - 1]
                            }
                        }
                        CheckBox {
                            objectName: "followClock"
                            Layout.leftMargin: 14
                            text: qsTr("Follow the tempo of a MIDI clock coming in (a drum machine or DAW leads)")
                            checked: dialog.settings.followClock
                            onToggled: dialog.settings.followClock = checked
                            focusPolicy: Qt.NoFocus
                        }

                        SettingsSection { title: qsTr("Pedals and pads") }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("Change songs with your feet or a pad. Click Learn, then press the pedal, pad or "
                                       + "button. What you choose here only switches: your instruments never hear it.")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: dialog.settings.controls
                            delegate: RowLayout {
                                id: controlRow
                                required property var modelData
                                readonly property bool learning: dialog.settings.learning === modelData.action
                                objectName: "controlRow" + modelData.action
                                Layout.leftMargin: 20
                                Layout.rightMargin: 20
                                spacing: 12
                                Label {
                                    Layout.preferredWidth: 180
                                    text: controlRow.modelData.label
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: controlRow.learning ? qsTr("Press it now…")
                                        : (controlRow.modelData.trigger !== "" ? controlRow.modelData.trigger : qsTr("Not set"))
                                    color: controlRow.learning ? Theme.accent
                                         : (controlRow.modelData.trigger !== "" ? Theme.text : Theme.textDim)
                                    font.bold: controlRow.learning
                                }
                                StageButton {
                                    objectName: "learnControl"
                                    text: controlRow.learning ? qsTr("Waiting…") : qsTr("Learn")
                                    highlighted: controlRow.learning
                                    onClicked: dialog.settings.learnControl(controlRow.modelData.action)
                                }
                                StageButton {
                                    text: qsTr("Clear")
                                    enabled: controlRow.modelData.trigger !== ""
                                    onClicked: dialog.settings.clearControl(controlRow.modelData.action)
                                }
                            }
                        }
                    }
                }

                // ------------------------------------------------ Plugins
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        SettingsSection { title: qsTr("Plugin folder") }
                        Label {
                            Layout.leftMargin: 20
                            text: "C:\\Program Files\\Common Files\\VST3"
                            color: Theme.text
                        }
                        Label {
                            Layout.leftMargin: 20
                            text: qsTr("%1 plugins found (instruments and effects).").arg(dialog.pluginModel.effects().length + dialog.pluginModel.instruments().length)
                            color: Theme.textDim
                        }
                        SettingsSection {
                            visible: dialog.settings.blockedPlugins.length > 0
                            title: qsTr("Switched off after a crash")
                        }
                        Label {
                            visible: dialog.settings.blockedPlugins.length > 0
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("These plugins crashed %1 while loading, so they are not loaded (a patch using one says so). "
                                       + "Try again after updating the plugin.").arg(Branding.name)
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: dialog.settings.blockedPlugins
                            delegate: RowLayout {
                                id: blockedRow
                                required property var modelData
                                Layout.leftMargin: 20
                                Layout.rightMargin: 20
                                spacing: 12
                                Label {
                                    Layout.fillWidth: true
                                    text: blockedRow.modelData.name
                                    elide: Text.ElideRight
                                    ToolTip.visible: blockedHover.hovered
                                    ToolTip.text: blockedRow.modelData.path
                                    HoverHandler { id: blockedHover }
                                }
                                StageButton {
                                    objectName: "unblockPlugin"
                                    text: qsTr("Try again")
                                    onClicked: dialog.settings.unblockPlugin(blockedRow.modelData.path)
                                }
                            }
                        }
                        SettingsSection { title: qsTr("Hidden instruments") }
                        RowLayout {
                            Layout.leftMargin: 20
                            spacing: 12
                            StageButton {
                                text: qsTr("Show hidden instruments")
                                onClicked: dialog.pluginModel.showAll()
                            }
                            Label {
                                text: qsTr("Hide an instrument by right-clicking it in the Instruments list.")
                                color: Theme.textDim
                            }
                        }
                    }
                }
            }
        }

        // problem, if OK failed
        Label {
            objectName: "settingsError"
            Layout.fillWidth: true
            visible: dialog.settings.error !== ""
            padding: 10
            leftPadding: 16
            text: dialog.settings.error
            color: "white"
            wrapMode: Text.Wrap
            background: Rectangle { color: Theme.danger }
        }

        StageDivider { Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            StageButton {
                text: qsTr("Reset to defaults")
                onClicked: dialog.settings.resetToDefaults()
            }
            Item { Layout.fillWidth: true }
            StageButton {
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
            StageButton {
                objectName: "settingsOk"
                text: qsTr("OK")
                tone: "accent"
                implicitWidth: 90
                onClicked: if (dialog.settings.apply()) dialog.close()
            }
        }
    }
}
