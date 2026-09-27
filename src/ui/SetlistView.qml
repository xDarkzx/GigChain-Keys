pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
            spacing: Theme.spacingSmall
            topMargin: Theme.spacing
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

                width: ListView.view.width - 2 * Theme.spacing
                x: Theme.spacing
                visible: isSong
                height: isSong ? 42 : 0
                radius: Theme.radiusCard
                border.color: isCurrentSong ? Qt.darker(Theme.accentBottom, 1.3) : Theme.outline
                gradient: Gradient {
                    GradientStop { position: 0.0; color: row.isCurrentSong ? Theme.accentTop : (hoverArea.hovered ? Theme.buttonHoverTop : Theme.buttonTop) }
                    GradientStop { position: 1.0; color: row.isCurrentSong ? Theme.accentBottom : (hoverArea.hovered ? Theme.buttonHoverBottom : Theme.buttonBottom) }
                }
                HoverHandler { id: hoverArea }
                // The lit top edge.
                Rectangle { x: 2; y: 1; width: parent.width - 4; height: 1; color: row.isCurrentSong ? "#40ffffff" : Theme.bevelLight }

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
                    StageTextField {
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
                        text: qsTr("Tempo…")
                        onTriggered: {
                            tempoPopup.song = row.songIndex
                            tempoPopup.open()
                        }
                    }
                    StageMenuItem {
                        text: qsTr("Backing Track…")
                        onTriggered: {
                            trackDialog.song = row.songIndex
                            trackDialog.open()
                        }
                    }
                    StageMenuItem {
                        text: qsTr("Remove Backing Track")
                        enabled: row.isCurrentSong && view.doc.songBackingTrack !== ""
                        onTriggered: view.doc.setSongBackingTrack(row.songIndex, "")
                    }
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

        StageDivider { Layout.fillWidth: true; visible: view.editable }
        RowLayout {
            visible: view.editable
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            StageButton {
                text: qsTr("Song")
                iconSource: "icons/plus.svg"
                Layout.fillWidth: true
                onClicked: view.doc.addSong()
            }
        }
    }

    // A song's tempo: it plays whenever the song is chosen (0 = none: the
    // tempo stays as it is).
    StageDialog {
        id: tempoPopup
        property int song: -1
        title: qsTr("Song tempo")
        width: 300
        onAboutToShow: {
            view.doc.selectPatch(song, 0)
            tempoBox.value = Math.round(view.doc.songTempo)
        }
        ColumnLayout {
            width: parent.width
            spacing: Theme.spacing
            Label {
                Layout.fillWidth: true
                Layout.margins: Theme.spacingLarge
                Layout.bottomMargin: 0
                text: qsTr("Beats per minute, played whenever the song is chosen (0 = none: the tempo stays as it is).")
                color: Theme.textDim
                wrapMode: Text.Wrap
            }
            StageSpinBox {
                id: tempoBox
                objectName: "songTempoBox"
                Layout.leftMargin: Theme.spacingLarge
                from: 0; to: 400
                editable: true
            }
            StageDivider { Layout.fillWidth: true; Layout.topMargin: Theme.spacing }
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.spacing
                Item { Layout.fillWidth: true }
                StageButton { text: qsTr("Cancel"); onClicked: tempoPopup.close() }
                StageButton {
                    text: qsTr("Set")
                    tone: "accent"
                    onClicked: {
                        view.doc.setSongTempo(tempoPopup.song, tempoBox.value)
                        tempoPopup.close()
                    }
                }
            }
        }
    }

    FileDialog {
        id: trackDialog
        property int song: -1
        title: qsTr("Backing track")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Audio files (*.wav *.mp3 *.flac *.m4a *.aac *.ogg *.aiff *.aif)"), qsTr("All files (*)")]
        onAccepted: view.doc.setSongBackingTrack(song, selectedFile)
    }
}
