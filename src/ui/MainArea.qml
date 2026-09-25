import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The main area: the song's chart. Plugins open in windows of their own.
Rectangle {
    id: area

    required property DocumentController doc

    signal newRequested()
    signal openRequested()
    signal openRecentRequested(string path)

    color: Theme.background

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

        ChartPanel {
            objectName: "chartPanel"
            Layout.fillWidth: true
            Layout.fillHeight: true
            doc: area.doc
        }
    }
}
