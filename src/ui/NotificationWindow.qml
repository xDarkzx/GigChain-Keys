pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Messages for the user, at the bottom right of the main window, each in its
// level's colour. Each goes by itself after its level's time on screen
// (paused while the mouse is on it), or with its close button. A window of its own, owned
// by the main window: it shows above a plugin's window, and only above this
// app.
Window {
    id: toasts

    required property Notifications notifications
    required property Window owner

    objectName: "notificationWindow"
    transientParent: owner
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    width: 380
    height: Math.max(1, list.contentHeight)
    x: owner.x + owner.width - width - Theme.spacing * 2
    y: owner.y + owner.height - height - Theme.spacing * 4 // above the status line
    // Shown while there is something to say (the model's count: the list's
    // own does not update while this window is hidden).
    visible: owner.visible && notifications.count > 0
    // Above the main window when it shows up with a message waiting (at
    // start, the main window is brought to the front after this one shows;
    // not every Linux window manager keeps an owned window above its owner).
    onVisibleChanged: if (visible) Qt.callLater(raise)

    ListView {
        id: list
        objectName: "notificationList"
        anchors.fill: parent
        model: toasts.notifications
        spacing: 6
        interactive: false

        delegate: Rectangle {
            id: toast

            required property string text
            required property int level
            required property int notificationId
            required property int repeats
            readonly property color levelColour: level === Notifications.Error ? Theme.danger
                                                 : level === Notifications.Warning ? Theme.warning
                                                 : Theme.info

            width: ListView.view.width
            height: Math.max(44, message.implicitHeight + 20)
            radius: Theme.radiusCard
            border.color: levelColour
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.barTop }
                GradientStop { position: 1.0; color: Theme.barBottom }
            }
            Rectangle { x: 6; y: 1; width: parent.width - 8; height: 1; color: Theme.bevelLight }

            // The level's colour down the left edge.
            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                width: 5
                radius: Theme.radius
                color: toast.levelColour
            }

            RowLayout {
                anchors { fill: parent; leftMargin: 16; rightMargin: 4 }
                spacing: Theme.spacing

                Label {
                    id: message
                    objectName: "notificationText"
                    Layout.fillWidth: true
                    text: toast.repeats > 1 ? qsTr("%1  (×%2)").arg(toast.text).arg(toast.repeats) : toast.text
                    color: Theme.text
                    font.pixelSize: Theme.fontSize
                    wrapMode: Text.Wrap
                    maximumLineCount: 5 // (a message with what to do about it, read whole)
                    elide: Text.ElideRight
                }
                StageButton {
                    iconSource: "icons/x.svg"
                    iconSize: 12
                    tip: qsTr("Dismiss")
                    onClicked: toasts.notifications.dismiss(toast.notificationId)
                }
            }

            HoverHandler { id: hover }

            Timer {
                id: lifetime
                interval: toasts.notifications.shownFor(toast.level)
                // Read at leisure: starts over when the mouse leaves, and
                // counts only once it can be seen (a message said at start,
                // behind the splash, waits for the main window).
                running: toasts.visible && !hover.hovered
                onTriggered: toasts.notifications.dismiss(toast.notificationId)
            }
            // Said again: its time starts over (if it is counting: unseen, it still waits).
            onRepeatsChanged: if (lifetime.running) lifetime.restart()
        }
    }
}
