import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Kontakt libraries, as Kontakt's own Libraries tab shows them: each with the
// official banner its maker embedded in the library's .nicnt file.
Item {
    id: browser

    required property DocumentController doc
    required property LibraryListModel libraryModel
    required property PluginListModel pluginModel

    FolderDialog {
        id: folderDialog
        title: qsTr("Add a folder containing Kontakt libraries")
        onAccepted: browser.libraryModel.addFolder(selectedFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("KONTAKT LIBRARIES")
                color: Theme.textDim
                font.pixelSize: Theme.smallFontSize
                font.bold: true
                Layout.fillWidth: true
            }
            BusyIndicator {
                running: browser.libraryModel.scanning
                visible: running
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
            }
            Button {
                text: qsTr("Add folder…")
                focusPolicy: Qt.NoFocus
                onClicked: folderDialog.open()
            }
        }

        Label {
            visible: libraryList.count === 0 && !browser.libraryModel.scanning
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            text: browser.libraryModel.folders.length === 0
                  ? qsTr("Add the folder where your Kontakt libraries are installed.")
                  : qsTr("No Kontakt libraries (.nicnt) found in %1").arg(browser.libraryModel.folders.join(", "))
        }

        ListView {
            id: libraryList
            objectName: "libraryList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: browser.libraryModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: card

                required property string name
                required property string company
                required property string bannerUrl
                required property string folder

                width: ListView.view.width - 10
                height: header.height + banner.height + footer.height + 8
                radius: 3
                color: hover.hovered ? Theme.slotHover : Theme.panelRaised
                border.color: Theme.stripBorder
                HoverHandler { id: hover }

                Label {
                    id: header
                    x: 6
                    y: 3
                    width: parent.width - 12
                    text: card.name
                    font.pixelSize: Theme.smallFontSize
                    font.bold: true
                    elide: Text.ElideRight
                }

                // The official banner at its own proportions (Kontakt's are ~6:1 to 9:1).
                Image {
                    id: banner
                    x: 3
                    y: header.y + header.height + 2
                    width: parent.width - 6
                    height: status === Image.Ready ? width * implicitHeight / Math.max(1, implicitWidth) : 40
                    source: card.bannerUrl
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    Rectangle {
                        anchors.fill: parent
                        visible: banner.status !== Image.Ready
                        color: Theme.panel
                        Text {
                            anchors.centerIn: parent
                            text: card.name
                            color: Theme.textDim
                        }
                    }
                }

                Label {
                    id: footer
                    x: 6
                    y: banner.y + banner.height + 2
                    width: parent.width - 12
                    text: card.company !== "" ? qsTr("Instruments  ·  %1").arg(card.company) : qsTr("Instruments")
                    color: Theme.textDim
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }

                TapHandler {
                    onDoubleTapped: {
                        const kontakt = browser.pluginModel.findInstrument("Kontakt")
                        if (kontakt.pluginId)
                            browser.doc.addChannel(kontakt.pluginId, card.name)
                        else
                            browser.doc.reportMessage(qsTr("Kontakt isn't installed as a VST3 plugin, so %1 can't be loaded yet").arg(card.name))
                    }
                }
                ToolTip.visible: hover.hovered
                ToolTip.delay: 600
                ToolTip.text: card.folder
            }
        }
    }
}
