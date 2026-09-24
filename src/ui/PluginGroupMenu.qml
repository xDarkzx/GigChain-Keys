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
            required property var modelData
            text: modelData.name
            onTriggered: group.picked(modelData.pluginId, modelData.name)
        }
        onObjectAdded: (index, object) => group.insertItem(index, object)
        onObjectRemoved: (index, object) => group.removeItem(object)
    }
}
