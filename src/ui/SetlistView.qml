import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The setlist's songs. Click a song to play it, double-click to rename, drag
// to reorder, right-click for more. (Songs may still hold sections; they are
// not listed.)
Item {
    id: view

    required property DocumentController doc
    required property SetlistModel setlistModel
    property bool editable: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: list
            objectName: "setlistList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: view.setlistModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: row

                required property string kind
                required property string name
                required property int songIndex
                required property int patchIndex
                required property int number
                required property bool isCurrent
                required property bool isCurrentSong
                readonly property bool isSong: kind === "song"

                width: ListView.view.width
                visible: isSong
                height: isSong ? 40 : 0
                color: isCurrentSong ? Theme.accent : (hoverArea.hovered ? Theme.slotHover : Theme.panelRaised)
                HoverHandler { id: hoverArea }

                function startRename() {
                    if (!view.editable) return
                    renameField.text = row.name
                    renameField.visible = true
                    renameField.forceActiveFocus()
                    renameField.selectAll()
                }
                function select() {
                    view.doc.selectPatch(row.songIndex, row.isSong ? 0 : row.patchIndex)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing
                    anchors.rightMargin: Theme.spacing
                    spacing: Theme.spacing
                    Label {
                        text: row.number
                        color: row.isCurrentSong ? Theme.accentText : Theme.textDim
                        Layout.preferredWidth: 22
                        horizontalAlignment: Text.AlignRight
                    }
                    Label {
                        visible: !renameField.visible
                        text: row.name
                        font.bold: true
                        font.pixelSize: Theme.fontSize + 1
                        color: row.isCurrentSong ? Theme.accentText : Theme.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    TextField {
                        id: renameField
                        visible: false
                        Layout.fillWidth: true
                        onAccepted: {
                            if (row.isSong) view.doc.renameSong(row.songIndex, text)
                            else view.doc.renamePatch(row.songIndex, row.patchIndex, text)
                            visible = false
                        }
                        onActiveFocusChanged: if (!activeFocus) visible = false
                        Keys.onEscapePressed: visible = false
                    }
                }

                DragSource {
                    dragEnabled: view.editable && !renameField.visible
                    dragKey: row.isSong ? "song" : "patch"
                    label: row.name
                    payload: ({ kind: row.kind, songIndex: row.songIndex, patchIndex: row.patchIndex })
                    onClicked: row.select()
                    onDoubleClicked: row.startRename()
                    onRightClicked: if (view.editable) contextMenu.popup()
                }

                DropArea {
                    anchors.fill: parent
                    keys: ["song", "patch"]
                    function acceptDrop(payload) {
                        if (payload.kind === "song")
                            view.doc.moveSong(payload.songIndex, row.songIndex)
                        else if (!row.isSong && payload.songIndex === row.songIndex)
                            view.doc.movePatch(row.songIndex, payload.patchIndex, row.patchIndex)
                    }
                }

                StageMenu {
                    id: contextMenu
                    StageMenuItem { text: qsTr("Rename"); onTriggered: row.startRename() }
                    StageMenuItem {
                        text: qsTr("Duplicate")
                        onTriggered: row.isSong ? view.doc.duplicateSong(row.songIndex)
                                                : view.doc.duplicatePatch(row.songIndex, row.patchIndex)
                    }
                    MenuSeparator { contentItem: Rectangle { implicitHeight: 1; color: Theme.stripBorder } }
                    StageMenuItem {
                        text: qsTr("Delete")
                        onTriggered: row.isSong ? view.doc.removeSong(row.songIndex)
                                                : view.doc.removePatch(row.songIndex, row.patchIndex)
                    }
                }
            }
        }

        RowLayout {
            visible: view.editable
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            Button {
                text: qsTr("+ Song")
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                onClicked: view.doc.addSong()
            }
        }
    }
}
