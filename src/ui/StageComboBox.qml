pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Our drop-down: dark field, list in the menu look, stays in the window.
ComboBox {
    id: box

    implicitHeight: 30
    implicitWidth: 260

    // Raised like a button, with the drop arrow in its own section.
    background: Rectangle {
        radius: Theme.radiusSmall
        border.color: box.activeFocus || box.popup.visible ? Theme.accent : Theme.outline
        gradient: Gradient {
            GradientStop { position: 0.0; color: box.hovered ? Theme.buttonHoverTop : Theme.buttonTop }
            GradientStop { position: 1.0; color: box.hovered ? Theme.buttonHoverBottom : Theme.buttonBottom }
        }
        Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelLight }
        // The groove before the arrow.
        StageDivider {
            vertical: true
            x: parent.width - 26
            y: 5
            height: parent.height - 10
        }
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

    indicator: Image {
        x: box.width - 13 - width / 2
        anchors.verticalCenter: parent.verticalCenter
        source: "icons/chevron-down.svg"
        sourceSize: Qt.size(14, 14)
        opacity: 0.8
    }

    delegate: ItemDelegate {
        id: row
        required property int index
        required property var modelData
        width: ListView.view.width
        implicitHeight: 26
        topPadding: 0
        bottomPadding: 0
        leftPadding: 6
        rightPadding: 6
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
            radius: Theme.radiusSmall
            border.color: row.highlighted ? Theme.outline : "transparent"
            gradient: Gradient {
                GradientStop { position: 0.0; color: row.highlighted ? Theme.accentTop : "transparent" }
                GradientStop { position: 1.0; color: row.highlighted ? Theme.accentBottom : "transparent" }
            }
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
            border.color: Theme.outline
            radius: Theme.radiusCard
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.lighter(Theme.menuBackground, 1.08) }
                GradientStop { position: 1.0; color: Theme.menuBackground }
            }
        }
    }
}
