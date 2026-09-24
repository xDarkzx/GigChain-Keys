import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Settings, laid out like Audacity 4's Preferences: pages on the left, the
// page on the right, Reset / Cancel / OK at the bottom. Nothing changes until
// OK; a problem stays on screen with its cause.
Popup {
    id: dialog

    required property SettingsController settings
    required property PluginListModel pluginModel

    property int page: 0

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(880, (parent ? parent.width : 880) - 32)
    height: Math.min(600, (parent ? parent.height : 600) - 32)
    padding: 0
    closePolicy: Popup.CloseOnEscape

    onAboutToShow: settings.load()

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.border
        radius: 8
    }

    Overlay.modal: Rectangle { color: "#99000000" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // title
        Label {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            leftPadding: 16
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Settings")
            font.pixelSize: 16
            font.bold: true
            color: Theme.text
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }

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
                model: [qsTr("Audio"), qsTr("MIDI"), qsTr("Plugins")]
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
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        radius: 4
                        color: dialog.page === pageRow.index ? Theme.accentBlue
                                                             : (pageRow.hovered ? Theme.slotHover : "transparent")
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: Theme.border }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: dialog.page

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
                                model: dialog.settings.asioAvailable ? [qsTr("Windows Audio (WASAPI)"), qsTr("ASIO")]
                                                                     : [qsTr("Windows Audio (WASAPI)")]
                                currentIndex: dialog.settings.driver === "asio" ? 1 : 0
                                onActivated: (i) => dialog.settings.driver = i === 1 ? "asio" : "system"
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
                ScrollView {
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 10
                        SettingsSection { title: qsTr("MIDI inputs") }
                        Label {
                            Layout.leftMargin: 20
                            visible: dialog.settings.midiInputs.length === 0
                            text: qsTr("No MIDI inputs found. Connect a keyboard and open Settings again.")
                            color: Theme.textDim
                        }
                        Repeater {
                            model: dialog.settings.midiInputs
                            delegate: RowLayout {
                                id: midiRow
                                required property var modelData
                                Layout.leftMargin: 20
                                Layout.rightMargin: 20
                                spacing: 12
                                Switch {
                                    checked: midiRow.modelData.enabled
                                    onToggled: dialog.settings.setMidiInputEnabled(midiRow.modelData.name, checked)
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: midiRow.modelData.name
                                    color: midiRow.modelData.enabled ? Theme.text : Theme.textDim
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        Label {
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            Layout.fillWidth: true
                            text: qsTr("Inputs that are on play every patch. Turn off controllers you don't want OpenStage to hear.")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
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
                        SettingsSection { title: qsTr("Hidden instruments") }
                        RowLayout {
                            Layout.leftMargin: 20
                            spacing: 12
                            Button {
                                text: qsTr("Show hidden instruments")
                                focusPolicy: Qt.NoFocus
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

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 8
            Button {
                text: qsTr("Reset to defaults")
                focusPolicy: Qt.NoFocus
                onClicked: dialog.settings.resetToDefaults()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Cancel")
                focusPolicy: Qt.NoFocus
                onClicked: dialog.close()
            }
            Button {
                objectName: "settingsOk"
                text: qsTr("OK")
                highlighted: true
                focusPolicy: Qt.NoFocus
                onClicked: if (dialog.settings.apply()) dialog.close()
            }
        }
    }
}
