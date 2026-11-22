pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every way to support the app. Opened from Help (mode "page", any time), or
// as one of the app's two asks in its whole life ("perform": after a real use
// of Perform; "month": after a month), with Not now and Don't ask again.
StageDialog {
    id: support
    objectName: "supportDialog"

    required property CommunityController community
    required property DocumentController doc
    property string mode: "page" // "page", "perform" or "month"
    readonly property bool asking: mode !== "page"

    title: mode === "perform" ? qsTr("Hope it went well! 🎹")
         : mode === "month" ? qsTr("A month with %1 🎹").arg(Branding.name)
         : qsTr("Support %1").arg(Branding.name)
    width: 560

    function openAs(how) {
        mode = how
        open()
    }
    function openLink(url) {
        if (!Qt.openUrlExternally(url)) {
            console.warn("Could not open " + url + " in the browser")
            doc.reportMessage(qsTr("Could not open the browser: the page is %1").arg(url))
        }
    }

    ColumnLayout {
        // No taller than the window (less the title bar and a margin): the
        // ways to help scroll, the buttons at the bottom always show.
        width: support.width
        height: Math.min(implicitHeight, (support.parent ? support.parent.height : 720) - 44 - 32)
        spacing: 0

        ScrollView {
            objectName: "supportScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            Layout.preferredHeight: ways.implicitHeight
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: ways
                width: parent.width
                spacing: Theme.spacing

                Label {
                    Layout.fillWidth: true
                    Layout.margins: Theme.spacingLarge
                    Layout.bottomMargin: 0
                    wrapMode: Text.Wrap
                    color: Theme.text
                    text: support.mode === "perform"
                          ? qsTr("%1 is free, with no ads and no account, and it's made by one keyboard player. If it helped you on stage, here's how you can help it grow:").arg(Branding.name)
                          : support.mode === "month"
                            ? qsTr("You've been playing %1 for a month. It's free, with no ads and no account, and it's made by one keyboard player. If it's earned a place in your rig, here's how you can help:").arg(Branding.name)
                            : qsTr("%1 is free and always will be: no ads, no account, no subscription. It's made by one keyboard player, and every bit of help keeps it going:").arg(Branding.name)
                }
                Repeater {
                    model: [
                        { icon: "☕", name: qsTr("Donate"), why: qsTr("Any amount, by card, Apple Pay or Google Pay, no account needed. First goal: code signing (and Apple's notarisation), so Windows and Mac warn less when you install it."), url: support.community.donateUrl },
                        { icon: "⭐", name: qsTr("Star it on GitHub"), why: qsTr("Takes a second, and helps other players find it."), url: Branding.website },
                        { icon: "💬", name: qsTr("Join the Discord"), why: qsTr("Show your rig, ask anything, suggest what's next."), url: support.community.discordUrl },
                        { icon: "▶", name: qsTr("Subscribe on YouTube"), why: qsTr("New features, and how to get the most from them."), url: support.community.videosUrl },
                        { icon: "♥", name: qsTr("Donate monthly"), why: qsTr("A small amount each month, through GitHub."), url: support.community.sponsorUrl },
                        { icon: "💡", name: qsTr("Report a bug or an idea"), why: qsTr("What breaks and what's missing decides what comes next."), url: support.community.issuesUrl }
                    ]
                    delegate: RowLayout {
                        id: way
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spacingLarge
                        Layout.rightMargin: Theme.spacingLarge
                        spacing: 12
                        StageButton {
                            Layout.preferredWidth: 210
                            Layout.preferredHeight: Theme.touchTarget
                            focusPolicy: Qt.TabFocus
                            text: way.modelData.icon + "  " + way.modelData.name
                            onClicked: support.openLink(way.modelData.url)
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: way.modelData.why
                            color: Theme.textDim
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingLarge
                    Layout.rightMargin: Theme.spacingLarge
                    wrapMode: Text.Wrap
                    text: qsTr("And tell another keys player about it: that's how most people find it.")
                    color: Theme.textDim
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            spacing: Theme.spacing
            StageButton {
                objectName: "supportNever"
                visible: support.asking
                Layout.preferredHeight: Theme.touchTarget
                focusPolicy: Qt.TabFocus
                text: qsTr("Don't ask again")
                onClicked: {
                    const problem = support.community.neverAskAgain()
                    if (problem !== "") support.doc.reportMessage(problem)
                    support.close()
                }
            }
            Item { Layout.fillWidth: true }
            StageButton {
                objectName: "supportClose"
                Layout.preferredHeight: Theme.touchTarget
                focusPolicy: Qt.TabFocus
                implicitWidth: 110
                text: support.asking ? qsTr("Not now") : qsTr("Close")
                tone: "accent"
                onClicked: support.close()
            }
        }
    }
}
