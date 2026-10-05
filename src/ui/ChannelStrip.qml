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
    required property int mappingCount
    required property DocumentController doc
    required property PluginListModel pluginModel
    // Clicking an effect opens its own window (floating, as in a DAW).
    property EffectWindows effectWindows: null
    // Audio input channels open now (for "Play Audio Input").
    property int inputChannels: 0

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

    Component {
        id: channelMenuComponent
        StageMenu {
            StageMenuItem { text: qsTr("Open %1").arg(strip.instrumentName || qsTr("instrument")); onTriggered: strip.doc.selectedChannel = strip.index }
            StageMenuItem { text: strip.mute ? qsTr("Unmute") : qsTr("Mute"); onTriggered: strip.doc.setChannelMute(strip.index, !strip.mute) }
            StageMenuItem { text: strip.solo ? qsTr("Unsolo") : qsTr("Solo"); onTriggered: strip.doc.setChannelSolo(strip.index, !strip.solo) }
            StageMenuItem { text: qsTr("Keyboard Zone…"); onTriggered: strip.doc.editChannel(strip.index, "zone") }
            StageMenuItem { text: qsTr("Knobs…"); onTriggered: strip.doc.editChannel(strip.index, "knobs") }
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

        PanKnob {
            objectName: "panKnob"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 2
            pan: strip.pan
            onPanMoved: (v) => strip.doc.setChannelPan(strip.index, v)
        }

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

        // name tag: a coloured plate, lit from above
        Rectangle {
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
                anchors.fill: parent
                anchors.margins: 3
                text: strip.name
                color: "white"
                font.pixelSize: Theme.smallFontSize
                font.bold: true
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
