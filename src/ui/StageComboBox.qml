import QtQuick
import QtQuick.Controls

// OpenStage's drop-down: dark field, list in the menu look, stays in the window.
ComboBox {
    id: box

    implicitHeight: 30
    implicitWidth: 260

    background: Rectangle {
        radius: Theme.radius
        color: box.hovered ? Theme.slotHover : Theme.panelRaised
        border.color: box.activeFocus ? Theme.accentBlue : Theme.border
    }

    contentItem: Text {
        leftPadding: 10
        rightPadding: 28
        text: box.displayText
        color: box.enabled ? Theme.text : Theme.textDim
        font.pixelSize: Theme.fontSize
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    indicator: Text {
        x: box.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: Theme.textDim
        font.pixelSize: 12
    }

    delegate: ItemDelegate {
        id: row
        required property int index
        required property var modelData
        width: ListView.view.width
        implicitHeight: 26
        highlighted: box.highlightedIndex === index
        contentItem: Text {
            leftPadding: 4
            text: box.textRole ? row.modelData[box.textRole] : row.modelData
            color: row.highlighted ? "white" : Theme.text
            font.pixelSize: Theme.fontSize
            font.bold: box.currentIndex === row.index
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 4
            color: row.highlighted ? Theme.accentBlue : "transparent"
        }
    }

    popup: Popup {
        y: box.height + 2
        width: box.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        margins: 8
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle {
            color: Theme.menuBackground
            border.color: Theme.stripBorder
            radius: 6
        }
    }
}
