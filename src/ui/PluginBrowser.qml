import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Installed plugins as a Kontakt-style library shelf: a banner card per
// plugin (a picture of its own editor once captured). Drag an instrument onto
// the mixer (or double-click) to add a channel; drag an effect onto a strip.
Item {
    id: browser

    required property DocumentController doc
    required property PluginListModel pluginModel
    required property ArtworkBuilder artworkBuilder

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

        // Build artwork: take a picture of every plugin's own editor.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            Button {
                visible: !browser.artworkBuilder.running
                text: qsTr("Build artwork")
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                onClicked: browser.artworkBuilder.start()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Opens each plugin once, off-screen, to take a picture of it")
            }
            ColumnLayout {
                visible: browser.artworkBuilder.running
                Layout.fillWidth: true
                spacing: 2
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Capturing %1 (%2 of %3)").arg(browser.artworkBuilder.current)
                          .arg(browser.artworkBuilder.done + 1).arg(browser.artworkBuilder.total)
                    color: Theme.textDim
                    font.pixelSize: Theme.smallFontSize
                    elide: Text.ElideRight
                }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, browser.artworkBuilder.total)
                    value: browser.artworkBuilder.done
                }
            }
            Button {
                visible: browser.artworkBuilder.running
                text: qsTr("Cancel")
                focusPolicy: Qt.NoFocus
                onClicked: browser.artworkBuilder.cancel()
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
                            verticalAlignment: Image.AlignTop // editors carry their branding at the top
                            asynchronous: true
                            cache: false
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
