pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A bus's effect slots (kept with the rig, not the setlist), then one empty
// slot to add another; click one to open its window, right-click for
// bypass, replace and remove.
ColumnLayout {
    id: list

    property MasterBus bus: null
    required property PluginListModel pluginModel
    property string addHint: ""
    property int menuEffect: -1

    spacing: 3

    ListView {
        id: effects
        objectName: "busEffectList"
        readonly property int slotHeight: 20
        readonly property int maxVisible: 4
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(count, maxVisible) * (slotHeight + spacing) - (count > 0 ? spacing : 0)
        visible: count > 0
        spacing: 3
        clip: true
        interactive: count > maxVisible
        boundsBehavior: Flickable.StopAtBounds
        model: list.bus ? list.bus.effectNames : []
        onCountChanged: positionViewAtEnd()
        ScrollBar.vertical: ScrollBar {
            policy: effects.count > effects.maxVisible ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            width: 3
        }
        delegate: EffectSlot {
            id: busSlot
            required property int index
            required property string modelData
            width: ListView.view.width - (effects.count > effects.maxVisible ? 4 : 0)
            height: effects.slotHeight
            text: modelData
            bypassed: list.bus.effectBypassed[index] === true
            onClicked: list.bus.openEffect(index, busSlot.Window.window)
            onPowerToggled: list.bus.setEffectBypass(index, !bypassed)
            onMenuRequested: {
                list.menuEffect = index
                effectMenu.popup(busSlot, 0, busSlot.height)
            }
        }
    }
    EffectSlot {
        id: addSlot
        objectName: "busAddEffect"
        Layout.fillWidth: true
        visible: list.bus !== null
        text: ""
        onClicked: {
            if (!addMenu) addMenu = addMenuComponent.createObject(list)
            addMenu.popup(addSlot, 0, addSlot.height)
        }
        property var addMenu: null
        property var replaceMenu: null
        HoverHandler { id: addHover }
        ToolTip.visible: addHover.hovered && list.addHint !== ""
        ToolTip.text: list.addHint
    }
    Component {
        id: addMenuComponent
        EffectPickerMenu {
            pluginModel: list.pluginModel
            onPicked: (pluginId, name) => list.bus.addEffect(pluginId, name)
        }
    }
    StageMenu {
        id: effectMenu
        StageMenuItem {
            text: list.menuEffect >= 0 && list.bus && list.bus.effectBypassed[list.menuEffect] ? qsTr("Turn On") : qsTr("Bypass")
            onTriggered: list.bus.setEffectBypass(list.menuEffect, !list.bus.effectBypassed[list.menuEffect])
        }
        StageMenuItem {
            text: qsTr("Replace With…")
            onTriggered: {
                if (!addSlot.replaceMenu) addSlot.replaceMenu = replaceMenuComponent.createObject(list)
                addSlot.replaceMenu.popup(addSlot, 0, addSlot.height)
            }
        }
        MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
        StageMenuItem { text: qsTr("Remove Effect"); onTriggered: list.bus.removeEffect(list.menuEffect) }
    }
    Component {
        id: replaceMenuComponent
        EffectPickerMenu {
            pluginModel: list.pluginModel
            onPicked: (pluginId, name) => list.bus.replaceEffect(list.menuEffect, pluginId, name)
        }
    }
}
