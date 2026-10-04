pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// A section's title in a chart (Intro, Verse 1, Chorus...), centred and
// larger than the lyrics so it reads at a glance, with what the section
// plays under it: a chip per instrument (✕ takes it out), [+] to add one of
// the patch's other instruments, and its length in bars (click to type).
// The section in force is lit; while the song plays it counts its bars.
// Clicking the title starts the song there (or goes there, when playing).
Column {
    id: header
    objectName: "sectionHeader"

    required property string label
    property int sectionIndex: -1
    // From DocumentController.currentSections; undefined when unknown.
    property var section
    property DocumentController doc: null
    property real size: 1.0
    property bool editable: true // false on stage: read, not edited
    property int chartLine: -1   // the title's line in the chart (to rename it)
    property bool current: false // the section in force
    property bool playing: false // the song is being counted
    property int bar: 0

    readonly property bool known: doc !== null && section !== undefined && section !== null
    readonly property color lit: header.current ? (header.playing ? Theme.chord : Theme.accent) : Theme.accentBlue

    topPadding: 14 * size
    bottomPadding: 6 * size
    spacing: 6 * size

    // The title.
    Item {
        width: header.width
        height: title.implicitHeight + 6 * header.size
        Rectangle {
            // The section in force: a lit band behind its title.
            visible: header.current && header.known
            anchors.centerIn: title
            width: title.implicitWidth + 36 * header.size
            height: title.implicitHeight + 6 * header.size
            radius: height / 2
            color: header.playing ? "#33f0bf45" : "#264a8fe7"
            border.color: header.lit
        }
        Text {
            id: title
            objectName: "sectionTitle"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            text: header.label
            color: header.lit
            font.pixelSize: (Theme.fontSize + 7) * 1.4 * header.size
            font.bold: true
            font.letterSpacing: 2 * header.size
            font.capitalization: Font.AllUppercase
            HoverHandler { id: titleHover; enabled: header.known; cursorShape: Qt.PointingHandCursor }
            // On stage: a tap goes there at once.
            TapHandler {
                enabled: header.known && !header.editable
                onTapped: header.doc.selectSection(header.sectionIndex)
            }
            // Editing: a click goes there, a double-click renames it.
            TapHandler {
                enabled: header.known && header.editable
                onSingleTapped: header.doc.selectSection(header.sectionIndex)
                onDoubleTapped: {
                    titleInput.text = header.label
                    titleInput.visible = true
                    titleInput.forceActiveFocus()
                    titleInput.selectAll()
                }
            }
            ToolTip.visible: titleHover.hovered && header.editable
            ToolTip.delay: 900
            ToolTip.text: qsTr("Click to go to this section · double-click to rename it")
        }
        TextInput {
            id: titleInput
            objectName: "sectionTitleInput"
            visible: false
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(contentWidth + 16, 160 * header.size)
            horizontalAlignment: TextInput.AlignHCenter
            color: header.lit
            font.pixelSize: title.font.pixelSize
            font.bold: true
            font.letterSpacing: title.font.letterSpacing
            selectByMouse: true
            Rectangle { anchors.fill: parent; z: -1; color: Theme.readoutBackground; border.color: Theme.accent; radius: Theme.radiusSmall }
            onAccepted: {
                visible = false
                if (text.trim() !== "" && text.trim() !== header.label) header.doc.renameChartSection(header.chartLine, text.trim())
            }
            Keys.onEscapePressed: visible = false
            onActiveFocusChanged: if (!activeFocus) visible = false
        }
    }

    // What it plays, and how long it is.
    Row {
        visible: header.known
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 6 * header.size

        Repeater {
            objectName: "sectionChips"
            model: header.known ? header.section.channels : []
            delegate: Rectangle {
                id: chip
                required property var modelData
                objectName: "sectionChip"
                height: 24 * header.size
                width: chipRow.implicitWidth + 16 * header.size
                radius: height / 2
                border.color: Theme.outline
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Theme.slotInstrumentTop }
                    GradientStop { position: 1.0; color: Theme.slotInstrument }
                }
                Row {
                    id: chipRow
                    anchors.centerIn: parent
                    spacing: 6 * header.size
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: chip.modelData.name
                        color: Theme.textOnAccent
                        font.pixelSize: Theme.smallFontSize * header.size
                        font.bold: true
                    }
                    Text {
                        objectName: "sectionChipRemove"
                        visible: header.editable
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✕"
                        color: removeHover.hovered ? Theme.text : "#b0ffffff"
                        font.pixelSize: Theme.smallFontSize * header.size
                        HoverHandler { id: removeHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: header.doc.removeSectionChannel(header.sectionIndex, chip.modelData.channel) }
                        ToolTip.visible: removeHover.hovered
                        ToolTip.text: qsTr("Not in this section")
                    }
                }
            }
        }
        // Nothing plays: say so (a break).
        Text {
            visible: header.known && header.section.channels.length === 0
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Silent")
            color: Theme.textDim
            font.pixelSize: Theme.smallFontSize * header.size
            font.italic: true
        }
        StageButton {
            id: addButton
            objectName: "sectionAdd"
            visible: header.editable
            anchors.verticalCenter: parent.verticalCenter
            implicitHeight: 24 * header.size
            text: "+"
            enabled: header.known && header.section.choices.length > 0
            tip: qsTr("Add an instrument of this patch to the section")
            onClicked: addMenu.popup(addButton, 0, addButton.height)
            StageMenu {
                id: addMenu
                objectName: "sectionAddMenu"
                Instantiator {
                    model: header.known ? header.section.choices : []
                    delegate: StageMenuItem {
                        required property var modelData
                        text: modelData.name
                        onTriggered: header.doc.addSectionChannel(header.sectionIndex, modelData.channel)
                    }
                    onObjectAdded: (index, object) => addMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => addMenu.removeItem(object)
                }
            }
        }

        // Its length: "8 bars" (click to type another), or where the count is.
        Item {
            id: barsField
            objectName: "sectionBars"
            readonly property bool editable: header.editable
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(barsText.implicitWidth, 40 * header.size)
            height: 24 * header.size
            readonly property int bars: header.known ? header.section.bars : 0
            Text {
                id: barsText
                anchors.centerIn: parent
                visible: !barsInput.visible
                text: header.current && header.playing && header.bar > 0
                          ? qsTr("bar %1 of %2").arg(header.bar).arg(barsField.bars)
                          : (barsField.bars === 1 ? qsTr("1 bar") : qsTr("%1 bars").arg(barsField.bars))
                color: header.current && header.playing ? Theme.chord : Theme.textDim
                font.pixelSize: Theme.smallFontSize * header.size
                font.bold: header.current && header.playing
                HoverHandler { id: barsHover; enabled: header.editable; cursorShape: Qt.IBeamCursor }
                ToolTip.visible: barsHover.hovered && header.editable
                ToolTip.text: header.known && header.section.guessed
                                  ? qsTr("Guessed from the chords: click to type the real length")
                                  : qsTr("Click to type the length in bars")
                TapHandler {
                    enabled: header.editable
                    onTapped: {
                        barsInput.text = String(barsField.bars)
                        barsInput.visible = true
                        barsInput.forceActiveFocus()
                        barsInput.selectAll()
                    }
                }
            }
            TextInput {
                id: barsInput
                objectName: "sectionBarsInput"
                anchors.fill: parent
                visible: false
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.text
                selectionColor: Theme.accent
                selectedTextColor: "white"
                font.pixelSize: Theme.smallFontSize * header.size
                validator: IntValidator { bottom: 1; top: 999 }
                onAccepted: {
                    visible = false
                    header.doc.setSectionBars(header.sectionIndex, parseInt(text))
                }
                Keys.onEscapePressed: visible = false
                onActiveFocusChanged: if (!activeFocus) visible = false
            }
        }
    }
}
