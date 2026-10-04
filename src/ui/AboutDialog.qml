import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Help > About: the splash picture, the version, what the app is, its
// licence and what it is built with.
StageDialog {
    id: about
    objectName: "aboutDialog"

    signal guideRequested()

    title: qsTr("About %1").arg(Branding.name)
    width: 600

    ColumnLayout {
        width: about.width
        spacing: Theme.spacing

        Image {
            objectName: "aboutSplash"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingLarge
            Layout.preferredWidth: about.width - 2 * Theme.spacingLarge
            Layout.preferredHeight: implicitHeight > 0 ? implicitHeight * (Layout.preferredWidth / implicitWidth) : 0
            source: "qrc:/branding/splash.png"
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
        Label {
            objectName: "aboutVersion"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("%1 · version %2").arg(Branding.name).arg(Branding.version)
            color: Theme.text
            font.pixelSize: Theme.titleFontSize + 2
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge * 2
            Layout.rightMargin: Theme.spacingLarge * 2
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("Your keyboard rig on stage: instruments and effects, the night's setlist, every song's chords and lyrics, and a place to practise them.")
            color: Theme.textDim
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge * 2
            Layout.rightMargin: Theme.spacingLarge * 2
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("Free for every player. © 2026 %1 contributors, under the GNU General Public License, version 3 or later.").arg(Branding.brand)
            color: Theme.textDim
            font.pixelSize: Theme.smallFontSize
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingLarge * 2
            Layout.rightMargin: Theme.spacingLarge * 2
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("Built with Qt, RtAudio, RtMidi and the Steinberg VST3 SDK. VST is a trademark of Steinberg Media Technologies GmbH.")
            color: Theme.textDim
            font.pixelSize: Theme.smallFontSize
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            spacing: Theme.spacing
            StageButton {
                text: qsTr("User guide")
                onClicked: {
                    about.close()
                    about.guideRequested()
                }
            }
            StageButton {
                text: qsTr("Website")
                tip: Branding.website
                onClicked: Qt.openUrlExternally(Branding.website)
            }
            Item { Layout.fillWidth: true }
            StageButton {
                objectName: "aboutClose"
                text: qsTr("Close")
                tone: "accent"
                onClicked: about.close()
            }
        }
    }
}
