pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Sustain Pads' channel strip in the mixer, as a channel's: its INSTRUMENT
// (Pads Pro, or a VST dragged onto it from the browser, or picked: click it
// for its window or Pads Pro's pads), its AUDIO FX (a reverb, a delay, an
// EQ... dragged on or added; kept with the rig), its fader with the meter,
// mute, and where it plays. Lit while the pad sounds.
Rectangle {
    id: strip
    objectName: "sustainPadsStrip"

    required property SustainPadsController pads
    required property MasterBus bus
    required property PluginListModel pluginModel
    property SettingsController settings: null // its outputs (null: the mix only)
    property real stripCap: Theme.stripHeight

    readonly property bool vst: pads.instrumentId !== ""

    Layout.alignment: Qt.AlignTop
    Layout.fillHeight: true
    Layout.maximumHeight: stripCap
    Layout.preferredWidth: Theme.stripWidth
    radius: Theme.radiusCard
    border.color: pads.active ? Theme.ledGreen : Theme.stripBorder
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.stripTop }
        GradientStop { position: 1.0; color: Theme.stripBottom }
    }
    Rectangle { x: 3; y: 1; width: parent.width - 6; height: 1; color: Theme.bevelLight }

    // An instrument dropped on it plays the pad; an effect goes in its FX.
    PayloadDropArea {
        objectName: "sustainPadsDrop"
        anchors.fill: parent
        keys: ["instrument", "effect"]
        onPayloadDropped: (payload) => {
            if (payload.kind === "effect") strip.bus.addEffect(payload.pluginId, payload.name)
            else strip.load(payload.pluginId, payload.name)
        }
    }
    function load(pluginId, name) {
        if (pluginId === "builtin:pads-pro") strip.pads.usePadsPro()
        else if (strip.pads.loadInstrument(pluginId, name) && strip.vst) strip.pads.openInstrumentWindow(strip.Window.window) // (to choose its sound)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 3
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: strip.pads.active ? Theme.ledGreen : Theme.ledOff }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("PADS")
            color: Theme.textDim
            font.pixelSize: Theme.tinyFontSize
            font.bold: true
            font.letterSpacing: 2
        }

        StripSection {
            Layout.fillWidth: true
            label: qsTr("INSTRUMENT")
            EffectSlot {
                id: instrumentSlot
                objectName: "sustainInstrumentSlot"
                Layout.fillWidth: true
                implicitHeight: 22
                text: strip.pads.instrumentName
                showPower: false
                loadedColor: Theme.slotInstrument
                onClicked: strip.vst ? strip.pads.openInstrumentWindow(strip.Window.window)
                                     : padsMenu.popup(instrumentSlot, 0, instrumentSlot.height)
                onMenuRequested: sourceMenu.popup(instrumentSlot, 0, instrumentSlot.height)
                HoverHandler { id: instrumentHover }
                ToolTip.visible: instrumentHover.hovered
                ToolTip.delay: 700
                ToolTip.text: strip.vst ? qsTr("%1 plays the pad: click for its window, right-click to change it").arg(strip.pads.instrumentName)
                                        : qsTr("Pads Pro plays the pad: click to choose its pad. Drag a VST instrument here to play it instead")
            }
            // Pads Pro's pads.
            StageMenu {
                id: padsMenu
                Instantiator {
                    model: strip.pads.presetNames
                    delegate: StageMenuItem {
                        id: padItem
                        required property string modelData
                        required property int index
                        text: padItem.modelData
                        checkable: true
                        checked: strip.pads.preset === padItem.index
                        onTriggered: strip.pads.preset = padItem.index
                    }
                    onObjectAdded: (i, object) => padsMenu.insertItem(i, object)
                    onObjectRemoved: (i, object) => padsMenu.removeItem(object)
                }
            }
            // What plays it.
            StageMenu {
                id: sourceMenu
                StageMenuItem {
                    objectName: "sustainUsePadsPro"
                    text: qsTr("Pads Pro (built in)")
                    checkable: true
                    checked: !strip.vst
                    onTriggered: strip.pads.usePadsPro()
                }
                StageMenuItem {
                    objectName: "sustainLoadVst"
                    text: strip.vst ? qsTr("Another VST instrument…") : qsTr("A VST instrument…")
                    onTriggered: vstPicker.popup(instrumentSlot, 0, instrumentSlot.height)
                }
                StageMenuItem {
                    visible: strip.vst
                    height: visible ? implicitHeight : 0
                    text: qsTr("Open %1's window").arg(strip.pads.instrumentName)
                    onTriggered: strip.pads.openInstrumentWindow(strip.Window.window)
                }
            }
            InstrumentPickerMenu {
                id: vstPicker
                pluginModel: strip.pluginModel
                onPicked: (pluginId, name) => strip.load(pluginId, name)
            }
        }

        StripSection {
            Layout.fillWidth: true
            label: qsTr("AUDIO FX")
            BusEffectList {
                objectName: "sustainPadsEffects"
                Layout.fillWidth: true
                bus: strip.bus
                pluginModel: strip.pluginModel
                addHint: qsTr("Add an effect on the pad (a reverb, a delay, an EQ). Kept with your rig.")
            }
        }

        // The fader and its meter.
        VolumeFader {
            objectName: "sustainPadsFader"
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            minDb: -60
            maxDb: 0
            volumeDb: strip.pads.volumeDb
            level: strip.pads.muted ? 0 : strip.pads.peak
            onVolumeMoved: (db) => strip.pads.volumeDb = db
        }

        // Mute, and where it plays.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 2
            StageButton {
                objectName: "sustainPadsMute"
                implicitWidth: 28
                text: "M"
                checked: strip.pads.muted
                tone: strip.pads.muted ? "danger" : "normal"
                tip: strip.pads.muted ? qsTr("Muted: click to hear the pad again") : qsTr("Mute the pad")
                onClicked: strip.pads.muted = !strip.pads.muted
            }
            StageButton {
                id: outputButton
                objectName: "sustainPadsOutput"
                visible: strip.settings !== null && strip.settings.outputChoices.length > 1
                implicitWidth: 44
                text: strip.pads.outputPair === 0 ? qsTr("Mix") : String(2 * strip.pads.outputPair + 1) + "-" + String(2 * strip.pads.outputPair + 2)
                tip: qsTr("Where the pad plays: the mix, or outputs of its own")
                onClicked: outputMenu.popup(outputButton, 0, outputButton.height)
                StageMenu {
                    id: outputMenu
                    Instantiator {
                        model: strip.settings !== null ? strip.settings.outputChoices : []
                        delegate: StageMenuItem {
                            id: outputItem
                            required property string modelData
                            required property int index
                            text: outputItem.modelData
                            checkable: true
                            checked: strip.pads.outputPair === outputItem.index
                            onTriggered: strip.pads.outputPair = outputItem.index
                        }
                        onObjectAdded: (i, object) => outputMenu.insertItem(i, object)
                        onObjectRemoved: (i, object) => outputMenu.removeItem(object)
                    }
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 4
            text: qsTr("Sustain Pads")
            color: Theme.text
            font.pixelSize: Theme.smallFontSize
        }
    }
}
