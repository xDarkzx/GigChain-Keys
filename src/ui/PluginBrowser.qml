import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Installed plugins as a Kontakt-style library shelf: a banner card per
// plugin with the maker's official artwork (VST3 snapshot, NKS, Arturia),
// or a styled card when the maker publishes none. Drag an instrument onto
// the mixer (or double-click) to add a channel; drag an effect onto a strip.
Item {
    id: browser

    required property DocumentController doc
    required property PluginListModel pluginModel

    // Stable colour per vendor for banners without a picture yet.
    function vendorColor(vendor) {
        const palette = ["#3d5a80", "#6d4c9f", "#2a7f62", "#8a4b2f", "#7a2e45", "#2f6f8f", "#5b6b2f", "#4f4f7a"]
        let h = 0
        for (let i = 0; i < vendor.length; ++i) h = (h * 31 + vendor.charCodeAt(i)) % 9973
        return palette[h % palette.length]
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        TextField {
            objectName: "pluginSearch"
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
            spacing: Theme.spacing
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
                bottomPadding: 2
            }

            delegate: Rectangle {
                id: card

                required property string pluginId
                required property string name
                required property string vendor
                required property string kind
                required property string version
                required property string category
                required property string icon
                required property string imageUrl

                width: ListView.view.width - 10
                height: 124
                radius: Theme.radius + 2
                color: hover.hovered ? Theme.slotHover : Theme.panelRaised
                border.color: Theme.stripBorder

                HoverHandler { id: hover }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: card.name
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: card.vendor
                            color: Theme.textDim
                            font.pixelSize: Theme.smallFontSize
                            elide: Text.ElideRight
                            Layout.maximumWidth: card.width * 0.45
                        }
                    }

                    // The banner: the plugin's own picture, or a styled stand-in.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.radius
                        clip: true
                        color: browser.vendorColor(card.vendor)

                        Image {
                            id: picture
                            anchors.fill: parent
                            source: card.imageUrl
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            visible: status === Image.Ready
                        }
                        Rectangle {
                            // readable fallback: gradient, icon and big name
                            anchors.fill: parent
                            visible: picture.status !== Image.Ready
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: Qt.darker(browser.vendorColor(card.vendor), 1.6) }
                                GradientStop { position: 1.0; color: browser.vendorColor(card.vendor) }
                            }
                            Image {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 12
                                source: card.icon
                                sourceSize: Qt.size(30, 30)
                                opacity: 0.85
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 52
                                width: parent.width - 60
                                text: card.name.toUpperCase()
                                color: "white"
                                font.pixelSize: 20
                                font.letterSpacing: 2
                                font.weight: Font.Light
                                elide: Text.ElideRight
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: [card.kind === "instrument" ? qsTr("Instrument") : qsTr("Effect"),
                               card.category, card.version !== "" ? "v" + card.version : ""]
                              .filter((part) => part !== "").join("  ·  ")
                        color: Theme.textDim
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

                DragSource {
                    dragKey: card.kind
                    label: card.name
                    payload: ({ pluginId: card.pluginId, name: card.name, kind: card.kind })
                    onDoubleClicked: {
                        if (card.kind === "instrument")
                            browser.doc.addChannel(card.pluginId, card.name)
                        else
                            browser.doc.addEffect(browser.doc.selectedChannel, card.pluginId, card.name)
                    }
                }
            }
        }
    }
}
