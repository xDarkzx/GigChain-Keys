import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Free instruments worth having (pianos, synths, pads, electric pianos), each
// with a button that opens its download page. Shown when no instruments are
// installed, and from Help > Get free instruments. They are the makers' own
// downloads: the app finds them the next time it starts.
ColumnLayout {
    id: list
    spacing: Theme.spacing

    // What each one is, and where it is downloaded (the makers' own pages).
    readonly property var instruments: [
        { name: "Splice INSTRUMENT (LABS)", what: qsTr("Pianos, pads, strings and more: Spitfire's LABS sounds. Free with a Splice account."),
          url: "https://splice.com/instrument/labs-instrument" },
        { name: "Surge XT", what: qsTr("A big synth: hundreds of pads, leads, basses and keys. Open source."),
          url: "https://surge-synthesizer.github.io/" },
        { name: "Vital", what: qsTr("A modern synth with lush pads and leads. The basic version is free with an account."),
          url: "https://vital.audio/" },
        { name: "Dexed", what: qsTr("Classic 80s electric pianos, bells and basses (a DX7). Open source."),
          url: "https://asb2m10.github.io/dexed/" },
        { name: "Salamander Grand Piano", what: qsTr("A sampled concert grand. Plays in the free sfizz player (the page says how)."),
          url: "https://sfzinstruments.github.io/pianos/salamander" }
    ]

    Repeater {
        model: list.instruments
        delegate: Rectangle {
            id: entry
            required property var modelData
            objectName: "freeInstrument"
            readonly property string url: modelData.url
            Layout.fillWidth: true
            implicitHeight: row.implicitHeight + 2 * Theme.spacing
            radius: Theme.radiusCard
            color: Theme.panelRaised
            border.color: Theme.outline
            RowLayout {
                id: row
                anchors.fill: parent
                anchors.margins: Theme.spacing
                spacing: Theme.spacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: entry.modelData.name
                        color: Theme.text
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: entry.modelData.what
                        color: Theme.textDim
                        font.pixelSize: Theme.smallFontSize
                        wrapMode: Text.Wrap
                    }
                }
                StageButton {
                    objectName: "freeInstrumentGet"
                    text: qsTr("Get it")
                    tip: entry.url
                    onClicked: Qt.openUrlExternally(entry.url)
                }
            }
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Installed one? Close and reopen %1: it finds new instruments when it starts.").arg(Branding.name)
        color: Theme.textDim
        font.pixelSize: Theme.smallFontSize
        wrapMode: Text.Wrap
    }
}
