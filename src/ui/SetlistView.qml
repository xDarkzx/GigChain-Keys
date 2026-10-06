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
            // (Up and Down choose songs everywhere: Main's shortcuts.)
            keyNavigationEnabled: false

            // A song clicked: Delete (a Mac's delete key, ⌘delete) removes it,
            // F2 (on a Mac also Return) renames it, Ctrl+D (⌘D) duplicates it.
            // (Never on stage: the list is not editable there.)
            // F2: the song's own row starts renaming it.
            signal renameRequested(int song)
            Keys.onPressed: (event) => {
                const song = view.doc.songIndex
                if (!view.editable || song < 0) return
                if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                    event.accepted = true
                    view.doc.removeSong(song)
                } else if (event.key === Qt.Key_F2
                           || (Theme.mac && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter))) { // (the Finder's way)
                    event.accepted = true
                    list.renameRequested(song)
                } else if (event.key === Qt.Key_D && event.modifiers === Qt.ControlModifier) {
                    event.accepted = true
                    view.doc.duplicateSong(song)
                }
            }

            delegate: Rectangle {
                id: row
                objectName: "setlistRow"

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
                Connections {
                    target: list
                    function onRenameRequested(song) { if (row.isSong && row.songIndex === song) row.startRename() }
                }
                function select() {
                    view.doc.selectPatch(row.songIndex, row.isSong ? 0 : row.patchIndex)
                    list.forceActiveFocus() // (the keys below act on it)
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

                PayloadDropArea {
                    anchors.fill: parent
                    keys: ["song", "patch"]
                    onPayloadDropped: (payload) => {
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
        title: qsTr("Song tempo and time")
        width: 340
        // Time signatures a song is likely to have; the song's own is added when it is another.
        readonly property var commonTimes: ["2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8"]
        property var times: commonTimes
        onAboutToShow: {
            view.doc.selectPatch(song, 0)
            tempoBox.value = Math.round(view.doc.songTempo)
            const now = view.doc.songTimeNumerator + "/" + view.doc.songTimeDenominator
            times = commonTimes.indexOf(now) >= 0 ? commonTimes : commonTimes.concat([now])
            timeBox.currentIndex = times.indexOf(now)
            earlyBox.checked = view.doc.songSwitchEarly
            followBox.checked = view.doc.songFollowChords
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
            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                text: qsTr("Time signature: how the song's bars are counted for its sections, the click and plugins.")
                color: Theme.textDim
                wrapMode: Text.Wrap
            }
            StageComboBox {
                id: timeBox
                objectName: "songTimeBox"
                Layout.leftMargin: Theme.spacingLarge
                implicitWidth: 120
                model: tempoPopup.times
            }
            CheckBox {
                id: earlyBox
                objectName: "songSwitchEarlyBox"
                Layout.leftMargin: Theme.spacing
                text: qsTr("Change sections a beat early (for pads that swell in)")
                focusPolicy: Qt.NoFocus
            }
            CheckBox {
                id: followBox
                objectName: "songFollowChordsBox"
                Layout.leftMargin: Theme.spacing
                text: qsTr("Sections follow the chords I play (off: they follow the tempo)")
                focusPolicy: Qt.NoFocus
            }
            Label {
                objectName: "songFollowHint"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                visible: followBox.checked && !view.doc.following
                text: qsTr("Add chords to the chart to follow them (until then the tempo leads).")
                color: Theme.textDim
                wrapMode: Text.Wrap
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
                        const time = tempoPopup.times[timeBox.currentIndex].split("/")
                        view.doc.setSongTimeSignature(tempoPopup.song, parseInt(time[0]), parseInt(time[1]))
                        view.doc.setSongSwitchEarly(tempoPopup.song, earlyBox.checked)
                        view.doc.setSongFollowChords(tempoPopup.song, followBox.checked)
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
