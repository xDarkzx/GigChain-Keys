import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Installed plugins. Drag an instrument onto the mixer (or double-click) to add
// a channel; drag an effect onto a channel strip (or double-click to add it to
// the selected channel).
Item {
    id: browser

    required property DocumentController doc
    required property PluginListModel pluginModel

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        TextField {
            Layout.fillWidth: true
            placeholderText: qsTr("Search plugins")
            onTextChanged: browser.pluginModel.filterText = text
            onAccepted: focus = false
            Keys.onEscapePressed: {
                text = ""
                focus = false
            }
        }

        ListView {
            objectName: "pluginList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: browser.pluginModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            section.property: "kind"
            section.delegate: Label {
                required property string section
                text: section === "instrument" ? qsTr("INSTRUMENTS") : qsTr("EFFECTS")
                color: Theme.textDim
                font.pixelSize: Theme.smallFontSize
                font.bold: true
                topPadding: Theme.spacing
                bottomPadding: 4
            }

            delegate: Rectangle {
                id: item

                required property string pluginId
                required property string name
                required property string vendor
                required property string kind

                width: ListView.view.width
                height: 42
                radius: Theme.radius
                color: hover.hovered ? Theme.panelRaised : "transparent"

                HoverHandler { id: hover }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.spacing
                    width: parent.width - 2 * Theme.spacing
                    Label { text: item.name; elide: Text.ElideRight; width: parent.width }
                    Label {
                        text: item.vendor
                        color: Theme.textDim
                        font.pixelSize: Theme.smallFontSize
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }

                DragSource {
                    dragKey: item.kind
                    label: item.name
                    payload: ({ pluginId: item.pluginId, name: item.name, kind: item.kind })
                    onDoubleClicked: {
                        if (item.kind === "instrument")
                            browser.doc.addChannel(item.pluginId, item.name)
                        else
                            browser.doc.addEffect(browser.doc.selectedChannel, item.pluginId, item.name)
                    }
                }
            }
        }
    }
}
