import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Collapsible left panel: Setlist and Instruments tabs (setlist only in Perform).
StagePanel {
    id: panel

    required property DocumentController doc
    required property SetlistModel setlistModel
    required property PluginListModel pluginModel
    property bool editable: true

    outlined: false

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabs
            objectName: "sidePanelTabs"
            Layout.fillWidth: true
            visible: panel.editable
            spacing: 0
            background: Rectangle { color: Theme.barBottom }
            StageTabButton { text: qsTr("Setlist") }
            StageTabButton { text: qsTr("Instruments") }
        }
        StageDivider { Layout.fillWidth: true; visible: panel.editable }

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

    // The engraved edge against the main area.
    StageDivider {
        vertical: true
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
    }
}
