import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// On stage: huge current patch, what comes next, big previous/next buttons.
// Nothing here edits the setlist.
Rectangle {
    id: perform

    required property DocumentController doc
    required property SetlistModel setlistModel
    required property PluginListModel pluginModel
    required property EngineStatus engineStatus
    required property ChannelModel channelModel
    required property bool sidePanelOpen
    required property bool mixerOpen
    property EffectWindows effectWindows: null

    color: Theme.performBackground

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            SidePanel {
                visible: perform.sidePanelOpen
                Layout.preferredWidth: Theme.sidePanelWidth
                Layout.fillHeight: true
                doc: perform.doc
                setlistModel: perform.setlistModel
                pluginModel: perform.pluginModel
                editable: false
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.spacing * 5
                spacing: Theme.spacing * 2

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: perform.doc.currentSongName
                    color: Theme.textDim
                    font.pixelSize: Theme.performSubtitleSize
                    elide: Text.ElideRight
                }
                Label {
                    objectName: "performPatchName"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: perform.doc.hasPatch ? perform.doc.currentPatchName : qsTr("No patch")
                    color: Theme.accent
                    font.pixelSize: Theme.performTitleSize
                    font.bold: true
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 24
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: perform.doc.nextPatchLabel === "" ? qsTr("End of set")
                                                             : qsTr("Next: %1").arg(perform.doc.nextPatchLabel)
                    font.pixelSize: Theme.performSubtitleSize
                    elide: Text.ElideRight
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing * 2
                    Button {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        text: qsTr("◀  Previous")
                        font.pixelSize: Theme.performSubtitleSize
                        focusPolicy: Qt.NoFocus
                        onClicked: perform.doc.previousPatch()
                    }
                    Button {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        text: qsTr("Next  ▶")
                        font.pixelSize: Theme.performSubtitleSize
                        highlighted: true
                        focusPolicy: Qt.NoFocus
                        onClicked: perform.doc.nextPatch()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing * 2
                    Label { text: qsTr("Master"); color: Theme.textDim }
                    Slider {
                        Layout.preferredWidth: 260
                        from: -60
                        to: 12
                        value: perform.engineStatus.masterVolumeDb
                        focusPolicy: Qt.NoFocus
                        onMoved: perform.engineStatus.masterVolumeDb = value
                    }
                    StatBox {
                    label: ""
                    value: perform.engineStatus.masterVolumeDb.toFixed(1) + " dB"
                    widest: "-60.0 dB"
                    valueColor: Theme.textDim
                }
                    Item { Layout.fillWidth: true }
                    StatBox {
                    label: qsTr("CPU")
                    value: Math.round(perform.engineStatus.cpuLoad * 100) + "%"
                    widest: "100%"
                    valueColor: perform.engineStatus.cpuLoad > 0.8 ? Theme.danger : Theme.text
                }
                    Rectangle {
                        width: 12
                        height: 12
                        radius: 6
                        color: perform.engineStatus.midiActivity ? Theme.meterLow : Theme.border
                    }
                }
            }

        }

        Mixer {
            visible: perform.mixerOpen
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.mixerHeight
            doc: perform.doc
            channelModel: perform.channelModel
            pluginModel: perform.pluginModel
            engineStatus: perform.engineStatus
            effectWindows: perform.effectWindows
        }
    }
}
