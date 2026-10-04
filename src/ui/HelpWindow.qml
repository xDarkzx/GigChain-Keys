pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Help > User guide: the guide's pages (docs/help, HelpLibrary) in a window
// of its own, beside the app. The topics down the left (or what a search
// finds), the page on the right; a link to another page goes there, and
// Back comes back.
Window {
    id: guide
    objectName: "helpWindow"

    width: 1060
    height: 760
    minimumWidth: 640
    minimumHeight: 420
    title: qsTr("%1 — User guide").arg(Branding.name)
    color: Theme.background

    property string topic: "getting-started"
    property var history: []

    function show(id) {
        root.openTopic(id === "" ? guide.topic : id)
        guide.visible = true
        guide.raise()
        guide.requestActivate()
    }

    Item {
        id: root
        objectName: "helpWindowRoot"
        anchors.fill: parent

        function openTopic(id) {
            if (id === guide.topic && page.markdown !== "") return
            if (page.markdown !== "") guide.history = guide.history.concat([guide.topic])
            guide.topic = id
            page.markdown = HelpLibrary.page(id)
            pageScroll.contentY = 0
        }
        function back() {
            if (guide.history.length === 0) return
            const previous = guide.history[guide.history.length - 1]
            guide.history = guide.history.slice(0, -1)
            guide.topic = previous
            page.markdown = HelpLibrary.page(previous)
            pageScroll.contentY = 0
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // The topics, or what the search finds.
            Rectangle {
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                color: Theme.panel
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLarge
                    spacing: Theme.spacing

                    Label {
                        text: qsTr("User guide")
                        color: Theme.text
                        font.pixelSize: Theme.titleFontSize + 2
                        font.bold: true
                    }
                    StageTextField {
                        id: search
                        objectName: "helpSearch"
                        Layout.fillWidth: true
                        iconSource: "icons/search.svg"
                        placeholderText: qsTr("Search the guide")
                    }

                    ListView {
                        id: topics
                        objectName: "helpTopics"
                        visible: search.text.trim() === ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        readonly property var all: HelpLibrary.topics()
                        model: topics.all
                        delegate: Column {
                            id: topicRow
                            required property var modelData
                            required property int index
                            // The group's name over its first topic.
                            readonly property bool firstOfGroup: topicRow.index === 0
                                || topics.all[topicRow.index - 1].group !== topicRow.modelData.group
                            width: ListView.view.width
                            Label {
                                visible: topicRow.firstOfGroup
                                width: parent.width
                                topPadding: topicRow.index === 0 ? Theme.spacing : Theme.spacingLarge
                                bottomPadding: Theme.spacingSmall
                                text: topicRow.modelData.group.toUpperCase()
                                color: Theme.textDim
                                font.pixelSize: Theme.smallFontSize
                                font.bold: true
                                font.letterSpacing: 1
                            }
                            ItemDelegate {
                                id: topicButton
                                width: parent.width
                                height: 34
                                highlighted: topicRow.modelData.id === guide.topic
                                contentItem: Label {
                                    text: topicRow.modelData.title
                                    color: topicButton.highlighted ? Theme.accentText : Theme.text
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                background: Rectangle {
                                    radius: Theme.radiusSmall
                                    color: topicButton.highlighted ? Theme.accent : (topicButton.hovered ? Theme.slotHover : "transparent")
                                }
                                onClicked: root.openTopic(topicRow.modelData.id)
                            }
                        }
                    }

                    ListView {
                        id: results
                        objectName: "helpResults"
                        visible: search.text.trim() !== ""
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: Theme.spacingSmall
                        model: HelpLibrary.search(search.text)
                        delegate: ItemDelegate {
                            id: resultRow
                            required property var modelData
                            width: ListView.view.width
                            highlighted: resultRow.modelData.id === guide.topic
                            contentItem: Column {
                                spacing: 2
                                Label {
                                    width: parent.width
                                    text: resultRow.modelData.title
                                    color: Theme.text
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Label {
                                    width: parent.width
                                    text: resultRow.modelData.snippet
                                    color: Theme.textDim
                                    font.pixelSize: Theme.smallFontSize
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 3
                                    elide: Text.ElideRight
                                }
                            }
                            background: Rectangle {
                                radius: Theme.radiusSmall
                                color: resultRow.highlighted ? Theme.selection : (resultRow.hovered ? Theme.slotHover : "transparent")
                            }
                            onClicked: root.openTopic(resultRow.modelData.id)
                        }
                        Label {
                            visible: results.count === 0
                            width: parent.width
                            text: qsTr("Nothing in the guide has all of those words.")
                            color: Theme.textDim
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.outline }

            // The page.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: Theme.spacing
                    StageButton {
                        objectName: "helpBack"
                        text: qsTr("Back")
                        iconSource: "icons/chevron-left.svg"
                        enabled: guide.history.length > 0
                        onClicked: root.back()
                    }
                    Item { Layout.fillWidth: true }
                    StageButton {
                        text: qsTr("Close")
                        onClicked: guide.close()
                    }
                }
                StageDivider { Layout.fillWidth: true }

                Flickable {
                    id: pageScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: page.implicitHeight + 60
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    Text {
                        id: page
                        objectName: "helpPage"
                        // A comfortable line length, centred in a wide window.
                        width: Math.min(pageScroll.width - 64, 820)
                        x: Math.max(32, (pageScroll.width - width) / 2)
                        y: 24
                        // The page (Markdown), shown as HTML with links that read on the dark theme.
                        property string markdown: ""
                        text: HelpLibrary.toHtml(page.markdown, String(Theme.accentTop))
                        textFormat: Text.RichText
                        wrapMode: Text.Wrap
                        color: Theme.text
                        font.pixelSize: Theme.fontSize + 2
                        lineHeight: 1.15
                        onLinkActivated: (link) => {
                            if (link.startsWith("help:")) root.openTopic(link.slice(5))
                            else Qt.openUrlExternally(link)
                        }
                        HoverHandler { cursorShape: page.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    }
                }
            }
        }

        Shortcut { sequence: "Esc"; onActivated: guide.close() }
        Shortcut { sequences: [StandardKey.Find]; onActivated: search.forceActiveFocus() }
        Shortcut { sequences: [StandardKey.Back, "Alt+Left"]; onActivated: root.back() }
    }
}
