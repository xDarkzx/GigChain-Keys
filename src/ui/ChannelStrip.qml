pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One MainStage/Logic-style channel strip. Right-click anywhere for the
// channel menu; ✕ (on hover) removes the channel.
Rectangle {
    id: strip

    required property int index
    required property string name
    required property string instrumentName
    required property var effectNames
    required property var effectBypassed
    required property double volumeDb
    required property double pan
    required property bool mute
    required property bool solo
    required property real peak
    required property bool selected
    required property string icon
    required property bool officialIcon
    required property string color
    required property int keyLow
    required property int keyHigh
    required property int transpose
    required property int velocityLow
    required property int velocityHigh
    required property int inputLeft
    required property int inputRight
    required property int outputPair // 0 the mix; n the interface's outputs 2n+1-2n+2
    required property double auxSendDb // to the aux effects; -96 = none
    required property string midiOutPort // the hardware synth it plays ("" none)
    required property int midiOutChannel
    required property int mappingCount
    required property DocumentController doc
    required property PluginListModel pluginModel
    // Clicking an effect opens its own window (floating, as in a DAW).
    property EffectWindows effectWindows: null
    // Audio input channels open now (for "Play Audio Input").
    property int inputChannels: 0
    property int outputChannels: 2 // the interface's outputs open
    // Renamed here (not on stage).
    property bool editable: true
    // For learning keyboard knobs (right-click on the fader or the pan).
    property EngineStatus engineStatus: null

    // The channel's name: the sound it plays ("Classic American Piano", "Juno
    // Pad"), where the plugin alone would say "Analog Lab V" twice. Typed
    // into its name plate.
    function startRename() {
        if (!strip.editable) return
        nameField.text = strip.name
        nameField.visible = true
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    function noteName(n) { return noteNames[n % 12] + (Math.floor(n / 12) - 1) }
    // What the strip plays when it is not the whole keyboard: "C2–B3 +12 · vel 1–64".
    readonly property string zoneText: {
        const parts = []
        if (inputLeft > 0) parts.push(inputRight > 0 ? qsTr("In %1+%2").arg(inputLeft).arg(inputRight) : qsTr("In %1").arg(inputLeft))
        if (keyLow > 0 || keyHigh < 127) parts.push(noteName(keyLow) + "–" + noteName(keyHigh))
        if (transpose !== 0) parts.push((transpose > 0 ? "+" : "") + transpose)
        if (velocityLow > 1 || velocityHigh < 127) parts.push(qsTr("vel %1–%2").arg(velocityLow).arg(velocityHigh))
        if (mappingCount > 0) parts.push(mappingCount === 1 ? qsTr("1 knob") : qsTr("%1 knobs").arg(mappingCount))
        if (outputPair > 0) parts.push(qsTr("Out %1-%2").arg(2 * outputPair + 1).arg(2 * outputPair + 2))
        if (midiOutPort !== "") parts.push(qsTr("→ %1 ch %2").arg(midiOutPort).arg(midiOutChannel))
        return parts.join(" · ")
    }

    // The strip clicked: the mixer takes the computer keyboard (Delete removes it).
    signal keysWanted()
    function takeKeys() { strip.keysWanted() }

    readonly property real peakDb: peak > 0 ? 20 * Math.log10(peak) : -200
    property int menuEffect: -1 // effect the effect menu acts on

    width: Theme.stripWidth
    radius: Theme.radiusCard
    border.color: selected ? Theme.accent : Theme.stripBorder
    border.width: selected ? 2 : 1
    gradient: Gradient {
        GradientStop { position: 0.0; color: strip.selected ? Qt.lighter(Theme.stripSelected, 1.15) : Theme.stripTop }
        GradientStop { position: 1.0; color: strip.selected ? Theme.stripSelected : Theme.stripBottom }
    }

    // The lit top edge of the strip.
    Rectangle { x: 3; y: strip.border.width; width: parent.width - 6; height: 1; color: Theme.bevelLight }

    HoverHandler { id: stripHover }

    // Why it would not sound if played now ("" = it plays): dimmed, the
    // reason on hover (muted, another soloed, not in this section, another
    // selected in a one-at-a-time sound).
    property int songSection: -1 // the section in force (the mixer passes it)
    property string silentReason: ""
    function refreshSilent() { strip.silentReason = strip.doc.silentReason(strip.index) }
    onSongSectionChanged: refreshSilent()
    onIndexChanged: refreshSilent()
    Component.onCompleted: refreshSilent()
    Connections {
        target: strip.doc
        function onChannelUpdated() { strip.refreshSilent() }
        function onChannelsChanged() { strip.refreshSilent() }
        function onSectionsChanged() { strip.refreshSilent() }
        function onSelectedChannelChanged() { strip.refreshSilent() }
        function onPlayModeChanged() { strip.refreshSilent() }
        function onCurrentChanged() { strip.refreshSilent() }
    }
    opacity: silentReason !== "" ? 0.55 : 1
    ToolTip.visible: stripHover.hovered && silentReason !== ""
    ToolTip.delay: 400
    ToolTip.text: qsTr("Silent now: %1").arg(silentReason)

    // Background: left click selects, right click opens the channel menu.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            strip.doc.selectedChannel = strip.index
            strip.takeKeys()
            if (mouse.button === Qt.RightButton) strip.menu(channelMenuComponent).popup(mouse.x, mouse.y)
        }
    }

    PayloadDropArea {
        anchors.fill: parent
        keys: ["effect"]
        onPayloadDropped: (payload) => strip.doc.addEffect(strip.index, payload.pluginId, payload.name)
    }

    // ---------------------------------------------------------------- menus
    // Each menu lists every installed plugin, so it is built the first time
    // it is opened, not when the strip is created: switching songs rebuilds
    // the strips and must stay instant.
    property var builtMenus: ({})
    function menu(component) {
        const key = component.toString()
        if (!builtMenus[key]) builtMenus[key] = component.createObject(strip)
        return builtMenus[key]
    }

    // A strip's knob slots (DocumentController.setMixerKnob): the first 16 strips have them.
    readonly property int volumeKnobSlot: strip.index < 16 ? 1 + strip.index : -1
    readonly property int panKnobSlot: strip.index < 16 ? 17 + strip.index : -1
    Component {
        id: volumeKnobMenuComponent
        KnobLearnMenu {
            objectName: "volumeKnobMenu"
            doc: strip.doc
            engineStatus: strip.engineStatus
            slot: strip.volumeKnobSlot
            what: qsTr("the volume")
        }
    }
    Component {
        id: panKnobMenuComponent
        KnobLearnMenu {
            doc: strip.doc
            engineStatus: strip.engineStatus
            slot: strip.panKnobSlot
            what: qsTr("the pan")
        }
    }

    Component {
        id: channelMenuComponent
        StageMenu {
            StageMenuItem { text: qsTr("Open %1").arg(strip.instrumentName || qsTr("instrument")); onTriggered: strip.doc.selectedChannel = strip.index }
            StageMenuItem {
                text: qsTr("Rename…")
                enabled: strip.editable
                onTriggered: strip.startRename()
            }
            StageMenuItem { text: strip.mute ? qsTr("Unmute") : qsTr("Mute"); onTriggered: strip.doc.setChannelMute(strip.index, !strip.mute) }
            StageMenuItem { text: strip.solo ? qsTr("Unsolo") : qsTr("Solo"); onTriggered: strip.doc.setChannelSolo(strip.index, !strip.solo) }
            StageMenuItem { text: qsTr("Keyboard Zone…"); onTriggered: strip.doc.editChannel(strip.index, "zone") }
            StageMenuItem { text: qsTr("Knobs…"); onTriggered: strip.doc.editChannel(strip.index, "knobs") }
            StageMenuItem {
                objectName: "learnPluginKnob"
                text: qsTr("MIDI Learn a knob of %1…").arg(strip.instrumentName || qsTr("the instrument"))
                enabled: strip.instrumentName !== ""
                onTriggered: strip.doc.editChannel(strip.index, "plugin-learn")
            }
            StageMenuItem {
                text: qsTr("MIDI Learn the volume…")
                enabled: strip.engineStatus !== null && strip.volumeKnobSlot >= 0
                onTriggered: strip.engineStatus.learnMixerKnob(strip.volumeKnobSlot)
            }
            // Where it plays: the mix, or outputs of its own (the desk, the in-ears).
            StageMenu {
                id: outputMenu
                objectName: "outputMenu"
                title: qsTr("Output")
                Instantiator {
                    model: {
                        const list = [{ pair: 0, text: qsTr("Main mix (1-2)") }]
                        for (let p = 1; 2 * p + 2 <= strip.outputChannels && p <= 7; ++p)
                            list.push({ pair: p, text: qsTr("Outputs %1-%2").arg(2 * p + 1).arg(2 * p + 2) })
                        if (strip.outputPair > 0 && 2 * strip.outputPair + 2 > strip.outputChannels)
                            list.push({ pair: strip.outputPair,
                                        text: qsTr("Outputs %1-%2 (not on this interface)").arg(2 * strip.outputPair + 1).arg(2 * strip.outputPair + 2) })
                        return list
                    }
                    delegate: StageMenuItem {
                        id: outputItem
                        required property var modelData
                        objectName: "outputChoice"
                        text: outputItem.modelData.text
                        checkable: true
                        checked: strip.outputPair === outputItem.modelData.pair
                        onTriggered: strip.doc.setChannelOutput(strip.index, outputItem.modelData.pair)
                    }
                    onObjectAdded: (index, object) => outputMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => outputMenu.removeItem(object)
                }
            }
            StageMenu {
                id: inputMenu
                title: qsTr("Play Audio Input")
                StageMenuItem {
                    text: qsTr("None (the instrument)")
                    enabled: strip.inputLeft > 0
                    onTriggered: strip.doc.setChannelInput(strip.index, 0, 0)
                }
                Instantiator {
                    // Mono inputs, then stereo pairs.
                    model: {
                        const list = []
                        for (let i = 1; i <= strip.inputChannels; ++i) list.push({ l: i, r: 0 })
                        for (let i = 1; i + 1 <= strip.inputChannels; i += 2) list.push({ l: i, r: i + 1 })
                        return list
                    }
                    delegate: StageMenuItem {
                        required property var modelData
                        text: modelData.r > 0 ? qsTr("Input %1+%2 (stereo)").arg(modelData.l).arg(modelData.r) : qsTr("Input %1").arg(modelData.l)
                        onTriggered: strip.doc.setChannelInput(strip.index, modelData.l, modelData.r)
                    }
                    onObjectAdded: (i, object) => inputMenu.insertItem(i + 1, object)
                    onObjectRemoved: (i, object) => inputMenu.removeItem(object)
                }
                StageMenuItem {
                    visible: strip.inputChannels === 0
                    text: qsTr("No inputs open: choose an input device in Settings > Audio")
                    enabled: false
                }
            }
            // A hardware synth it plays: its keys out on a MIDI output (give
            // the channel the synth's audio input above to hear it).
            StageMenu {
                id: midiOutMenu
                objectName: "midiOutMenu"
                title: qsTr("Play Hardware Synth")
                property var ports: []
                onAboutToShow: {
                    const list = strip.doc.midiOutputs()
                    if (strip.midiOutPort !== "" && list.indexOf(strip.midiOutPort) < 0) list.push(strip.midiOutPort)
                    ports = list
                }
                StageMenuItem {
                    text: qsTr("None")
                    checkable: true
                    checked: strip.midiOutPort === ""
                    onTriggered: strip.doc.setChannelMidiOut(strip.index, "", strip.midiOutChannel)
                }
                Instantiator {
                    model: midiOutMenu.ports
                    delegate: StageMenuItem {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: strip.midiOutPort === modelData
                        onTriggered: strip.doc.setChannelMidiOut(strip.index, modelData, strip.midiOutChannel)
                    }
                    onObjectAdded: (i, object) => midiOutMenu.insertItem(i + 1, object)
                    onObjectRemoved: (i, object) => midiOutMenu.removeItem(object)
                }
                StageMenuItem {
                    visible: midiOutMenu.ports.length === 0
                    text: qsTr("No MIDI outputs: plug the synth in (USB or a MIDI interface)")
                    enabled: false
                }
                StageMenu {
                    id: midiOutChannelMenu
                    title: qsTr("On MIDI Channel")
                    enabled: strip.midiOutPort !== ""
                    Instantiator {
                        model: 16
                        delegate: StageMenuItem {
                            required property int index
                            text: index + 1
                            checkable: true
                            checked: strip.midiOutChannel === index + 1
                            onTriggered: strip.doc.setChannelMidiOut(strip.index, strip.midiOutPort, index + 1)
                        }
                        onObjectAdded: (i, object) => midiOutChannelMenu.insertItem(i, object)
                        onObjectRemoved: (i, object) => midiOutChannelMenu.removeItem(object)
                    }
                }
            }
            EffectPickerMenu {
                title: qsTr("Add Effect")
                pluginModel: strip.pluginModel
                onPicked: (pluginId, name) => strip.doc.addEffect(strip.index, pluginId, name)
            }
            InstrumentPickerMenu {
                title: qsTr("Replace Instrument")
                pluginModel: strip.pluginModel
                onPicked: (pluginId, name) => strip.doc.setChannelInstrument(strip.index, pluginId, name)
            }
            MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
            StageMenuItem { text: qsTr("Remove Channel"); onTriggered: strip.doc.removeChannel(strip.index) }
        }
    }

    Component {
        id: effectMenuComponent
        StageMenu {
            StageMenuItem {
                text: strip.menuEffect >= 0 && strip.effectBypassed[strip.menuEffect] ? qsTr("Turn On") : qsTr("Bypass")
                onTriggered: strip.doc.setEffectBypass(strip.index, strip.menuEffect, !strip.effectBypassed[strip.menuEffect])
            }
            EffectPickerMenu {
                title: qsTr("Replace With")
                pluginModel: strip.pluginModel
                onPicked: (pluginId, name) => strip.doc.replaceEffect(strip.index, strip.menuEffect, pluginId, name)
            }
            MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
            StageMenuItem { text: qsTr("Remove Effect"); onTriggered: strip.doc.removeEffect(strip.index, strip.menuEffect) }
        }
    }

    Component {
        id: addEffectMenuComponent
        EffectPickerMenu {
            pluginModel: strip.pluginModel
            onPicked: (pluginId, name) => strip.doc.addEffect(strip.index, pluginId, name)
        }
    }

    Component {
        id: instrumentPickerComponent
        InstrumentPickerMenu {
            pluginModel: strip.pluginModel
            onPicked: (pluginId, name) => strip.doc.setChannelInstrument(strip.index, pluginId, name)
        }
    }

    // ---------------------------------------------------------------- layout
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 3

        // colour tag with the ✕ remove button
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 12
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 16
                height: 3
                radius: 1.5
                color: strip.color
            }
            Text {
                objectName: "removeChannelButton"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: stripHover.hovered
                text: "✕"
                color: closeArea.containsMouse ? Theme.danger : Theme.textDim
                font.pixelSize: 11
                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    onClicked: strip.doc.removeChannel(strip.index)
                }
                ToolTip.visible: closeArea.containsMouse
                ToolTip.text: qsTr("Remove this channel")
            }
        }

        // The sound: its icon and the instrument (or audio input) it plays.
        StripSection {
            Layout.fillWidth: true
            label: strip.inputLeft > 0 ? qsTr("INPUT") : qsTr("INSTRUMENT")
        // instrument icon
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: 17
            color: Qt.darker(strip.color, 2.2)
            border.color: strip.color
            clip: true
            Image {
                anchors.centerIn: parent
                source: strip.icon
                width: strip.officialIcon ? 32 : 20
                height: width
                sourceSize: Qt.size(64, 64)
                fillMode: Image.PreserveAspectCrop
                smooth: true
            }
        }

        // instrument slot
        EffectSlot {
            id: instrumentSlot
            objectName: "instrumentSlot"
            Layout.fillWidth: true
            implicitHeight: 22
            text: strip.instrumentName
            showPower: false
            loadedColor: Theme.slotInstrument
            onClicked: {
                strip.doc.selectedChannel = strip.index
                strip.takeKeys()
                if (!loaded) strip.menu(instrumentPickerComponent).popup(instrumentSlot, 0, instrumentSlot.height)
            }
            onMenuRequested: strip.menu(channelMenuComponent).popup(instrumentSlot, 0, instrumentSlot.height)
        }
        }

        StripSection {
            Layout.fillWidth: true
            label: qsTr("AUDIO FX")
        // effect slots, then one empty slot to add another. The list grows
        // with each effect; past four it scrolls, so the fader keeps its room.
        ListView {
            id: effectList
            objectName: "effectList"
            readonly property int slotHeight: 20
            readonly property int maxVisible: 4
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(count, maxVisible) * (slotHeight + spacing) - (count > 0 ? spacing : 0)
            visible: count > 0
            spacing: 3
            clip: true
            interactive: count > maxVisible
            boundsBehavior: Flickable.StopAtBounds
            model: strip.effectNames
            // A new effect is added at the end: show it.
            onCountChanged: positionViewAtEnd()
            ScrollBar.vertical: ScrollBar {
                policy: effectList.count > effectList.maxVisible ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                width: 3
            }
            delegate: EffectSlot {
                id: fxSlot
                required property int index
                required property string modelData
                width: ListView.view.width - (effectList.count > effectList.maxVisible ? 4 : 0)
                height: effectList.slotHeight
                text: modelData
                bypassed: strip.effectBypassed[index] === true
                onClicked: {
                    strip.doc.selectedChannel = strip.index
                    if (strip.effectWindows) strip.effectWindows.open(strip.index, index, fxSlot.Window.window)
                }
                onPowerToggled: strip.doc.setEffectBypass(strip.index, index, !bypassed)
                onMenuRequested: {
                    strip.menuEffect = index
                    strip.menu(effectMenuComponent).popup(fxSlot, 0, fxSlot.height)
                }
            }
        }
        EffectSlot {
            id: addSlot
            Layout.fillWidth: true
            text: ""
            onClicked: strip.menu(addEffectMenuComponent).popup(addSlot, 0, addSlot.height)
        }
        }

        // The send to the aux effects (the Aux strip's reverb or delay) and the pan, side by side.
        StripSection {
            Layout.fillWidth: true
            label: qsTr("SEND · PAN")
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 2
        SendKnob {
            objectName: "sendKnob"
            Layout.alignment: Qt.AlignVCenter
            sendDb: strip.auxSendDb
            onSendMoved: (db) => strip.doc.setChannelSend(strip.index, db)
        }

        PanKnob {
            objectName: "panKnob"
            Layout.alignment: Qt.AlignVCenter
            pan: strip.pan
            onPanMoved: (v) => strip.doc.setChannelPan(strip.index, v)
            // Right-click: learn the keyboard knob that turns it.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                enabled: strip.engineStatus !== null
                onClicked: (mouse) => {
                    const at = mapToItem(strip, mouse.x, mouse.y) // (the menu opens where it was clicked)
                    strip.menu(panKnobMenuComponent).popup(at.x, at.y)
                }
            }
        }
        }
        }

        // The level: its readouts, the fader with its meter, mute and solo.
        StripSection {
            Layout.fillWidth: true
            Layout.fillHeight: true
        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            Readout {
                objectName: "volumeReadout"
                Layout.fillWidth: true
                text: strip.volumeDb.toFixed(1)
                editable: true
                onVolumeTyped: (db) => strip.doc.setChannelVolume(strip.index, db)
            }
            Readout {
                Layout.fillWidth: true
                text: strip.peakDb < -99 ? "-∞" : strip.peakDb.toFixed(1)
                alarm: strip.peakDb > 0
            }
        }

        VolumeFader {
            objectName: "channelFader"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 90
            volumeDb: strip.volumeDb
            level: strip.peak
            onVolumeMoved: (db) => strip.doc.setChannelVolume(strip.index, db)
            // Right-click: learn the keyboard knob or fader that moves it.
            MouseArea {
                objectName: "faderKnobArea"
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                enabled: strip.engineStatus !== null
                onClicked: (mouse) => {
                    const at = mapToItem(strip, mouse.x, mouse.y) // (the menu opens where it was clicked)
                    strip.menu(volumeKnobMenuComponent).popup(at.x, at.y)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 3
            ToggleChip {
                Layout.fillWidth: true
                text: "M"
                active: strip.mute
                activeColor: Theme.muteColor
                onClicked: strip.doc.setChannelMute(strip.index, !strip.mute)
            }
            ToggleChip {
                Layout.fillWidth: true
                text: "S"
                active: strip.solo
                activeColor: Theme.soloColor
                onClicked: strip.doc.setChannelSolo(strip.index, !strip.solo)
            }
        }
        }

        // where it plays, when not the whole keyboard (click to change)
        Text {
            objectName: "zoneText"
            Layout.fillWidth: true
            visible: strip.zoneText !== ""
            text: strip.zoneText
            color: Theme.textDim
            font.pixelSize: Theme.tinyFontSize
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            MouseArea {
                anchors.fill: parent
                onClicked: strip.doc.editChannel(strip.index, "zone")
            }
        }

        // name tag: a coloured plate, lit from above; double-click to rename
        Rectangle {
            objectName: "stripNamePlate"
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            radius: Theme.radiusSmall
            border.color: Theme.outline
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.lighter(strip.color, 1.2) }
                GradientStop { position: 1.0; color: Qt.darker(strip.color, 1.3) }
            }
            Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: "#40ffffff" }
            Text {
                objectName: "stripName"
                anchors.fill: parent
                anchors.margins: 3
                visible: !nameField.visible
                text: strip.name
                color: "white"
                font.pixelSize: Theme.smallFontSize
                font.bold: true
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            HoverHandler { id: plateHover; enabled: strip.editable }
            ToolTip.visible: plateHover.hovered && !nameField.visible
            ToolTip.delay: 600
            ToolTip.text: strip.instrumentName !== "" && strip.instrumentName !== strip.name
                          ? qsTr("%1 · %2 (double-click to rename)").arg(strip.name).arg(strip.instrumentName)
                          : qsTr("%1 (double-click to rename it after the sound it plays)").arg(strip.name)
            TapHandler {
                onTapped: {
                    strip.doc.selectedChannel = strip.index
                    strip.takeKeys()
                }
                onDoubleTapped: strip.startRename()
            }
            StageTextField {
                id: nameField
                objectName: "stripNameField"
                anchors.fill: parent
                visible: false
                font.pixelSize: Theme.smallFontSize
                // Return or Enter saves it. The key stops here: on a Mac, Return
                // is also the mixer's "rename", which would open the field again.
                function commit() {
                    if (text.trim() !== "" && text.trim() !== strip.name) strip.doc.setChannelName(strip.index, text.trim())
                    visible = false
                    strip.takeKeys()
                }
                Keys.onReturnPressed: (event) => {
                    event.accepted = true
                    nameField.commit()
                }
                Keys.onEnterPressed: (event) => {
                    event.accepted = true
                    nameField.commit()
                }
                onActiveFocusChanged: if (!activeFocus) visible = false
                Keys.onEscapePressed: {
                    visible = false
                    strip.takeKeys()
                }
            }
        }
    }
}
