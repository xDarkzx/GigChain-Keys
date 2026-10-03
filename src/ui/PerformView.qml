pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// On stage, as MainStage and Gig Performer lay it out: a slim header
// (previous, the song and what comes next, next), the song's parts as tiles
// (the one playing lit; a tap jumps there), and the chart filling the rest,
// big enough to read from the keys, scrolled by chord follow. Panic is the
// toolbar's. Pedals and pads learned in Settings switch songs and parts too.
// Nothing here edits the setlist.
Rectangle {
    id: perform
    objectName: "performView"

    required property DocumentController doc
    required property SetlistModel setlistModel
    required property PluginListModel pluginModel
    required property EngineStatus engineStatus
    required property ChannelModel channelModel
    required property bool sidePanelOpen
    required property bool mixerOpen
    property SettingsController settings: null
    property EffectWindows effectWindows: null
    property MasterBus masterBus: null
    property LoopController loops: null

    // "Add lyrics & chords": to the chart editor.
    signal editChartRequested()

    readonly property bool hasChart: perform.doc.currentChart.trim() !== ""
    readonly property real chartSize: perform.settings !== null ? perform.settings.chartTextSize : 1.7

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
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                Layout.topMargin: Theme.spacing
                Layout.bottomMargin: Theme.spacing
                spacing: Theme.spacing

                // ◀ the song · its patch · what comes next ▶
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingLarge
                    StageButton {
                        objectName: "performPrevious"
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 52
                        iconSource: "icons/chevron-left.svg"
                        iconSize: 26
                        tip: qsTr("Previous song")
                        onClicked: perform.doc.previousSong()
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            objectName: "performSongName"
                            Layout.fillWidth: true
                            text: perform.doc.currentSongName !== "" ? perform.doc.currentSongName : qsTr("No song")
                            color: Theme.accent
                            font.pixelSize: Theme.performSubtitleSize + 4
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingLarge
                            Label {
                                objectName: "performPatchName"
                                text: perform.doc.hasPatch ? perform.doc.currentPatchName : qsTr("No patch")
                                color: Theme.text
                                font.pixelSize: Theme.fontSize + 3
                            }
                            Label {
                                Layout.fillWidth: true
                                text: perform.doc.nextPatchLabel === "" ? qsTr("End of set")
                                                                         : qsTr("Next: %1").arg(perform.doc.nextPatchLabel)
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSize + 3
                                elide: Text.ElideRight
                            }
                        }
                    }
                    // The chart's size, kept for next time.
                    Row {
                        visible: perform.hasChart
                        spacing: -1
                        StageButton {
                            objectName: "performSmaller"
                            text: "A−"
                            tip: qsTr("Smaller chart text")
                            enabled: perform.chartSize > 1.0
                            onClicked: if (perform.settings !== null) perform.settings.chartTextSize = perform.chartSize - 0.15
                        }
                        StageButton {
                            objectName: "performBigger"
                            text: "A+"
                            tip: qsTr("Bigger chart text")
                            enabled: perform.chartSize < 3.0
                            onClicked: if (perform.settings !== null) perform.settings.chartTextSize = perform.chartSize + 0.15
                        }
                    }
                    StageButton {
                        objectName: "performNext"
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 52
                        iconSource: "icons/chevron-right.svg"
                        iconSize: 26
                        tone: "accent"
                        tip: qsTr("Next song")
                        onClicked: perform.doc.nextSong()
                    }
                }

                // The song's parts (its chart's sections), as Gig Performer's
                // tiles: the one playing lit, a tap goes there.
                Flow {
                    id: parts
                    objectName: "performParts"
                    Layout.fillWidth: true
                    visible: count > 0
                    spacing: Theme.spacing
                    readonly property int count: perform.doc.currentSections.length
                    function choose(index) { perform.doc.selectSection(index) }
                    Repeater {
                        model: perform.doc.currentSections
                        StageButton {
                            id: part
                            required property var modelData
                            required property int index
                            height: 40
                            width: Math.max(110, implicitWidth + 24)
                            text: part.modelData.label !== "" ? part.modelData.label : part.modelData.name
                            font.pixelSize: Theme.fontSize + 2
                            checked: part.index === perform.engineStatus.songSection
                            onClicked: parts.choose(part.index)
                        }
                    }
                }

                // The whole song, big enough to read from the keys.
                Flickable {
                    id: performChart
                    objectName: "performChart"
                    visible: perform.hasChart
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
                    // Chord follow: the line being played stays in the upper third.
                    NumberAnimation {
                        id: followScroll
                        target: performChart
                        property: "contentY"
                        duration: 250
                        easing.type: Easing.OutCubic
                    }
                    Connections {
                        target: stageChart
                        function onFollowYChanged() {
                            if (stageChart.followY < 0) return
                            followScroll.to = Math.max(0, Math.min(stageChart.followY - performChart.height / 3,
                                                                   performChart.contentHeight - performChart.height))
                            followScroll.restart()
                        }
                    }
                    ChartView {
                        id: stageChart
                        width: performChart.width - 16
                        size: perform.chartSize
                        editable: false
                        lines: perform.doc.chartLines(perform.doc.currentChart)
                        doc: perform.doc
                        currentSection: perform.engineStatus.songSection
                        playing: perform.engineStatus.songPlaying
                        bar: perform.engineStatus.songBar
                        currentStep: perform.engineStatus.chordStep
                        followStarted: perform.engineStatus.chordStarted
                    }
                }

                // No lyrics or chords yet: say so, and lead to the editor.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !perform.hasChart
                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.spacingLarge
                        Label {
                            objectName: "performNoChart"
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("No lyrics or chords for this song")
                            color: Theme.textDim
                            font.pixelSize: Theme.performSubtitleSize
                        }
                        StageButton {
                            objectName: "performAddChart"
                            anchors.horizontalCenter: parent.horizontalCenter
                            height: 40
                            text: qsTr("Add lyrics & chords")
                            iconSource: "icons/plus.svg"
                            tip: qsTr("Type them, paste them, or import a chord sheet in the Chart tab")
                            onClicked: perform.editChartRequested()
                        }
                    }
                }

                // The master and how the computer is doing, small.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing * 2
                    Label { text: qsTr("Master"); color: Theme.textDim }
                    Slider {
                        id: performMaster
                        Layout.preferredWidth: 220
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
                        Layout.preferredWidth: 12
                        Layout.preferredHeight: 12
                        radius: 6
                        color: perform.engineStatus.midiActivity ? Theme.meterLow : Theme.border
                    }
                }
            }
        }

        Mixer {
            visible: perform.mixerOpen
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.mixerHeight + (perform.loops !== null && perform.loops.stripVisible ? Theme.looperHeight : 0)
            doc: perform.doc
            channelModel: perform.channelModel
            pluginModel: perform.pluginModel
            engineStatus: perform.engineStatus
            effectWindows: perform.effectWindows
            masterBus: perform.masterBus
            loops: perform.loops
        }
    }
}
