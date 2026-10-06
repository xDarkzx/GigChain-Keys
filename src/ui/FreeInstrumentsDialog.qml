import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Help > Get free instruments: the free pianos, synths and pads worth
// installing, each a click from its download page.
StageDialog {
    id: dialog
    objectName: "freeInstrumentsDialog"

    title: qsTr("Free instruments")
    width: 560

    ColumnLayout {
        width: dialog.width
        spacing: Theme.spacing

        Label {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            Layout.bottomMargin: 0
            text: qsTr("%1 plays the VST3 instruments on your computer. No instruments yet, or want more? These are free:").arg(Branding.name)
            color: Theme.text
            wrapMode: Text.Wrap
        }
        FreeInstruments {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge
            Layout.rightMargin: Theme.spacingLarge
        }
        StageDivider { Layout.fillWidth: true; Layout.topMargin: Theme.spacing }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            Item { Layout.fillWidth: true }
            StageButton { text: qsTr("Close"); tone: "accent"; onClicked: dialog.close() }
        }
    }
}
