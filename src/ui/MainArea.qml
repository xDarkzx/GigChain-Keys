import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The main area: the song's chart, or the selected channel's instrument
// window, chosen with the tabs along the top.
Rectangle {
    id: area

    required property DocumentController doc
    required property EditorService editorService
    required property EngineStatus engineStatus
    property bool suspended: false
    property alias currentTab: tabs.currentIndex

    signal newRequested()
    signal openRequested()
    signal openRecentRequested(string path)

    color: Theme.background

    // A new instrument (dropped on the mixer, picked, double-clicked) is shown.
    Connections {
        target: area.doc
        function onChannelAdded(channel) { tabs.currentIndex = 1 }
    }

    // Nothing is open yet: start a setlist or open one.
    StartScreen {
        anchors.fill: parent
        visible: !area.doc.hasSetlist
        doc: area.doc
        onNewRequested: area.newRequested()
        onOpenRequested: area.openRequested()
        onOpenRecentRequested: (path) => area.openRecentRequested(path)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: area.doc.hasSetlist

        TabBar {
            id: tabs
            objectName: "mainTabs"
            Layout.fillWidth: true
            spacing: 0
            StageTabButton { text: qsTr("Chart"); width: implicitWidth + 40 }
            StageTabButton { text: qsTr("Instrument"); width: implicitWidth + 40 }
            background: Rectangle { color: Theme.barBottom }
        }
        StageDivider { Layout.fillWidth: true }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ChartPanel {
                objectName: "chartPanel"
                doc: area.doc
                engineStatus: area.engineStatus
            }
            PluginArea {
                doc: area.doc
                editorService: area.editorService
                // The plugin's window sits above everything: hide it behind the chart tab and dialogs.
                suspended: area.suspended || tabs.currentIndex !== 1 || !area.doc.hasSetlist
            }
        }
    }
}
