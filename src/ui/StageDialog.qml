import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The frame every dialog shares: a shadowed, outlined panel with a raised
// title bar and an engraved line under it. Put the dialog's content inside.
Popup {
    id: dialog

    property string title
    default property alias content: body.data

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    padding: 0
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: Theme.overlay }

    background: Item {
        // A soft shadow under the dialog.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            anchors.topMargin: -2
            radius: Theme.radiusDialog + 6
            color: Theme.shadow
            opacity: 0.6
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusDialog
            color: Theme.panel
            border.color: Theme.outline
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Title bar.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            topLeftRadius: Theme.radiusDialog
            topRightRadius: Theme.radiusDialog
            border.color: Theme.outline
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.barTop }
                GradientStop { position: 1.0; color: Theme.barBottom }
            }
            Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelLight }
            Label {
                anchors.fill: parent
                leftPadding: Theme.spacingLarge
                rightPadding: Theme.spacingLarge
                verticalAlignment: Text.AlignVCenter
                text: dialog.title
                font.pixelSize: Theme.titleFontSize
                font.bold: true
                color: Theme.text
                elide: Text.ElideRight
            }
        }
        StageDivider { Layout.fillWidth: true }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: childrenRect.height
            implicitWidth: childrenRect.width
        }
    }
}
