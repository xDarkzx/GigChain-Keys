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
    required property DocumentController doc
    required property PluginListModel pluginModel

    readonly property real peakDb: peak > 0 ? 20 * Math.log10(peak) : -200
    property int menuEffect: -1 // effect the effect menu acts on

    width: Theme.stripWidth
    radius: Theme.radius
    color: selected ? Theme.stripSelected : Theme.stripBackground
    border.color: selected ? Theme.accent : Theme.stripBorder
    border.width: selected ? 2 : 1

    HoverHandler { id: stripHover }

    // Background: left click selects, right click opens the channel menu.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            strip.doc.selectedChannel = strip.index
            if (mouse.button === Qt.RightButton) strip.menu(channelMenuComponent).popup(mouse.x, mouse.y)
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["effect"]
        function acceptDrop(payload) { strip.doc.addEffect(strip.index, payload.pluginId, payload.name) }
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
            Layout.fillWidth: true
            implicitHeight: 22
            text: strip.instrumentName
            showPower: false
            loadedColor: Theme.slotInstrument
            onClicked: {
                strip.doc.selectedChannel = strip.index
                if (!loaded) strip.menu(instrumentPickerComponent).popup(instrumentSlot, 0, instrumentSlot.height)
            }
            onMenuRequested: strip.menu(channelMenuComponent).popup(instrumentSlot, 0, instrumentSlot.height)
        }

        // effect slots, then one empty slot to add another
        Repeater {
            model: strip.effectNames
            delegate: EffectSlot {
                id: fxSlot
                required property int index
                required property string modelData
                Layout.fillWidth: true
                text: modelData
                bypassed: strip.effectBypassed[index] === true
                onClicked: strip.doc.selectedChannel = strip.index
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
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 2
            value: strip.pan
            onPanMoved: (v) => strip.doc.setChannelPan(strip.index, v)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            Readout { Layout.fillWidth: true; text: strip.volumeDb.toFixed(1) }
            Readout {
                Layout.fillWidth: true
                text: strip.peakDb < -99 ? "-∞" : strip.peakDb.toFixed(1)
                alarm: strip.peakDb > 0
            }
        }

        VolumeFader {
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

        // name tag
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            radius: 3
            color: strip.color
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
