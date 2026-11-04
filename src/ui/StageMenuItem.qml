import QtQuick
import QtQuick.Controls

// One row of a StageMenu (also used for submenu entries).
MenuItem {
    id: item

    implicitHeight: 26
    implicitWidth: 220

    // Its text, after its icon when it has one (icon.source).
    contentItem: Item {
        readonly property bool hasIcon: item.icon.source.toString() !== ""
        Image {
            id: itemIcon
            x: item.checkable ? 26 : 8
            anchors.verticalCenter: parent.verticalCenter
            visible: parent.hasIcon
            source: item.icon.source
            sourceSize: Qt.size(16, 16)
            opacity: item.enabled ? 1 : 0.5
        }
        Text {
            anchors.fill: parent
            leftPadding: (item.checkable ? 26 : 8) + (parent.hasIcon ? 24 : 0)
            rightPadding: 18
            text: item.text
            color: item.enabled ? (item.highlighted ? "white" : Theme.text) : Theme.textDim
            font.pixelSize: Theme.fontSize
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    arrow: Text {
        x: item.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        visible: item.subMenu !== null
        text: "›"
        color: item.highlighted ? "white" : Theme.textDim
        font.pixelSize: 16
    }

    // A tick for an option that is on.
    indicator: Text {
        x: 9
        anchors.verticalCenter: parent.verticalCenter
        visible: item.checkable && item.checked
        text: "✓"
        color: item.highlighted ? "white" : Theme.accent
        font.pixelSize: Theme.fontSize
        font.bold: true
    }

    background: Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: Theme.radiusSmall
        border.color: item.highlighted ? Theme.outline : "transparent"
        gradient: Gradient {
            GradientStop { position: 0.0; color: item.highlighted ? Theme.accentTop : "transparent" }
            GradientStop { position: 1.0; color: item.highlighted ? Theme.accentBottom : "transparent" }
        }
    }
}
