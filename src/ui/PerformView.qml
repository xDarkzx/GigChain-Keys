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
    required property ArtworkBuilder artworkBuilder
    required property EngineStatus engineStatus
    required property bool sidePanelOpen

    color: Theme.performBackground

    RowLayout {
        anchors.fill: parent
        spacing: 0

        SidePanel {
            visible: perform.sidePanelOpen
            Layout.preferredWidth: Theme.sidePanelWidth
            Layout.fillHeight: true
            doc: perform.doc
            setlistModel: perform.setlistModel
            pluginModel: perform.pluginModel
            artworkBuilder: perform.artworkBuilder
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
                Label { text: perform.engineStatus.masterVolumeDb.toFixed(1) + " dB"; color: Theme.textDim }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("CPU %1%").arg(Math.round(perform.engineStatus.cpuLoad * 100)) }
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: perform.engineStatus.midiActivity ? Theme.meterLow : Theme.border
                }
            }
        }
    }
}
