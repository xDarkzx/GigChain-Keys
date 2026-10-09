pragma ComponentBehavior: Bound

import QtQuick

// Instruments grouped by maker (Arturia, Spitfire Audio, Xfer Records...).
// With `shared` set, first "Same as in another song": an instrument another
// song plays, shared with it (loaded once, changed together).
StageMenu {
    id: picker

    required property PluginListModel pluginModel
    // [{song, patch, channel, name, plugin, songName}]: DocumentController.otherSongsInstruments()
    property var shared: []
    signal picked(string pluginId, string name)
    signal pickedShared(int song, int patch, int channel)

    Instantiator {
        model: picker.shared.length > 0 ? 1 : 0
        delegate: StageMenu {
            id: sameAs
            objectName: "sameAsAnotherSong"
            title: qsTr("Same as in another song")
            Instantiator {
                model: picker.shared
                delegate: StageMenuItem {
                    id: entry
                    required property var modelData
                    objectName: "sharedInstrument"
                    text: qsTr("%1 (%2)").arg(entry.modelData.name).arg(entry.modelData.songName)
                    onTriggered: picker.pickedShared(entry.modelData.song, entry.modelData.patch, entry.modelData.channel)
                }
                onObjectAdded: (index, object) => sameAs.insertItem(index, object)
                onObjectRemoved: (index, object) => sameAs.removeItem(object)
            }
        }
        onObjectAdded: (index, object) => picker.insertMenu(0, object)
        onObjectRemoved: (index, object) => picker.removeMenu(object)
    }

    Instantiator {
        model: picker.pluginModel.instrumentMenu.vendors
        delegate: PluginGroupMenu {
            onPicked: (pluginId, name) => picker.picked(pluginId, name)
        }
        onObjectAdded: (index, object) => picker.insertMenu(index + (picker.shared.length > 0 ? 1 : 0), object)
        onObjectRemoved: (index, object) => picker.removeMenu(object)
    }
}
