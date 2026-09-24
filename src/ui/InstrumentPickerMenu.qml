import QtQuick
import QtQuick.Controls

// Instruments grouped by maker (Arturia, Spitfire Audio, Xfer Records...).
StageMenu {
    id: picker

    required property PluginListModel pluginModel
    signal picked(string pluginId, string name)

    Instantiator {
        model: picker.pluginModel.instrumentMenu.vendors
        delegate: PluginGroupMenu {
            onPicked: (pluginId, name) => picker.picked(pluginId, name)
        }
        onObjectAdded: (index, object) => picker.insertMenu(index, object)
        onObjectRemoved: (index, object) => picker.removeMenu(object)
    }
}
