import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Collapsible left panel: Setlist and Plugins tabs (setlist only in Perform).
Rectangle {
    id: panel

    required property DocumentController doc
    required property SetlistModel setlistModel
    required property PluginListModel pluginModel
    property bool editable: true

    color: Theme.panel

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabs
            objectName: "sidePanelTabs"
            Layout.fillWidth: true
            visible: panel.editable
            TabButton { text: qsTr("Setlist"); focusPolicy: Qt.NoFocus }
            TabButton { text: qsTr("Plugins"); focusPolicy: Qt.NoFocus }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: panel.editable ? tabs.currentIndex : 0

            SetlistView {
                doc: panel.doc
                setlistModel: panel.setlistModel
                editable: panel.editable
            }
            PluginBrowser {
                doc: panel.doc
                pluginModel: panel.pluginModel
            }
        }
    }
}
