pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// A submenu of plugins: {title, plugins: [{pluginId, name}]}.
StageMenu {
    id: group

    required property var modelData
    signal picked(string pluginId, string name)

    title: modelData.title

    Instantiator {
        model: group.modelData.plugins
        delegate: StageMenuItem {
            id: entry
            required property var modelData
            text: entry.modelData.name
            onTriggered: group.picked(entry.modelData.pluginId, entry.modelData.name)
        }
        onObjectAdded: (index, object) => group.insertItem(index, object)
        onObjectRemoved: (index, object) => group.removeItem(object)
    }
}
