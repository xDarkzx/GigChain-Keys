import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The very first start only: three steps to the first sound, and the way
// into the community (CommunityController.welcomeDue).
StageDialog {
    id: welcome
    objectName: "welcomeDialog"

    required property CommunityController community
    required property DocumentController doc
    signal guideRequested()

    title: qsTr("Welcome to %1").arg(Branding.name)
    width: 560
    onClosed: community.welcomeSeen()

    function openLink(url) {
        if (!Qt.openUrlExternally(url)) {
            console.warn("Could not open " + url + " in the browser")
            doc.reportMessage(qsTr("Could not open the browser: the page is %1").arg(url))
        }
    }

    ColumnLayout {
        width: welcome.width
        spacing: Theme.spacing

        Label {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            Layout.bottomMargin: 0
            wrapMode: Text.Wrap
            text: qsTr("Your keyboard rig, free. Three steps to your first sound:")
            color: Theme.text
            font.pixelSize: Theme.fontSize + 2
        }
        Repeater {
            model: [
                qsTr("<b>Pick your audio</b>: Settings > Audio, your interface or the computer's own."),
                qsTr("<b>Plug in your keyboard</b>: it is found by itself (Settings > MIDI to check)."),
                qsTr("<b>Add a sound</b>: choose an instrument on the left. Try Piano Pro or Pads Pro, or one of your own plugins.")
            ]
            delegate: RowLayout {
                id: step
                required property string modelData
                required property int index
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                spacing: 12
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: 26
                    implicitHeight: 26
                    radius: 13
                    color: Theme.accent
                    Label { anchors.centerIn: parent; text: step.index + 1; color: "white"; font.bold: true }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    text: step.modelData
                    color: Theme.text
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            implicitHeight: invite.implicitHeight + 24
            radius: Theme.radiusSmall
            color: Theme.wellTop
            border.color: Theme.outline
            ColumnLayout {
                id: invite
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Questions, ideas, or want to show your rig? Other keyboard players are on the Discord, and so is the person who makes %1.").arg(Branding.name)
                    color: Theme.textDim
                }
                StageButton {
                    objectName: "welcomeDiscord"
                    Layout.preferredHeight: Theme.touchTarget
                    text: qsTr("💬 Join the Discord")
                    onClicked: welcome.openLink(welcome.community.discordUrl)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            Layout.topMargin: 0
            spacing: Theme.spacing
            StageButton {
                Layout.preferredHeight: Theme.touchTarget
                text: qsTr("Getting started guide")
                onClicked: {
                    welcome.close()
                    welcome.guideRequested()
                }
            }
            Item { Layout.fillWidth: true }
            StageButton {
                objectName: "welcomeClose"
                Layout.preferredHeight: Theme.touchTarget
                implicitWidth: 120
                text: qsTr("Let's go")
                tone: "accent"
                onClicked: welcome.close()
            }
        }
    }
}
