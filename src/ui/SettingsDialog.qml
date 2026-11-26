pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
    // Closed while a keyboard is being taught: as it was (Save keeps it).
    onAboutToHide: settings.cancelTeaching()

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
                        CheckBox {
                            objectName: "startInPerform"
                            Layout.leftMargin: 34
                            enabled: dialog.settings.reopenLastSetlist
                            text: qsTr("…and go straight into Perform")
                            checked: dialog.settings.startInPerformMode
                            onToggled: dialog.settings.startInPerformMode = checked
                            focusPolicy: Qt.NoFocus
                        }
                        SettingsRow {
                            label: qsTr("Language")
                            StageComboBox {
                                objectName: "languageBox"
                                implicitWidth: 260
                                // [{code, name}]: the computer's, English, then each language in its own words.
                                model: dialog.settings.languages
                                textRole: "name"
                                currentIndex: dialog.settings.languages.findIndex(l => l.code === dialog.settings.language)
                                onActivated: (i) => dialog.settings.language = dialog.settings.languages[i].code
                            }
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            visible: dialog.settings.languageNeedsRestart
                            text: qsTr("The new language shows the next time %1 starts.").arg(Branding.name)
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }

                        SettingsSection { title: qsTr("Saving") }
                        SettingsRow {
                            label: qsTr("Autosave")
                            StageComboBox {
                                objectName: "autosaveMinutes"
                                implicitWidth: 210
                                model: dialog.settings.autosaveChoices.map((m) => m === 0 ? qsTr("Never")
                                                                                 : (m === 1 ? qsTr("Every minute") : qsTr("Every %1 minutes").arg(m)))
                                currentIndex: dialog.settings.autosaveChoices.indexOf(dialog.settings.autosaveMinutes)
                                onActivated: (i) => dialog.settings.autosaveMinutes = dialog.settings.autosaveChoices[i]
                            }
                            StageButton {
                                text: qsTr("Open backups…")
                                onClicked: dialog.settings.openBackupFolder()
                            }
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("A setlist with unsaved changes is saved by itself, never while you perform (going into "
                                       + "Perform saves it first). Every save also keeps the version before it: the last %1 of each "
                                       + "setlist are in the backups folder.").arg(10)
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }

                        SettingsSection { title: qsTr("Changing sounds") }
                        CheckBox {
                            objectName: "silencePreviousPatch"
                            Layout.leftMargin: 14
                            text: qsTr("Stop the previous sound at once")
                            checked: dialog.settings.silencePreviousPatch
                            onToggled: dialog.settings.silencePreviousPatch = checked
                            focusPolicy: Qt.NoFocus
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("Off (usual): when you change sound, notes you hold and reverb tails ring out under the new "
                                       + "one. On: the old sound stops the moment you change, for clean cuts between songs.")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }

                        SettingsSection { title: qsTr("Tuning and transpose") }
                        SettingsRow {
                            label: qsTr("Tuning (A4)")
                            StageSpinBox {
                                objectName: "tuningHz"
                                from: 4153
                                to: 4662
                                editable: true
                                value: Math.round(dialog.settings.tuningHz * 10)
                                textFromValue: (v) => qsTr("%1 Hz").arg((v / 10).toFixed(1))
                                valueFromText: (text) => Math.round(parseFloat(text.replace(",", ".")) * 10) // ("442,5": a decimal comma)
                                validator: RegularExpressionValidator { regularExpression: /\d{3}([.,]\d)?( ?Hz)?/ }
                                onValueModified: dialog.settings.tuningHz = value / 10
                            }
                            StageButton {
                                text: qsTr("440")
                                enabled: dialog.settings.tuningHz !== 440
                                onClicked: dialog.settings.tuningHz = 440
                            }
                        }
                        SettingsRow {
                            label: qsTr("Transpose everything")
                            StageSpinBox {
                                objectName: "globalTranspose"
                                from: -24
                                to: 24
                                value: dialog.settings.globalTranspose
                                textFromValue: (v) => v === 0 ? qsTr("Off") : qsTr("%1%2 semitones").arg(v > 0 ? "+" : "").arg(v)
                                onValueModified: dialog.settings.globalTranspose = value
                            }
                            StageButton {
                                text: qsTr("Reset")
                                enabled: dialog.settings.globalTranspose !== 0
                                onClicked: dialog.settings.globalTranspose = 0
                            }
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("Tuning: play in tune with a piano or a recording that isn't at 440 Hz. It tunes %1's own "
                                       + "instruments (Piano Pro, Saw Stack Pro, Pads Pro, the SoundFont Player); VST plugins have "
                                       + "their own tuning. Transpose: every keyboard moves together, on top of each channel's own "
                                       + "transpose, and it works at once, even mid-song. The toolbar shows it while it is on.").arg(Branding.name)
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
                        // JACK: the server's size, not the one chosen above (#83).
                        Label {
                            objectName: "bufferNote"
                            visible: text !== ""
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: dialog.settings.bufferNote
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        SettingsSection { title: qsTr("Outputs") }
                        SettingsRow {
                            label: qsTr("Click to")
                            StageComboBox {
                                objectName: "clickOutputBox"
                                implicitWidth: 260
                                model: dialog.settings.outputChoices
                                currentIndex: dialog.settings.clickOutput
                                onActivated: (i) => dialog.settings.clickOutput = i
                            }
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("With an interface that has more than two outputs, send the click to a pair of its own "
                                       + "(your in-ears) so the audience never hears it. Each channel can go to its own "
                                       + "outputs too: right-click it in the mixer > Output.")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        SettingsSection { title: qsTr("Dropouts") }
                        CheckBox {
                            objectName: "dropoutWarnings"
                            Layout.leftMargin: 14
                            text: qsTr("Tell me when the audio drops out (clicks or gaps)")
                            checked: dialog.settings.dropoutWarnings
                            onToggled: dialog.settings.dropoutWarnings = checked
                            focusPolicy: Qt.NoFocus
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("At most once a minute, with what usually fixes it (a bigger buffer above).")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        SettingsSection { title: qsTr("Screen") }
                        CheckBox {
                            objectName: "keepScreenOn"
                            Layout.leftMargin: 14
                            text: qsTr("Keep the screen on while the sound is running")
                            checked: dialog.settings.keepScreenOn
                            onToggled: dialog.settings.keepScreenOn = checked
                            focusPolicy: Qt.NoFocus
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("Your computer doesn't count playing a MIDI keyboard as using it, so mid-set the screen "
                                       + "could go dark and lock. Switch this off to let it sleep as usual (on battery, say).")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
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

                        // header (each device's name sits on its own line, over its settings)
                        RowLayout {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            visible: dialog.settings.midiInputs.length > 0
                            spacing: 12
                            Label { Layout.preferredWidth: 160; text: qsTr("Mode"); color: Theme.textDim; font.pixelSize: Theme.smallFontSize }
                            Label { Layout.preferredWidth: 130; text: qsTr("Channel"); color: Theme.textDim; font.pixelSize: Theme.smallFontSize }
                            Label { Layout.preferredWidth: 130; text: qsTr("Touch"); color: Theme.textDim; font.pixelSize: Theme.smallFontSize }
                            Item { Layout.fillWidth: true }
                        }
                        Repeater {
                            model: dialog.settings.midiInputs
                            // A device: its whole name on a line of its own (a keyboard's two ports
                            // differ only at the end, #56), its settings under it.
                            delegate: ColumnLayout {
                                id: midiDevice
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.leftMargin: 20
                                Layout.rightMargin: 20
                                Layout.topMargin: 4
                                spacing: 4
                                Label {
                                    objectName: "midiName" + midiDevice.index
                                    Layout.fillWidth: true
                                    text: midiDevice.modelData.name
                                    color: midiDevice.modelData.mode !== 2 ? Theme.text : Theme.textDim
                                    elide: Text.ElideRight
                                    // (Longer than even a whole line: all of it on hover.)
                                    HoverHandler { id: nameHover }
                                    ToolTip.visible: nameHover.hovered && truncated
                                    ToolTip.delay: 600
                                    ToolTip.text: text
                                }
                            RowLayout {
                                id: midiRow
                                readonly property var modelData: midiDevice.modelData
                                readonly property int index: midiDevice.index
                                objectName: "midiRow" + index
                                Layout.fillWidth: true
                                spacing: 12
                                StageComboBox {
                                    objectName: "midiMode" + midiRow.index
                                    Layout.preferredWidth: 160
                                    implicitWidth: 160
                                    model: [qsTr("Plays"), qsTr("Buttons and knobs"), qsTr("Off")]
                                    currentIndex: midiRow.modelData.mode
                                    onActivated: (i) => dialog.settings.setMidiInputMode(midiRow.modelData.name, i)
                                }
                                StageComboBox {
                                    Layout.preferredWidth: 130
                                    implicitWidth: 130
                                    enabled: midiRow.modelData.enabled
                                    model: [qsTr("All channels")].concat(Array.from({ length: 16 }, (_, c) => qsTr("Channel %1").arg(c + 1)))
                                    currentIndex: midiRow.modelData.channel
                                    onActivated: (i) => dialog.settings.setMidiInputChannel(midiRow.modelData.name, i)
                                }
                                // Its touch: how hard playing loud takes (MainStage's velocity scaling).
                                StageComboBox {
                                    objectName: "midiVelocity" + midiRow.index
                                    Layout.preferredWidth: 130
                                    implicitWidth: 130
                                    enabled: midiRow.modelData.enabled
                                    model: [qsTr("Normal"), qsTr("Light touch"), qsTr("Heavy touch"), qsTr("Fixed")]
                                    currentIndex: midiRow.modelData.velocity
                                    onActivated: (i) => dialog.settings.setMidiInputVelocity(midiRow.modelData.name, i)
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 600
                                    ToolTip.text: qsTr("Light touch: soft playing sounds louder. Heavy touch: it takes more to play loud. "
                                                       + "Fixed: every note equally loud (organ, synth leads).")
                                }
                                // "Learn this keyboard": a backup or borrowed one plays everything learned on yours.
                                StageButton {
                                    objectName: "teachKeyboard" + midiRow.index
                                    Layout.preferredWidth: 110
                                    text: midiRow.modelData.taught > 0 ? qsTr("Taught (%1)").arg(midiRow.modelData.taught) : qsTr("Teach…")
                                    enabled: midiRow.modelData.mode !== 2 && !dialog.settings.teaching.active
                                    onClicked: dialog.settings.startTeaching(midiRow.modelData.name)
                                }
                                Item { Layout.fillWidth: true }
                            }
                            }
                        }

                        // Teaching a keyboard: one control at a time.
                        Rectangle {
                            objectName: "teachingPanel"
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            visible: dialog.settings.teaching.active === true
                            implicitHeight: teachingColumn.implicitHeight + 24
                            radius: 6
                            color: Theme.wellTop
                            border.color: Theme.accent
                            ColumnLayout {
                                id: teachingColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 8
                                readonly property var t: dialog.settings.teaching
                                Label {
                                    text: teachingColumn.t.done
                                          ? qsTr("Done: %1 knows your controls.").arg(teachingColumn.t.keyboard)
                                          : qsTr("On %1, move or press: (%2 of %3)").arg(teachingColumn.t.keyboard)
                                                .arg(teachingColumn.t.at + 1).arg(teachingColumn.t.count)
                                    color: Theme.textDim
                                    font.pixelSize: Theme.smallFontSize
                                }
                                Label {
                                    objectName: "teachingAsk"
                                    Layout.fillWidth: true
                                    visible: !teachingColumn.t.done
                                    text: teachingColumn.t.label ?? ""
                                    color: Theme.text
                                    font.pixelSize: Theme.titleFontSize
                                    font.bold: true
                                    wrapMode: Text.Wrap
                                }
                                Label {
                                    visible: !teachingColumn.t.done
                                    text: qsTr("Learned on your keyboard as %1").arg(teachingColumn.t.learned ?? "")
                                    color: Theme.textDim
                                    font.pixelSize: Theme.smallFontSize
                                }
                                RowLayout {
                                    spacing: 8
                                    StageButton { text: qsTr("Back"); enabled: teachingColumn.t.at > 0; onClicked: dialog.settings.backTeaching() }
                                    StageButton {
                                        text: qsTr("Skip (this keyboard has none)")
                                        visible: !teachingColumn.t.done
                                        onClicked: dialog.settings.skipTeaching()
                                    }
                                    Item { Layout.fillWidth: true }
                                    StageButton { text: qsTr("Cancel"); onClicked: dialog.settings.cancelTeaching() }
                                    StageButton {
                                        objectName: "finishTeaching"
                                        text: qsTr("Save")
                                        highlighted: teachingColumn.t.done
                                        onClicked: dialog.settings.finishTeaching()
                                    }
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
                            text: qsTr("Set only the port your keys play on to Plays. Many keyboards show a second port "
                                       + "for DAW control (\"MIDIIN2\", \"DAW In\"): it is on for its Buttons and knobs "
                                       + "(Play, Stop, ◀◀ ▶▶, Loop, Click in Mackie Control mode, and anything you learn), "
                                       + "and never plays a note. A controller without keys (a nanoKONTROL) can be set "
                                       + "to Buttons and knobs too. %1 remembers your choice.").arg(Branding.name)
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("Playing another keyboard (a backup, or one at the venue)? Teach… asks for each knob and "
                                       + "button you learned, one by one: move the one on this keyboard that should do it. Then "
                                       + "every song, the mixer, the looper and your buttons work on it as on yours. It is "
                                       + "remembered: plug it in again and it just works.")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }

                        SettingsSection { title: qsTr("Note names") }
                        SettingsRow {
                            label: qsTr("Middle C is")
                            StageComboBox {
                                objectName: "middleC"
                                implicitWidth: 210
                                model: [qsTr("C4 (most keyboards)"), qsTr("C3 (Yamaha, MainStage)")]
                                currentIndex: dialog.settings.middleCOctave === 3 ? 1 : 0
                                onActivated: (i) => dialog.settings.middleCOctave = i === 1 ? 3 : 4
                            }
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

                        SettingsSection { title: qsTr("Starting the song") }
                        CheckBox {
                            objectName: "transportButtons"
                            Layout.leftMargin: 14
                            text: qsTr("My keyboard's Play and Stop buttons start and stop the song")
                            checked: dialog.settings.transportButtons
                            onToggled: dialog.settings.transportButtons = checked
                            focusPolicy: Qt.NoFocus
                        }
                        CheckBox {
                            objectName: "sustainDoubleTap"
                            Layout.leftMargin: 14
                            text: qsTr("Pressing the sustain pedal twice quickly starts and stops the song (it still sustains)")
                            checked: dialog.settings.sustainDoubleTap
                            onToggled: dialog.settings.sustainDoubleTap = checked
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
                        SettingsSection { title: qsTr("Plugin folders") }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("%1 looks for VST3 and VST2 plugins in these folders. Keep plugins on another drive "
                                       + "or in a folder of your own? Add it here.").arg(Branding.name)
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                        // The folders: the standard ones (always looked in), then the player's own.
                        Rectangle {
                            id: folderBox
                            objectName: "pluginFolderList"
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            implicitHeight: folderColumn.implicitHeight + 2
                            color: Theme.panel
                            border.color: Theme.border
                            radius: Theme.radiusSmall
                            Column {
                                id: folderColumn
                                x: 1
                                y: 1
                                width: parent.width - 2
                                Repeater {
                                    model: dialog.settings.pluginFolders
                                    delegate: Rectangle {
                                        id: folderRow
                                        required property var modelData
                                        required property int index
                                        objectName: "pluginFolder" + index
                                        width: folderColumn.width
                                        height: 44 // (Remove: a target a finger hits on a touch screen)
                                        color: folderHover.hovered ? Theme.listRowHover
                                             : (index % 2 === 0 ? Theme.listRowEven : Theme.listRowOdd)
                                        HoverHandler { id: folderHover }
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 6
                                            spacing: 10
                                            Label {
                                                Layout.fillWidth: true
                                                text: folderRow.modelData.path
                                                color: folderRow.modelData.found ? Theme.text : Theme.textDim
                                                elide: Text.ElideMiddle
                                                ToolTip.visible: folderHover.hovered && truncated
                                                ToolTip.text: folderRow.modelData.path
                                            }
                                            Label {
                                                text: !folderRow.modelData.found ? qsTr("Not found")
                                                    : (folderRow.modelData.own ? qsTr("Added by you") : qsTr("Standard"))
                                                color: !folderRow.modelData.found ? Theme.danger
                                                     : (folderRow.modelData.own ? Theme.accent : Theme.textDim)
                                                font.pixelSize: Theme.smallFontSize
                                            }
                                            StageButton {
                                                objectName: "removePluginFolder"
                                                visible: folderRow.modelData.own
                                                enabled: !pluginScan.busy
                                                text: qsTr("Remove")
                                                onClicked: pluginScan.start("remove", folderRow.modelData.path)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            spacing: 8
                            StageButton {
                                objectName: "addPluginFolder"
                                text: qsTr("Add folder…")
                                enabled: !pluginScan.busy
                                onClicked: folderPicker.open()
                            }
                            StageButton {
                                objectName: "rescanPlugins"
                                text: pluginScan.busy ? qsTr("Scanning…") : qsTr("Rescan plugins")
                                enabled: !pluginScan.busy
                                onClicked: pluginScan.start("rescan", "")
                            }
                            Label {
                                objectName: "pluginCount"
                                Layout.fillWidth: true
                                leftPadding: 6
                                text: pluginScan.busy ? qsTr("Looking for plugins… new ones take a moment each")
                                    : (pluginScan.problem !== "" ? pluginScan.problem
                                       : qsTr("%n plugin(s) found (instruments and effects).", "", dialog.settings.pluginCount))
                                color: pluginScan.problem !== "" && !pluginScan.busy ? Theme.danger : Theme.textDim
                                wrapMode: Text.Wrap
                            }
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("Installed a new plugin while %1 was open? Click Rescan plugins: no restart needed.").arg(Branding.name)
                            color: Theme.textDim
                            font.pixelSize: Theme.smallFontSize
                            wrapMode: Text.Wrap
                        }
                        FolderDialog {
                            id: folderPicker
                            title: qsTr("Choose a folder with plugins")
                            onAccepted: pluginScan.start("add", selectedFolder)
                        }
                        // A scan holds the app while new plugins are read: "Scanning…" is
                        // drawn first, then the job runs ("add", "remove" or "rescan"; its
                        // problem, if any, shown after).
                        Timer {
                            id: pluginScan
                            property string job: ""
                            property var folder // the folder added (a URL) or removed (a path)
                            property string problem: ""
                            property bool busy: false
                            function start(what, which) {
                                problem = ""
                                job = what
                                folder = which
                                busy = true
                                restart()
                            }
                            interval: 80
                            onTriggered: {
                                if (job === "add") problem = dialog.settings.addPluginFolder(folder)
                                else if (job === "remove") dialog.settings.removePluginFolder(folder)
                                else dialog.settings.rescanPlugins()
                                job = ""
                                busy = false
                            }
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
                        // Each one shown again on its own, or all at once (#84).
                        Repeater {
                            model: dialog.pluginModel.hiddenPlugins
                            delegate: RowLayout {
                                id: hiddenRow
                                required property var modelData
                                objectName: "hiddenPluginRow"
                                Layout.leftMargin: 20
                                Layout.rightMargin: 20
                                spacing: 12
                                Label {
                                    Layout.fillWidth: true
                                    text: hiddenRow.modelData.name
                                    elide: Text.ElideRight
                                }
                                StageButton {
                                    objectName: "unhidePlugin"
                                    text: qsTr("Show again")
                                    onClicked: dialog.pluginModel.unhide(hiddenRow.modelData.pluginId)
                                }
                            }
                        }
                        RowLayout {
                            Layout.leftMargin: 20
                            spacing: 12
                            StageButton {
                                objectName: "showAllHidden"
                                text: qsTr("Show all hidden instruments")
                                enabled: dialog.pluginModel.hiddenPlugins.length > 0
                                onClicked: dialog.pluginModel.showAll()
                            }
                            Label {
                                text: dialog.pluginModel.hiddenPlugins.length > 0
                                      ? qsTr("Hide an instrument by right-clicking it in the Instruments list.")
                                      : qsTr("None hidden. Hide an instrument by right-clicking it in the Instruments list.")
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
            // Applies the changes and keeps the window open, to try them (#85).
            StageButton {
                objectName: "settingsApply"
                text: qsTr("Apply")
                onClicked: dialog.settings.apply()
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
