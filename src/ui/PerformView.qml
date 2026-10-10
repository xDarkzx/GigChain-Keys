pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// On stage, as MainStage and Gig Performer lay it out: a slim header
// (previous, the song and what comes next, next), the song's transport (Play,
// Next part, Loop part: big, for a finger), its parts as tiles (the one
// playing lit; a tap goes there at the next bar line, as Playback does), and
// the chart filling the rest, big enough to read from the keys, scrolled
// along as the song plays. Panic is the toolbar's. Pedals and pads learned
// in Settings switch songs and parts too. Nothing here edits the setlist.
Rectangle {
    id: perform
    objectName: "performView"

    // A chord tapped in the chart: how to play it.
    ChordDiagram {
        id: performDiagram
        doc: perform.doc
    }

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
    property MasterBus auxBus: null
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
                    // The song and patch on a big display, as MainStage's patch LCD.
                    LcdPanel {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52 // (as tall as the buttons beside it: the chart keeps the screen)
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingLarge
                        anchors.rightMargin: Theme.spacingLarge
                        anchors.topMargin: 2
                        anchors.bottomMargin: 3
                        spacing: -2
                        Label {
                            objectName: "performSongName"
                            Layout.fillWidth: true
                            text: perform.doc.currentSongName !== "" ? perform.doc.currentSongName : qsTr("No song")
                            color: Theme.lcdText
                            font.pixelSize: Theme.performSubtitleSize - 6
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingLarge
                            Label {
                                objectName: "performPatchName"
                                text: perform.doc.hasPatch ? perform.doc.currentPatchName : qsTr("No patch")
                                color: Theme.lcdAccent
                                font.pixelSize: Theme.fontSize
                                font.bold: true
                            }
                            Label {
                                Layout.fillWidth: true
                                text: perform.doc.nextPatchLabel === "" ? qsTr("End of set")
                                                                         : qsTr("Next: %1").arg(perform.doc.nextPatchLabel)
                                color: Theme.lcdTextDim
                                font.pixelSize: Theme.fontSize
                                elide: Text.ElideRight
                            }
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

                // The deck: the transport and the parts, raised pads on a
                // panel of their own, as a control surface's.
                Rectangle {
                    id: deck
                    Layout.fillWidth: true
                    visible: perform.doc.canPlaySong || parts.count > 0
                    implicitHeight: deckColumn.implicitHeight + 2 * Theme.spacing
                    radius: Theme.radiusCard
                    border.color: Theme.outline
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.panelRaised }
                        GradientStop { position: 1.0; color: Theme.panelBottom }
                    }
                    Rectangle { x: 2; y: 1; width: parent.width - 4; height: 1; color: Theme.bevelLight }
                ColumnLayout {
                    id: deckColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
                    spacing: Theme.spacing
                // The song's transport: Play / Stop, on to the next part (at
                // the next bar line), loop the part playing, and where it is.
                RowLayout {
                    objectName: "performTransport"
                    Layout.fillWidth: true
                    visible: perform.doc.canPlaySong
                    spacing: Theme.spacing
                    StageButton {
                        objectName: "performPlay"
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: Theme.touchTarget
                        iconSource: perform.engineStatus.songPlaying ? "icons/player-stop.svg" : "icons/player-play.svg"
                        iconSize: 24
                        text: perform.engineStatus.songPlaying ? qsTr("Stop") : qsTr("Play")
                        font.pixelSize: Theme.fontSize + 3
                        font.bold: true
                        tone: perform.engineStatus.songPlaying ? "normal" : "accent"
                        checked: perform.engineStatus.songPlaying
                        tip: perform.engineStatus.songPlaying ? qsTr("Stop the song (Space)")
                                                              : qsTr("Play the song at its tempo along its parts (Space)")
                        onClicked: perform.engineStatus.songPlaying ? perform.doc.stopSong() : perform.doc.playSong()
                    }
                    StageButton {
                        objectName: "performNextPart"
                        Layout.preferredHeight: Theme.touchTarget
                        iconSource: "icons/chevron-right.svg"
                        iconSize: 22
                        text: qsTr("Next part")
                        font.pixelSize: Theme.fontSize + 2
                        tip: qsTr("Playing: on to the next part at the next bar line (N). Stopped: the next part is where Play starts")
                        onClicked: perform.doc.nextPart()
                    }
                    StageButton {
                        objectName: "performLoopPart"
                        Layout.preferredHeight: Theme.touchTarget
                        iconSource: "icons/loop.svg"
                        iconSize: 22
                        text: qsTr("Loop part")
                        font.pixelSize: Theme.fontSize + 2
                        enabled: perform.engineStatus.songPlaying
                        checked: perform.engineStatus.songHold
                        tip: qsTr("Play this part again and again until tapped again (H)")
                        onClicked: perform.doc.holdPart()
                    }
                    Label {
                        objectName: "performWhere"
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                        text: perform.engineStatus.songCountingIn ? qsTr("Count-in…")
                              : !perform.engineStatus.songPlaying ? ""
                              : qsTr("Bar %1 of %2").arg(perform.engineStatus.songBar).arg(perform.engineStatus.songBars)
                                + (perform.engineStatus.songQueued !== "" ? "   " + perform.engineStatus.songQueued : "")
                        color: Theme.chord
                        font.pixelSize: Theme.fontSize + 3
                        font.bold: true
                    }
                }

                // The song's parts in the order it is played (its flow), as
                // Gig Performer's tiles: the one playing lit, a tap goes
                // to that very part (playing: at the next bar line).
                Flow {
                    id: parts
                    objectName: "performParts"
                    Layout.fillWidth: true
                    visible: count > 0
                    spacing: Theme.spacing
                    readonly property int count: perform.doc.songFlow.length
                    // The part playing, or the one Play starts from.
                    readonly property int playingPart: perform.engineStatus.songPlace
                    // A tile tapped: that very part of the flow.
                    function choose(place) { perform.doc.selectFlowPart(place) }
                    Repeater {
                        model: perform.doc.songFlow
                        StageButton {
                            id: part
                            required property var modelData
                            required property int index
                            objectName: "performPart"
                            height: Theme.touchTarget
                            width: Math.max(110, implicitWidth + 24)
                            text: part.modelData.label
                            font.pixelSize: Theme.fontSize + 2
                            checked: parts.playingPart >= 0 ? part.index === parts.playingPart
                                                            : part.modelData.section === perform.engineStatus.songSection
                            onClicked: parts.choose(part.index)
                            // Playing: how far through the part (a bar along its foot).
                            Rectangle {
                                objectName: "performPartProgress"
                                visible: part.checked && perform.engineStatus.songPlaying
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                anchors.margins: 3
                                height: 3
                                radius: 1.5
                                width: (parent.width - 6) * perform.engineStatus.songProgress
                                color: Theme.text
                                opacity: 0.85
                            }
                            // Queued for the next bar line: outlined.
                            Rectangle {
                                objectName: "performPartQueued"
                                visible: perform.engineStatus.songQueuedPlace === part.index
                                anchors.fill: parent
                                color: "transparent"
                                radius: Theme.radiusCard
                                border.color: Theme.chord
                                border.width: 2
                            }
                        }
                    }
                }
                }
                }

                // The whole song, big enough to read from the keys, set into
                // the surface (a framed well, not floating on black).
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: perform.hasChart
                    radius: Theme.radiusCard
                    border.color: Theme.wellBorder
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.chartWellTop }
                        GradientStop { position: 1.0; color: Theme.chartWellBottom }
                    }
                    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 3; radius: 2; color: Theme.wellShadow }
                Flickable {
                    id: performChart
                    objectName: "performChart"
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
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
                    // Playing: the line it has come to stays in the upper third.
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
                        onChordClicked: (name) => performDiagram.show(name) // forgot it? how to play it
                    }
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
                        // A hardware fader on its side: a cut groove lit up to the cap, and a metal cap.
                        background: Rectangle {
                            x: performMaster.leftPadding
                            y: performMaster.topPadding + performMaster.availableHeight / 2 - height / 2
                            width: performMaster.availableWidth
                            height: 6
                            radius: 3
                            color: Theme.faderGroove
                            border.color: "#000000"
                            Rectangle {
                                width: performMaster.visualPosition * parent.width
                                height: parent.height
                                radius: 3
                                color: Theme.ledBlue
                                opacity: 0.55
                            }
                        }
                        handle: Rectangle {
                            x: performMaster.leftPadding + performMaster.visualPosition * (performMaster.availableWidth - width)
                            y: performMaster.topPadding + performMaster.availableHeight / 2 - height / 2
                            width: 34
                            height: 22
                            radius: 3
                            border.color: "#0d0d0e"
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: Theme.faderCapTop }
                                GradientStop { position: 0.48; color: Theme.faderCapMid }
                                GradientStop { position: 0.52; color: Theme.faderCapBottom }
                                GradientStop { position: 1.0; color: Theme.faderCapMid }
                            }
                            Rectangle { anchors.centerIn: parent; width: 2; height: parent.height - 2; color: "#ffffff" }
                        }
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
            editable: false
            visible: perform.mixerOpen
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.mixerHeight + (perform.loops !== null && perform.loops.stripVisible ? Theme.looperHeight : 0)
            doc: perform.doc
            channelModel: perform.channelModel
            pluginModel: perform.pluginModel
            engineStatus: perform.engineStatus
            effectWindows: perform.effectWindows
            masterBus: perform.masterBus
            auxBus: perform.auxBus
            loops: perform.loops
        }
    }
}
