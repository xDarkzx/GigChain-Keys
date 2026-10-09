pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The Logic-style mixer along the bottom: one strip per channel of the
// current patch, then the master strip. Shown in Edit and Perform mode.
Rectangle {
    id: mixer

    required property DocumentController doc
    required property ChannelModel channelModel
    required property PluginListModel pluginModel
    required property EngineStatus engineStatus
    property EffectWindows effectWindows: null
    property MasterBus masterBus: null
    property MasterBus auxBus: null // the shared effects the Send knobs feed
    property LoopController loops: null
    // Channels may be removed from the computer keyboard (false on stage).
    property bool editable: true

    // The console's floor: darker at the bottom, strips standing on it.
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.panelBottom }
        GradientStop { position: 1.0; color: Theme.mixerBackground }
    }

    PayloadDropArea {
        objectName: "mixerDrop"
        anchors.fill: parent
        keys: ["instrument"]
        onPayloadDropped: (payload) => mixer.doc.addChannel(payload.pluginId, payload.name)
    }

    StageDivider { anchors.top: parent.top; width: parent.width }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: 4

        // The loop station: a strip right across the mixer, its name in
        // the middle, and a loop's buttons above each channel (scrolling
        // with the strips).
        Rectangle {
            id: looperBand
            objectName: "looperBand"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.looperHeight - 4 // (the gap under it: the column's spacing)
            visible: mixer.loops !== null && mixer.loops.stripVisible
            radius: Theme.radiusCard
            border.color: Theme.outline
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.panelTop }
                GradientStop { position: 1.0; color: Theme.panelBottom }
            }
            Rectangle { x: 2; y: 1; width: parent.width - 4; height: 1; color: Theme.bevelLight }

            // Its name, engraved between two grooves, like a hardware unit.
            RowLayout {
                id: looperTitle
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Theme.spacingLarge
                anchors.rightMargin: Theme.spacingLarge
                anchors.topMargin: 4
                height: 14
                spacing: Theme.spacing
                StageDivider { Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter }
                Text {
                    objectName: "looperTitle"
                    text: qsTr("LOOP STATION")
                    color: Theme.textDim
                    font.pixelSize: Theme.tinyFontSize
                    font.bold: true
                    font.letterSpacing: 3
                }
                StageDivider { Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter }
            }

            ListView {
                id: looperCells
                objectName: "looperCells"
                anchors.left: parent.left
                anchors.top: looperTitle.bottom
                anchors.bottom: parent.bottom
                anchors.topMargin: 3
                anchors.bottomMargin: 4
                width: strips.width // over the strips exactly
                orientation: ListView.Horizontal
                spacing: strips.spacing
                interactive: false
                clip: true
                contentX: strips.contentX
                // One per channel, as the strips (a loop's state changing
                // many times a second must not rebuild the cells).
                model: mixer.channelModel
                delegate: LooperCell {
                    required property int index
                    loop: mixer.loops !== null && index < mixer.loops.channelLoops.length ? mixer.loops.channelLoops[index] : undefined
                    selected: mixer.doc.selectedChannel === index
                    loopLength: mixer.doc.songLoopSync ? mixer.doc.songLoopBars : 0
                    onRecordPressed: mixer.loops.record(index)
                    onPlayStopPressed: mixer.loops.playStop(index)
                    onUndoRequested: mixer.loops.undo(index)
                    onClearRequested: mixer.loops.clear(index)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing

        ListView {
            id: strips
            objectName: "mixerStrips"
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            spacing: 4
            clip: true
            model: mixer.channelModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar {}
            keyNavigationEnabled: false // (the arrows change sounds and songs: Main's shortcuts)
            // A strip clicked: Delete (or Backspace) removes its channel, F2
            // (on a Mac also Return) renames it. Not on stage.
            Keys.onPressed: (event) => {
                const channel = mixer.doc.selectedChannel
                if (!mixer.editable || channel < 0) return
                if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                    event.accepted = true
                    mixer.doc.removeChannel(channel)
                } else if (event.key === Qt.Key_F2 || (Theme.mac && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter))) {
                    event.accepted = true
                    const selected = strips.itemAtIndex(channel) as ChannelStrip
                    if (selected !== null) selected.startRename()
                }
            }
            delegate: ChannelStrip {
                onKeysWanted: strips.forceActiveFocus()
                editable: mixer.editable
                engineStatus: mixer.engineStatus
                songSection: mixer.engineStatus.songSection
                height: Math.min(ListView.view.height, Theme.stripHeight)
                doc: mixer.doc
                pluginModel: mixer.pluginModel
                effectWindows: mixer.effectWindows
                inputChannels: mixer.engineStatus.audioInputChannels
                outputChannels: mixer.engineStatus.audioOutputChannels
            }
            footer: Item {
                width: Theme.stripWidth + 8
                height: Math.min(strips.height, Theme.stripHeight)
                // Add a channel: pick an instrument (grouped by maker)
                EffectSlot {
                    id: newChannelSlot
                    objectName: "addInstrumentChannel"
                    x: 4
                    width: Theme.stripWidth
                    height: parent.height - newInputSlot.height - 4
                    text: ""
                    onClicked: {
                        newChannelPicker.shared = mixer.doc.otherSongsInstruments() // as the setlist is now
                        newChannelPicker.popup(newChannelSlot, newChannelSlot.width / 2, newChannelSlot.height / 2)
                    }
                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: 18
                        text: qsTr("Instrument")
                        color: Theme.textDim
                        font.pixelSize: Theme.smallFontSize
                    }
                }
                InstrumentPickerMenu {
                    id: newChannelPicker
                    pluginModel: mixer.pluginModel
                    onPicked: (pluginId, name) => mixer.doc.addChannel(pluginId, name)
                    onPickedShared: (song, patch, channel) => mixer.doc.addSharedChannel(song, patch, channel)
                }
                // Add a channel playing an audio input (a mic, a guitar).
                EffectSlot {
                    id: newInputSlot
                    objectName: "addInputChannel"
                    x: 4
                    y: parent.height - height
                    width: Theme.stripWidth
                    height: 44
                    text: "" // the empty-slot look, as the instrument slot above
                    onClicked: newInputMenu.popup(newInputSlot, 0, 0)
                    Text {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: 12
                        text: qsTr("Audio input")
                        color: Theme.textDim
                        font.pixelSize: Theme.smallFontSize
                    }
                }
                StageMenu {
                    id: newInputMenu
                    Instantiator {
                        model: {
                            const list = []
                            const n = mixer.engineStatus.audioInputChannels
                            for (let i = 1; i <= n; ++i) list.push({ l: i, r: 0 })
                            for (let i = 1; i + 1 <= n; i += 2) list.push({ l: i, r: i + 1 })
                            return list
                        }
                        delegate: StageMenuItem {
                            required property var modelData
                            text: modelData.r > 0 ? qsTr("Input %1+%2 (stereo)").arg(modelData.l).arg(modelData.r) : qsTr("Input %1").arg(modelData.l)
                            onTriggered: mixer.doc.addInputChannel(modelData.l, modelData.r)
                        }
                        onObjectAdded: (i, object) => newInputMenu.insertItem(i, object)
                        onObjectRemoved: (i, object) => newInputMenu.removeItem(object)
                    }
                    StageMenuItem {
                        visible: mixer.engineStatus.audioInputChannels === 0
                        text: qsTr("No inputs open: choose an input device in Settings > Audio")
                        enabled: false
                    }
                }
            }
        }

        // Aux strip: the rig's shared effects (a reverb, a delay) that each
        // channel's Send knob feeds; what comes back is mixed in before the
        // master. Kept with the rig, as the master's.
        Rectangle {
            id: auxStrip
            objectName: "auxStrip"
            visible: mixer.auxBus !== null
            Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: Math.min(strips.height, Theme.stripHeight)
            Layout.preferredWidth: Theme.stripWidth
            radius: Theme.radiusCard
            border.color: Theme.stripBorder
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.stripTop }
                GradientStop { position: 1.0; color: Theme.stripBottom }
            }
            Rectangle { x: 3; y: 1; width: parent.width - 6; height: 1; color: Theme.bevelLight }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 3
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: Theme.accentBlue }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("AUX")
                    color: Theme.textDim
                    font.pixelSize: Theme.tinyFontSize
                    font.bold: true
                    font.letterSpacing: 2
                }
                StripSection {
                    Layout.fillWidth: true
                    label: qsTr("AUX FX")
                    BusEffectList {
                        objectName: "auxEffects"
                        Layout.fillWidth: true
                        bus: mixer.auxBus
                        pluginModel: mixer.pluginModel
                        addHint: qsTr("Add a shared effect (reverb, delay). Turn up a channel's Send knob to feed it.")
                    }
                }
                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Fed by the channels' Send knobs")
                    color: Theme.textDim
                    font.pixelSize: Theme.tinyFontSize
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: 4
                    text: qsTr("Aux")
                    color: Theme.text
                    font.pixelSize: Theme.smallFontSize
                }
            }
        }

        // Master strip: the rig's own effects on everything (not saved in the
        // setlist), the master fader, mute, and the safety limiter's light.
        Rectangle {
            id: masterStrip
            property int menuEffect: -1
            Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: Math.min(strips.height, Theme.stripHeight)
            Layout.preferredWidth: Theme.stripWidth
            radius: Theme.radiusCard
            border.color: Theme.stripBorder
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.stripTop }
                GradientStop { position: 1.0; color: Theme.stripBottom }
            }
            Rectangle { x: 3; y: 1; width: parent.width - 6; height: 1; color: Theme.bevelLight }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 3
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 3; radius: 1.5; color: Theme.text }
                // Mute everything (the volume is kept for unmute).
                Rectangle {
                    id: masterMute
                    objectName: "masterMuteButton"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                    radius: 17
                    readonly property bool muted: mixer.engineStatus.masterMuted
                    color: muted ? Theme.danger : (muteArea.containsMouse ? Theme.slotHover : "transparent")
                    Image {
                        anchors.centerIn: parent
                        source: "icons/volume.svg"
                        sourceSize: Qt.size(22, 22)
                        opacity: masterMute.muted ? 1.0 : 0.85
                    }
                    Rectangle { // the strike-through when muted
                        visible: masterMute.muted
                        anchors.centerIn: parent
                        width: 26; height: 2.5; radius: 1
                        rotation: -45
                        color: "white"
                    }
                    MouseArea {
                        id: muteArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: mixer.engineStatus.masterMuted = !mixer.engineStatus.masterMuted
                    }
                    ToolTip.visible: muteArea.containsMouse
                    ToolTip.text: masterMute.muted ? qsTr("Muted: click to hear everything again") : qsTr("Mute everything")
                }
                // master effect slots, then one empty slot to add another
                StripSection {
                    Layout.fillWidth: true
                    label: qsTr("MASTER FX")
                ListView {
                    id: masterEffects
                    objectName: "masterEffectList"
                    readonly property int slotHeight: 20
                    readonly property int maxVisible: 4
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(count, maxVisible) * (slotHeight + spacing) - (count > 0 ? spacing : 0)
                    visible: count > 0
                    spacing: 3
                    clip: true
                    interactive: count > maxVisible
                    boundsBehavior: Flickable.StopAtBounds
                    model: mixer.masterBus ? mixer.masterBus.effectNames : []
                    onCountChanged: positionViewAtEnd()
                    ScrollBar.vertical: ScrollBar {
                        policy: masterEffects.count > masterEffects.maxVisible ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        width: 3
                    }
                    delegate: EffectSlot {
                        id: masterSlot
                        required property int index
                        required property string modelData
                        width: ListView.view.width - (masterEffects.count > masterEffects.maxVisible ? 4 : 0)
                        height: masterEffects.slotHeight
                        text: modelData
                        bypassed: mixer.masterBus.effectBypassed[index] === true
                        onClicked: mixer.masterBus.openEffect(index, masterSlot.Window.window)
                        onPowerToggled: mixer.masterBus.setEffectBypass(index, !bypassed)
                        onMenuRequested: {
                            masterStrip.menuEffect = index
                            masterEffectMenu.popup(masterSlot, 0, masterSlot.height)
                        }
                    }
                }
                EffectSlot {
                    id: masterAddSlot
                    objectName: "masterAddEffect"
                    Layout.fillWidth: true
                    visible: mixer.masterBus !== null
                    text: ""
                    onClicked: {
                        if (!masterAddMenu) masterAddMenu = masterAddMenuComponent.createObject(masterStrip)
                        masterAddMenu.popup(masterAddSlot, 0, masterAddSlot.height)
                    }
                    property var masterAddMenu: null
                    property var replaceMenu: null
                    HoverHandler { id: masterAddHover }
                    ToolTip.visible: masterAddHover.hovered
                    ToolTip.text: qsTr("Add an effect on everything (EQ, compressor, limiter). Kept with your rig, not the setlist.")
                }
                }
                Component {
                    id: masterAddMenuComponent
                    EffectPickerMenu {
                        pluginModel: mixer.pluginModel
                        onPicked: (pluginId, name) => mixer.masterBus.addEffect(pluginId, name)
                    }
                }
                StageMenu {
                    id: masterEffectMenu
                    StageMenuItem {
                        text: masterStrip.menuEffect >= 0 && mixer.masterBus && mixer.masterBus.effectBypassed[masterStrip.menuEffect]
                              ? qsTr("Turn On") : qsTr("Bypass")
                        onTriggered: mixer.masterBus.setEffectBypass(masterStrip.menuEffect, !mixer.masterBus.effectBypassed[masterStrip.menuEffect])
                    }
                    StageMenuItem {
                        text: qsTr("Replace With…")
                        onTriggered: {
                            if (!masterAddSlot.replaceMenu) masterAddSlot.replaceMenu = masterReplaceMenuComponent.createObject(masterStrip)
                            masterAddSlot.replaceMenu.popup(masterAddSlot, 0, masterAddSlot.height)
                        }
                    }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
                    StageMenuItem { text: qsTr("Remove Effect"); onTriggered: mixer.masterBus.removeEffect(masterStrip.menuEffect) }
                }
                Component {
                    id: masterReplaceMenuComponent
                    EffectPickerMenu {
                        pluginModel: mixer.pluginModel
                        onPicked: (pluginId, name) => mixer.masterBus.replaceEffect(masterStrip.menuEffect, pluginId, name)
                    }
                }

                // The level: what goes out, the fader with its meter, the limiter's light.
                StripSection {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                Readout {
                    objectName: "masterVolumeReadout"
                    Layout.fillWidth: true
                    text: mixer.engineStatus.masterVolumeDb.toFixed(1)
                    editable: true
                    onVolumeTyped: (db) => mixer.engineStatus.masterVolumeDb = db
                }
                VolumeFader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    objectName: "masterFader"
                    volumeDb: mixer.engineStatus.masterVolumeDb
                    level: mixer.engineStatus.masterPeak
                    onVolumeMoved: (db) => mixer.engineStatus.masterVolumeDb = db
                    // Right-click: learn the keyboard knob or fader that moves it.
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: (mouse) => masterKnobMenu.popup(mouse.x, mouse.y) // (where it was clicked)
                    }
                    KnobLearnMenu {
                        id: masterKnobMenu
                        objectName: "masterKnobMenu"
                        doc: mixer.doc
                        engineStatus: mixer.engineStatus
                        slot: 0
                        what: qsTr("the master")
                    }
                }
                // The safety limiter caught a peak: the output is running hot.
                Rectangle {
                    objectName: "limiterLight"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 16
                    radius: 3
                    color: mixer.engineStatus.limiting ? Theme.meterHigh : Theme.readoutBackground
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("LIM")
                        color: mixer.engineStatus.limiting ? "white" : Theme.textDim
                        font.pixelSize: 9
                        font.bold: true
                    }
                    HoverHandler { id: limHover }
                    ToolTip.visible: limHover.hovered
                    ToolTip.text: qsTr("Lights when the safety limiter stops a peak going past its ceiling (Settings > Audio). Often lit: turn something down.")
                }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    radius: Theme.radiusSmall
                    border.color: Theme.outline
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.buttonHoverTop }
                        GradientStop { position: 1.0; color: Theme.buttonBottom }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Master")
                        color: Theme.text
                        font.pixelSize: Theme.smallFontSize
                        font.bold: true
                    }
                }
            }
        }
        }
    }
}
