import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The main area: the selected channel's own plugin window, as big as it can
// be (the toolbar already names the song and patch). The plugin does the
// heavy lifting here. Above it, MIDI Learn: the plugin draws its own window
// (its right-click is its own), so a knob in it is learned from here: click
// MIDI Learn, move the knob in the plugin, then a knob on the keyboard.
Rectangle {
    id: area

    required property DocumentController doc
    required property EngineStatus engineStatus
    required property EditorService editorService
    property bool suspended: false

    color: Theme.background

    PayloadDropArea {
        anchors.fill: parent
        keys: ["instrument"]
        onPayloadDropped: (payload) => area.doc.addChannel(payload.pluginId, payload.name)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            objectName: "pluginLearnBar"
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            visible: editorHost.hasEditor
            spacing: Theme.spacing
            StageButton {
                objectName: "pluginMidiLearn"
                text: area.engineStatus.learningMapping ? qsTr("Stop MIDI Learn") : qsTr("MIDI Learn")
                iconSource: "icons/plus.svg"
                checked: area.engineStatus.learningMapping
                tip: qsTr("Move a knob in the plugin, then a knob or fader on your keyboard: that knob moves it from now on")
                onClicked: area.engineStatus.learningMapping ? area.engineStatus.cancelMappingLearn()
                                                             : area.engineStatus.startMappingLearn(area.doc.selectedChannel, -1)
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.textDim
                text: qsTr("Click MIDI Learn, move a knob in the plugin, then turn a knob on your keyboard.")
                visible: !area.engineStatus.learningMapping
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Shown only where no plugin window covers the area.
            Label {
                anchors.centerIn: parent
                visible: !editorHost.hasEditor
                text: editorHost.emptyReason
                color: Theme.textDim
                font.pixelSize: Theme.headerFontSize
            }

            PluginEditorHost {
                id: editorHost
                objectName: "pluginEditorHost"
                anchors.fill: parent
                service: area.editorService
                suspended: area.suspended
            }

        }
    }
}
