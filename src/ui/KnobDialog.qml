pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Keyboard knobs, faders and pedals that move a plugin's settings on this
// channel (MainStage's screen controls). Learn: choose the setting (from the
// list, or by moving it in an effect's own window), then move the knob.
StageDialog {
    id: dialog

    required property DocumentController doc
    required property SelectedChannel channel
    required property EngineStatus engineStatus

    property var mappingList: []
    property int target: -1          // -1 = the instrument, else the effect's position
    property var parameterList: []   // of the chosen plugin
    property string filter: ""

    function refresh() {
        mappingList = doc.mappings(channel.index)
        parameterList = engineStatus.parameters(channel.index, target)
    }

    title: qsTr("Knobs: %1").arg(dialog.channel.name)
    width: 860
    height: Math.min(600, (parent ? parent.height : 600) - 32)

    onAboutToShow: {
        target = channel.instrumentName !== "" ? -1 : 0
        filter = ""
        refresh()
    }
    onClosed: engineStatus.cancelMappingLearn()

    Connections {
        target: dialog.doc
        function onChannelUpdated() { if (dialog.visible) dialog.mappingList = dialog.doc.mappings(dialog.channel.index) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ------------------------------------------------ the mappings
        SettingsSection { title: qsTr("Mapped knobs") }
        Label {
            Layout.leftMargin: 20
            visible: dialog.mappingList.length === 0
            text: qsTr("None yet: learn one below.")
            color: Theme.textDim
        }
        ListView {
            objectName: "mappingList"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.preferredHeight: Math.min(contentHeight, 150)
            clip: true
            model: dialog.mappingList
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                id: mappingRow
                required property var modelData
                required property int index
                width: ListView.view.width
                spacing: 8
                Label {
                    Layout.preferredWidth: 90
                    text: qsTr("CC %1").arg(mappingRow.modelData.controller)
                    color: Theme.text
                }
                Label {
                    Layout.fillWidth: true
                    text: mappingRow.modelData.targetName + " › " + mappingRow.modelData.parameterName
                    elide: Text.ElideRight
                }
                // The parameter's range over the knob's travel.
                RangeSlider {
                    Layout.preferredWidth: 150
                    from: 0; to: 1
                    first.value: Math.min(mappingRow.modelData.minimum, mappingRow.modelData.maximum)
                    second.value: Math.max(mappingRow.modelData.minimum, mappingRow.modelData.maximum)
                    first.onMoved: dialog.doc.setMappingRange(dialog.channel.index, mappingRow.index, first.value, second.value)
                    second.onMoved: dialog.doc.setMappingRange(dialog.channel.index, mappingRow.index, first.value, second.value)
                }
                // How the knob's travel feels (an expression pedal wants a gentle start).
                StageComboBox {
                    objectName: "mappingCurve"
                    implicitWidth: 120
                    model: [qsTr("Straight"), qsTr("Gentle start"), qsTr("Quick start")]
                    currentIndex: mappingRow.modelData.curve
                    onActivated: (i) => dialog.doc.setMappingCurve(dialog.channel.index, mappingRow.index, i)
                }
                // Pickup: the knob takes over only once it reaches the setting, so nothing jumps.
                CheckBox {
                    id: pickupBox
                    objectName: "mappingPickup"
                    text: qsTr("Pickup")
                    checked: mappingRow.modelData.pickup
                    focusPolicy: Qt.NoFocus
                    onToggled: dialog.doc.setMappingPickup(dialog.channel.index, mappingRow.index, pickupBox.checked)
                }
                StageButton {
                    iconSource: "icons/x.svg"
                    iconSize: 12
                    tip: qsTr("Remove this knob")
                    onClicked: dialog.doc.removeMapping(dialog.channel.index, mappingRow.index)
                }
            }
        }

        // ------------------------------------------------ learning
        StageDivider { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }
        SettingsSection { title: qsTr("Learn a knob") }
        SettingsRow {
            label: qsTr("Plugin")
            StageComboBox {
                objectName: "mappingTargetBox"
                // The instrument (if any), then each effect.
                readonly property var targets: (dialog.channel.instrumentName !== "" ? [{ t: -1, name: dialog.channel.instrumentName }] : [])
                                               .concat(dialog.channel.effectNames.map((n, i) => ({ t: i, name: n })))
                model: targets.map((x) => x.name)
                currentIndex: Math.max(0, targets.findIndex((x) => x.t === dialog.target))
                onActivated: (i) => {
                    dialog.target = targets[i].t
                    dialog.engineStatus.cancelMappingLearn()
                    dialog.refresh()
                }
            }
        }
        SettingsRow {
            label: qsTr("Setting")
            StageTextField {
                id: filterField
                iconSource: "icons/search.svg"
                Layout.fillWidth: true
                placeholderText: qsTr("Search the plugin's settings")
                onTextChanged: dialog.filter = text.toLowerCase()
            }
        }
        ListView {
            id: parameterView
            objectName: "parameterList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 180
            Layout.rightMargin: 20
            clip: true
            model: dialog.parameterList.filter((p) => dialog.filter === "" || p.name.toLowerCase().indexOf(dialog.filter) >= 0)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: parameterRow
                required property var modelData
                width: ListView.view.width
                implicitHeight: 26
                topPadding: 0
                bottomPadding: 0
                leftPadding: 8
                rightPadding: 8
                highlighted: dialog.engineStatus.learnedParameter === modelData.name
                contentItem: Text {
                    text: parameterRow.modelData.name
                    color: parameterRow.highlighted ? "white" : Theme.text
                    font.pixelSize: Theme.fontSize
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { color: parameterRow.highlighted ? Theme.accentBlue : (parameterRow.hovered ? Theme.slotHover : "transparent"); radius: 3 }
                onClicked: {
                    if (!dialog.engineStatus.learningMapping) dialog.engineStatus.startMappingLearn(dialog.channel.index, dialog.target)
                    dialog.engineStatus.setLearnParameter(modelData.id, modelData.name)
                }
            }
            Label {
                anchors.centerIn: parent
                visible: dialog.parameterList.length === 0
                text: qsTr("This plugin lists no settings a knob can move")
                color: Theme.textDim
            }
        }

        StageDivider { Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0
            spacing: 10
            StageButton {
                objectName: "learnButton"
                text: dialog.engineStatus.learningMapping ? qsTr("Stop learning") : qsTr("Learn")
                checked: dialog.engineStatus.learningMapping
                onClicked: dialog.engineStatus.learningMapping ? dialog.engineStatus.cancelMappingLearn()
                                                               : dialog.engineStatus.startMappingLearn(dialog.channel.index, dialog.target)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: dialog.engineStatus.learningMapping ? Theme.text : Theme.textDim
                text: !dialog.engineStatus.learningMapping
                      ? qsTr("Pick a setting above (or click Learn and move it in an effect's window), then move a knob.")
                      : (dialog.engineStatus.learnedParameter === "" ? qsTr("Now pick the setting it moves…")
                                                                   : qsTr("%1: now move a knob on your keyboard…").arg(dialog.engineStatus.learnedParameter))
                        + (dialog.engineStatus.learnedKnob !== "" ? "  " + dialog.engineStatus.learnedKnob : "")
            }
            StageButton {
                text: qsTr("Done")
                tone: "accent"
                onClicked: dialog.close()
            }
        }
    }
}
