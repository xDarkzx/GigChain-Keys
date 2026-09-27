import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// On stage: the song's chart, big enough to read from the keys; where we
// are and what comes next; song buttons and Panic. Pedals and pads learned
// in Settings switch songs too. Nothing here edits the setlist.
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
    property MasterBus masterBus: null

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

                // Where we are, and what comes next.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing * 2
                    Label {
                        objectName: "performSongName"
                        Layout.fillWidth: true
                        text: perform.doc.currentSongName !== "" ? perform.doc.currentSongName : qsTr("No song")
                        color: Theme.accent
                        font.pixelSize: Theme.performSubtitleSize + 10
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Label {
                        objectName: "performPatchName"
                        text: perform.doc.hasPatch ? perform.doc.currentPatchName : qsTr("No patch")
                        color: Theme.textDim
                        font.pixelSize: Theme.performSubtitleSize - 6
                    }
                    Label {
                        Layout.maximumWidth: perform.width / 3
                        horizontalAlignment: Text.AlignRight
                        text: perform.doc.nextPatchLabel === "" ? qsTr("End of set")
                                                                 : qsTr("Next: %1").arg(perform.doc.nextPatchLabel)
                        color: Theme.textDim
                        font.pixelSize: Theme.performSubtitleSize - 6
                        elide: Text.ElideRight
                    }
                }

                // The whole song, big enough to read from the keys.
                Flickable {
                    id: performChart
                    objectName: "performChart"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: stageChart.height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    // A new song starts at its top.
                    Connections {
                        target: perform.doc
                        function onChartChanged() { performChart.contentY = 0 }
                    }
                    ChartView {
                        id: stageChart
                        width: performChart.width - 16
                        size: 1.7
                        lines: perform.doc.chartLines(perform.doc.currentChart)
                        doc: perform.doc
                        currentSection: perform.engineStatus.songSection
                        playing: perform.engineStatus.songPlaying
                        bar: perform.engineStatus.songBar
                    }
                    Label {
                        visible: perform.doc.currentChart.trim() === ""
                        anchors.centerIn: parent
                        text: qsTr("No chart for this song")
                        color: Theme.textDim
                        font.pixelSize: Theme.performSubtitleSize
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing * 2
                    StageButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        text: qsTr("Previous song")
                        iconSource: "icons/chevron-left.svg"
                        iconSize: 28
                        font.pixelSize: Theme.performSubtitleSize - 6
                        onClicked: perform.doc.previousSong()
                    }
                    StageButton {
                        objectName: "performPanic"
                        Layout.preferredWidth: 180
                        Layout.preferredHeight: 72
                        text: qsTr("Panic")
                        iconSource: "icons/alert-octagon.svg"
                        iconSize: 28
                        tone: "danger"
                        font.pixelSize: Theme.performSubtitleSize - 6
                        onClicked: perform.engineStatus.panic()
                        tip: qsTr("Stop every sound now (stuck notes, runaway effects)")
                    }
                    StageButton {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 72
                        text: qsTr("Next song")
                        iconSource: "icons/chevron-right.svg"
                        iconSize: 28
                        tone: "accent"
                        font.pixelSize: Theme.performSubtitleSize - 6
                        onClicked: perform.doc.nextSong()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing * 2
                    Label { text: qsTr("Master"); color: Theme.textDim }
                    Slider {
                        id: performMaster
                        Layout.preferredWidth: 260
                        from: -60
                        to: 12
                        Binding on value { // keeps following after a drag
                            value: perform.engineStatus.masterVolumeDb
                            when: !performMaster.pressed
                            restoreMode: Binding.RestoreNone
                        }
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
            masterBus: perform.masterBus
        }
    }
}
