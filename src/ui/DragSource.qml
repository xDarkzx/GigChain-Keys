import QtQuick
import QtQuick.Controls

// Makes its parent draggable. While dragging, a label follows the pointer on
// the window overlay; on release the PayloadDropArea underneath (if any)
// gets the payload. Plain clicks pass through as the
// clicked / doubleClicked / rightClicked signals.
MouseArea {
    id: source

    required property var payload
    required property string label
    required property string dragKey
    property bool dragEnabled: true

    signal rightClicked()

    anchors.fill: parent
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    preventStealing: true
    drag.target: dragEnabled ? ghost : null
    drag.threshold: 8

    onPressed: (mouse) => {
        if (mouse.button === Qt.RightButton) return
        const p = source.mapToItem(ghost.parent, mouse.x, mouse.y)
        ghost.x = p.x - 10
        ghost.y = p.y - 10
    }
    onReleased: (mouse) => {
        if (mouse.button === Qt.RightButton) {
            source.rightClicked()
            return
        }
        const target = ghost.Drag.target as PayloadDropArea
        if (target !== null)
            target.acceptDrop(source.payload)
    }

    Rectangle {
        id: ghost
        parent: Overlay.overlay
        visible: source.drag.active
        width: ghostLabel.implicitWidth + 20
        height: 28
        radius: Theme.radius
        color: Theme.accent
        opacity: 0.9
        Drag.active: source.drag.active
        Drag.keys: [source.dragKey]
        Drag.hotSpot.x: 10
        Drag.hotSpot.y: 10
        Label {
            id: ghostLabel
            anchors.centerIn: parent
            text: source.label
            color: Theme.accentText
        }
    }
}
