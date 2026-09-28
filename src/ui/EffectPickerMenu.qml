pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Logic-style plug-in menu for effects: one submenu per category (EQ,
// Dynamics, Reverb...), then "By Manufacturer". Each submenu is short, so the
// menu always fits on screen.
StageMenu {
    id: picker

    required property PluginListModel pluginModel
    signal picked(string pluginId, string name)

    readonly property var groups: picker.pluginModel.effectMenu

    Instantiator {
        model: picker.groups.categories
        delegate: PluginGroupMenu {
            onPicked: (pluginId, name) => picker.picked(pluginId, name)
        }
        onObjectAdded: (index, object) => picker.insertMenu(index, object)
        onObjectRemoved: (index, object) => picker.removeMenu(object)
    }

    MenuSeparator {
        contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder }
    }

    StageMenu {
        id: byVendor
        title: qsTr("By Manufacturer")
        Instantiator {
            model: picker.groups.vendors
            delegate: PluginGroupMenu {
                onPicked: (pluginId, name) => picker.picked(pluginId, name)
            }
            onObjectAdded: (index, object) => byVendor.insertMenu(index, object)
            onObjectRemoved: (index, object) => byVendor.removeMenu(object)
        }
    }
}
