import QtQuick

// A place a DragSource can drop on: `keys` says which drags it takes, and
// payloadDropped hands over the dragged thing's payload.
DropArea {
    signal payloadDropped(var payload)

    // Called by DragSource on release over this area.
    function acceptDrop(payload) { payloadDropped(payload) }
}
