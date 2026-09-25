import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shown until a setlist is open: nothing is created by default.
Rectangle {
    id: start

    required property DocumentController doc
    signal newRequested()
    signal openRequested()
    signal openRecentRequested(string path)

    objectName: "startScreen"
    color: Theme.background

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 40)
        spacing: 14

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("No setlist open")
            font.pixelSize: Theme.headerFontSize + 4
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("Start a setlist, then paste each song's chords and lyrics into it.")
            color: Theme.textDim
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10
            Button {
                objectName: "newSetlistButton"
                text: qsTr("New setlist")
                highlighted: true
                focusPolicy: Qt.NoFocus
                onClicked: start.newRequested()
            }
            Button {
                text: qsTr("Open setlist…")
                focusPolicy: Qt.NoFocus
                onClicked: start.openRequested()
            }
        }

        Label {
            Layout.topMargin: 10
            visible: start.doc.recentFiles.length > 0
            text: qsTr("Recent setlists")
            color: Theme.textDim
            font.bold: true
        }
        Repeater {
            model: start.doc.recentFiles
            delegate: ItemDelegate {
                id: recent
                required property string modelData
                Layout.fillWidth: true
                implicitHeight: 40
                onClicked: start.openRecentRequested(modelData)
                contentItem: ColumnLayout {
                    spacing: 0
                    Label {
                        text: recent.modelData.split(/[\\/]/).pop()
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: recent.modelData
                        color: Theme.textDim
                        font.pixelSize: Theme.smallFontSize
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: recent.hovered ? Theme.slotHover : Theme.panelRaised
                }
            }
        }
    }
}
