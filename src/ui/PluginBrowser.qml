import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The installed VST instruments, as a Kontakt-style stacked list of cards:
// name on top, the instrument's own art (installed by its maker) as the
// banner, and a footer bar. Instruments whose maker installs no art get a
// banner with the maker's name and a category icon.
// Double-click (or drag onto the mixer) to add the instrument as a channel.
Item {
    id: browser

    required property DocumentController doc
    required property PluginListModel pluginModel

    Component.onCompleted: pluginModel.instrumentsOnly = true

    // A stable colour per maker for the banner.
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
            placeholderText: qsTr("Search instruments")
            onTextChanged: browser.pluginModel.filterText = text
            onAccepted: focus = false
            Keys.onEscapePressed: {
                text = ""
                focus = false
            }
        }

        Button {
            text: qsTr("Show hidden instruments")
            flat: true
            focusPolicy: Qt.NoFocus
            Layout.alignment: Qt.AlignRight
            onClicked: browser.pluginModel.showAll()
        }

        ListView {
            objectName: "pluginList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: browser.pluginModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

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
                height: 100
                radius: 3
                color: hover.hovered ? Theme.slotHover : Theme.panelRaised
                border.color: Theme.stripBorder
                HoverHandler { id: hover }

                Label {
                    id: title
                    x: 6
                    y: 3
                    width: parent.width - 12
                    text: card.name
                    font.pixelSize: Theme.smallFontSize
                    font.bold: true
                    elide: Text.ElideRight
                }

                // Banner: maker colour, category icon and the maker's name.
                Rectangle {
                    id: banner
                    x: 3
                    y: title.y + title.height + 2
                    width: parent.width - 6
                    height: 56
                    radius: 2
                    clip: true
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Qt.darker(browser.vendorColor(card.vendor), 1.7) }
                        GradientStop { position: 1.0; color: browser.vendorColor(card.vendor) }
                    }
                    Image {
                        id: art
                        anchors.fill: parent
                        source: card.imageUrl
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        visible: status === Image.Ready
                    }
                    Image {
                        visible: art.status !== Image.Ready
                        anchors.verticalCenter: parent.verticalCenter
                        x: 12
                        source: card.icon
                        sourceSize: Qt.size(26, 26)
                        opacity: 0.9
                    }
                    Text {
                        visible: art.status !== Image.Ready
                        anchors.verticalCenter: parent.verticalCenter
                        x: 50
                        width: parent.width - 58
                        text: card.vendor.toUpperCase()
                        color: "white"
                        font.pixelSize: 15
                        font.letterSpacing: 2
                        font.weight: Font.Light
                        elide: Text.ElideRight
                    }
                }

                Label {
                    x: 6
                    y: banner.y + banner.height + 2
                    width: parent.width - 12
                    text: [qsTr("Instrument"), card.category, card.version !== "" ? "v" + card.version : ""]
                          .filter((part) => part !== "").join("  ·  ")
                    color: Theme.textDim
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }

                DragSource {
                    dragKey: card.kind
                    label: card.name
                    payload: ({ pluginId: card.pluginId, name: card.name, kind: card.kind })
                    onDoubleClicked: browser.doc.addChannel(card.pluginId, card.name)
                    onRightClicked: cardMenu.popup()
                }
                StageMenu {
                    id: cardMenu
                    StageMenuItem { text: qsTr("Hide from list"); onTriggered: browser.pluginModel.hide(card.pluginId) }
                }
            }
        }
    }
}
