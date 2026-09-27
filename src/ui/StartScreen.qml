import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shown until a setlist is open: nothing is created by default.
Rectangle {
    id: start

    required property DocumentController doc
    signal newRequested()
    signal openRequested()
    signal openRecentRequested(string path)

    objectName: "startScreen"
    color: Theme.background

    // "Opened today", "yesterday", "3 days ago", then the date; "" when unknown.
    function whenOpened(opened) {
        if (!(opened instanceof Date) || isNaN(opened.getTime())) return ""
        const startOfDay = (d) => new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime()
        const days = Math.round((startOfDay(new Date()) - startOfDay(opened)) / 86400000)
        if (days <= 0) return qsTr("Opened today")
        if (days === 1) return qsTr("Opened yesterday")
        if (days < 7) return qsTr("Opened %1 days ago").arg(days)
        return qsTr("Opened %1").arg(opened.toLocaleDateString(Qt.locale(), Locale.ShortFormat))
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 40)
        spacing: 14

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("No setlist open")
            font.pixelSize: Theme.headerFontSize + 4
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("Start a setlist, then paste each song's chords and lyrics into it.")
            color: Theme.textDim
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10
            StageButton {
                objectName: "newSetlistButton"
                text: qsTr("New setlist")
                iconSource: "icons/plus.svg"
                tone: "accent"
                implicitHeight: 36
                onClicked: start.newRequested()
            }
            StageButton {
                text: qsTr("Open setlist…")
                implicitHeight: 36
                onClicked: start.openRequested()
            }
        }

        StageDivider { Layout.fillWidth: true; Layout.topMargin: 10; visible: start.doc.recentFiles.length > 0 }
        Label {
            visible: start.doc.recentFiles.length > 0
            text: qsTr("Recent setlists")
            color: Theme.textDim
            font.bold: true
        }
        Repeater {
            model: start.doc.recentSetlists
            delegate: ItemDelegate {
                id: recent
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 48
                onClicked: start.openRecentRequested(modelData.path)
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: modelData.path
                contentItem: ColumnLayout {
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: recent.modelData.name
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: recent.modelData.songs >= 0
                            text: recent.modelData.songs === 1 ? qsTr("1 song") : qsTr("%1 songs").arg(recent.modelData.songs)
                            color: Theme.textDim
                        }
                    }
                    Label {
                        text: [start.whenOpened(recent.modelData.opened), recent.modelData.path]
                                  .filter((part) => part !== "").join("  ·  ")
                        color: Theme.textDim
                        font.pixelSize: Theme.smallFontSize
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }
                background: Rectangle {
                    radius: Theme.radiusCard
                    border.color: Theme.outline
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: recent.hovered ? Theme.buttonHoverTop : Theme.buttonTop }
                        GradientStop { position: 1.0; color: recent.hovered ? Theme.buttonHoverBottom : Theme.buttonBottom }
                    }
                    Rectangle { x: 3; y: 1; width: parent.width - 6; height: 1; color: Theme.bevelLight }
                }
            }
        }
    }
}
