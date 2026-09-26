pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The installed VST instruments, as a Kontakt-style stacked list of cards:
// name and maker on top with favourite (★) and details (ⓘ) buttons, the
// instrument's own banner (its VST3 snapshot) when it has one, then the
// user's star rating. Favourites are listed first. Other instruments get a
// banner in the maker's colour with the maker's name and the plugin's own
// icon from its folder (PlugIn.ico), else a category icon.
// Double-click (or drag onto the mixer) to add the instrument as a channel.
Item {
    id: browser

    required property DocumentController doc
    required property PluginListModel pluginModel

    Component.onCompleted: pluginModel.instrumentsOnly = true

    // What each number of stars means.
    function ratingWord(stars) {
        return [qsTr("Rate it"), qsTr("Poor"), qsTr("Okay"), qsTr("Good"), qsTr("Great"), qsTr("Outstanding")][stars] || ""
    }

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
                required property bool officialIcon
                required property string imageUrl
                required property bool favorite
                required property int rating
                required property string website
                required property string email
                required property string sdkVersion
                required property var tags
                required property string location
                required property string size
                required property string installed

                property bool expanded: false

                width: ListView.view.width - 10
                height: body.implicitHeight + 10
                radius: 4
                color: hover.hovered ? Theme.slotHover : Theme.panelRaised
                border.color: card.favorite ? Theme.accent : Theme.stripBorder
                HoverHandler { id: hover }

                // Drag onto the mixer, double-click to add, right-click for more.
                // Underneath the buttons, so they still get their clicks.
                DragSource {
                    dragKey: card.kind
                    label: card.name
                    payload: ({ pluginId: card.pluginId, name: card.name, kind: card.kind })
                    onDoubleClicked: browser.doc.addChannel(card.pluginId, card.name)
                    onRightClicked: cardMenu.popup()
                }
                StageMenu {
                    id: cardMenu
                    StageMenuItem {
                        text: card.favorite ? qsTr("Remove from favourites") : qsTr("Add to favourites")
                        onTriggered: browser.pluginModel.setFavorite(card.pluginId, !card.favorite)
                    }
                    StageMenuItem { text: qsTr("Hide from list"); onTriggered: browser.pluginModel.hide(card.pluginId) }
                }

                ColumnLayout {
                    id: body
                    x: 6
                    y: 5
                    width: parent.width - 12
                    spacing: 4

                    // Name, maker; favourite and info on the right.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                objectName: "cardName"
                                Layout.fillWidth: true
                                text: card.name
                                font.pixelSize: Theme.fontSize
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Label {
                                objectName: "cardMaker"
                                Layout.fillWidth: true
                                text: card.vendor
                                color: Qt.lighter(browser.vendorColor(card.vendor), 1.9)
                                font.pixelSize: Theme.smallFontSize
                                font.bold: true
                                elide: Text.ElideRight
                            }
                        }
                        CardButton {
                            objectName: "favoriteButton"
                            glyph: card.favorite ? "★" : "☆"
                            active: card.favorite
                            tip: card.favorite ? qsTr("Remove from favourites") : qsTr("Favourite: keep at the top")
                            onClicked: browser.pluginModel.setFavorite(card.pluginId, !card.favorite)
                        }
                        CardButton {
                            objectName: "infoButton"
                            glyph: "ⓘ"
                            active: card.expanded
                            tip: card.expanded ? qsTr("Hide details") : qsTr("Details")
                            onClicked: card.expanded = !card.expanded
                        }
                    }

                    // Banner: the maker's own art, else their colour and name.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 56
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
                        // The plugin's own icon (from its folder), else a category icon.
                        Image {
                            id: cardIcon
                            visible: art.status !== Image.Ready
                            anchors.verticalCenter: parent.verticalCenter
                            x: card.officialIcon ? 8 : 12
                            source: card.icon
                            width: card.officialIcon ? 40 : 26
                            height: width
                            sourceSize: card.officialIcon ? Qt.size(80, 80) : Qt.size(26, 26)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            opacity: card.officialIcon ? 1.0 : 0.9
                        }
                        Text {
                            visible: art.status !== Image.Ready
                            anchors.verticalCenter: parent.verticalCenter
                            x: cardIcon.x + cardIcon.width + 12
                            width: parent.width - x - 8
                            text: card.vendor.toUpperCase()
                            color: "white"
                            font.pixelSize: 15
                            font.letterSpacing: 2
                            font.weight: Font.Light
                            elide: Text.ElideRight
                        }
                    }

                    // Rating on the left; category and version on the right.
                    RowLayout {
                        id: ratingRow
                        Layout.fillWidth: true
                        spacing: 0
                        property int hovered: 0 // the star under the mouse, 1-5
                        Label {
                            text: qsTr("Rating")
                            color: Theme.textDim
                            font.pixelSize: 10
                            rightPadding: 4
                        }
                        Repeater {
                            model: 5
                            delegate: Text {
                                id: starText
                                required property int index
                                objectName: "ratingStar" + index
                                text: index < card.rating ? "★" : "☆"
                                color: index < card.rating ? Theme.accent : Theme.textDim
                                font.pixelSize: 13
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -2
                                    hoverEnabled: true
                                    onContainsMouseChanged: ratingRow.hovered = containsMouse ? starText.index + 1 : 0
                                    // The same star again clears the rating.
                                    onClicked: browser.pluginModel.setRating(
                                                   card.pluginId, card.rating === starText.index + 1 ? 0 : starText.index + 1)
                                }
                            }
                        }
                        Label {
                            objectName: "ratingWord"
                            leftPadding: 4
                            // The word for the star under the mouse, else for the rating.
                            text: browser.ratingWord(ratingRow.hovered > 0 ? ratingRow.hovered : card.rating)
                            color: ratingRow.hovered > 0 || card.rating > 0 ? Theme.text : Theme.textDim
                            font.pixelSize: 10
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            text: [card.category, card.version !== "" ? "v" + card.version : ""]
                                  .filter((part) => part !== "").join("  ·  ")
                            color: Theme.textDim
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    // Details (the ⓘ button).
                    ColumnLayout {
                        objectName: "cardDetails"
                        visible: card.expanded
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        spacing: 3

                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.stripBorder }
                        DetailRow { label: qsTr("Maker"); value: card.vendor }
                        DetailRow { label: qsTr("Website"); value: card.website; link: true }
                        DetailRow { label: qsTr("Support"); value: card.email }
                        DetailRow { label: qsTr("Version"); value: card.version }
                        DetailRow { label: qsTr("Type"); value: card.kind === "instrument" ? qsTr("Instrument") : qsTr("Effect") }
                        DetailRow { label: qsTr("Categories"); value: card.tags.join(", ") }
                        DetailRow { label: qsTr("Built with"); value: card.sdkVersion }
                        DetailRow { label: qsTr("Installed"); value: card.installed }
                        DetailRow { label: qsTr("Size"); value: card.size }
                        DetailRow { label: qsTr("Location"); value: card.location; wrap: true }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            spacing: 6
                            Button {
                                text: qsTr("Visit website")
                                visible: card.website !== ""
                                focusPolicy: Qt.NoFocus
                                font.pixelSize: Theme.smallFontSize
                                onClicked: if (!Qt.openUrlExternally(card.website))
                                               browser.doc.reportMessage(qsTr("Could not open %1").arg(card.website),
                                                                         Notifications.Warning)
                            }
                            Button {
                                text: qsTr("Show in folder")
                                focusPolicy: Qt.NoFocus
                                font.pixelSize: Theme.smallFontSize
                                onClicked: {
                                    const problem = browser.pluginModel.showInFolder(card.pluginId)
                                    if (problem !== "") browser.doc.reportMessage(problem, Notifications.Warning)
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    }
}
