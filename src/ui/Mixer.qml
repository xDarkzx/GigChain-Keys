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

    color: Theme.mixerBackground

    DropArea {
        anchors.fill: parent
        keys: ["instrument"]
        function acceptDrop(payload) { mixer.doc.addChannel(payload.pluginId, payload.name) }
    }

    Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
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
            delegate: ChannelStrip {
                height: Math.min(ListView.view.height, Theme.stripHeight)
                doc: mixer.doc
                pluginModel: mixer.pluginModel
                effectWindows: mixer.effectWindows
            }
            footer: Item {
                width: Theme.stripWidth + 8
                height: Math.min(strips.height, Theme.stripHeight)
                // Add a channel: pick an instrument (grouped by maker)
                EffectSlot {
                    id: newChannelSlot
                    x: 4
                    width: Theme.stripWidth
                    height: parent.height
                    text: ""
                    onClicked: newChannelPicker.popup(newChannelSlot, newChannelSlot.width / 2, newChannelSlot.height / 2)
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
            radius: Theme.radius
            color: Theme.stripBackground
            border.color: Theme.stripBorder

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
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    radius: 3
                    color: Theme.panelRaised
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
