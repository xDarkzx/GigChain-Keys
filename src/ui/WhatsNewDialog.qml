import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Once after each update: this version's changes (from the CHANGELOG), and
// where to see them and say thanks (CommunityController.whatsNewDue).
StageDialog {
    id: news
    objectName: "whatsNewDialog"

    required property CommunityController community
    required property DocumentController doc

    title: qsTr("What's new in %1 %2").arg(Branding.name).arg(community.version)
    width: 600
    onClosed: community.whatsNewSeen()

    function openLink(url) {
        if (!Qt.openUrlExternally(url)) {
            console.warn("Could not open " + url + " in the browser")
            doc.reportMessage(qsTr("Could not open the browser: the page is %1").arg(url))
        }
    }

    ColumnLayout {
        width: news.width
        spacing: Theme.spacing

        ScrollView {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            Layout.preferredHeight: Math.min(changes.implicitHeight, 340)
            contentWidth: availableWidth
            ColumnLayout {
                id: changes
                width: parent.width
                spacing: 8
                Repeater {
                    model: news.community.whatsNew
                    delegate: RowLayout {
                        id: change
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: 10
                        Label { Layout.alignment: Qt.AlignTop; text: "•"; color: Theme.accent; font.bold: true }
                        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: change.modelData; color: Theme.text }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
            wrapMode: Text.Wrap
            text: qsTr("%1 is free and made by a keyboard player. A star, a share or a hello on Discord keeps it going.").arg(Branding.name)
            color: Theme.textDim
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            spacing: Theme.spacing
            StageButton {
                Layout.preferredHeight: Theme.touchTarget
                text: qsTr("▶ Watch the videos")
                onClicked: news.openLink(news.community.videosUrl)
            }
            StageButton {
                Layout.preferredHeight: Theme.touchTarget
                text: qsTr("⭐ Star on GitHub")
                onClicked: news.openLink(Branding.website)
            }
            StageButton {
                Layout.preferredHeight: Theme.touchTarget
                text: qsTr("💬 Discord")
                onClicked: news.openLink(news.community.discordUrl)
            }
            Item { Layout.fillWidth: true }
            StageButton {
                objectName: "whatsNewClose"
                Layout.preferredHeight: Theme.touchTarget
                implicitWidth: 100
                text: qsTr("Got it")
                tone: "accent"
                onClicked: news.close()
            }
        }
    }
}
