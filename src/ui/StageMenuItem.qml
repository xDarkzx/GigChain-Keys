import QtQuick
import QtQuick.Controls

// One row of a StageMenu (also used for submenu entries).
MenuItem {
    id: item

    implicitHeight: 26
    implicitWidth: 220

    contentItem: Text {
        leftPadding: 8
        rightPadding: 18
        text: item.text
        color: item.enabled ? (item.highlighted ? "white" : Theme.text) : Theme.textDim
        font.pixelSize: Theme.fontSize
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    arrow: Text {
        x: item.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        visible: item.subMenu !== null
        text: "›"
        color: item.highlighted ? "white" : Theme.textDim
        font.pixelSize: 16
    }

    indicator: Item {}

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
