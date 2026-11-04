pragma ComponentBehavior: Bound

import QtQuick

// Two settings on one square, as Tonic's transform pad: across (`xValue`)
// and up (`yValue`), each 0-1. Drag the dot (or click anywhere); the cross
// marks where it is. Used by Pads Pro (Tone across, Shimmer up) and Sustain
// Pads (Filter across, Shimmer up).
Rectangle {
    id: pad

    property real xValue: 0.5
    property real yValue: 0
    property string xLabel: qsTr("Tone")
    property string yLabel: qsTr("Shimmer")
    property color colour: "#4cc3ff"
    property color well: "#0b0d11"
    property color edge: "#232834"
    property color muted: "#8590a2"
    signal moved(real x, real y)

    radius: 12
    color: well
    border.color: dragArea.pressed ? colour : edge
    border.width: dragArea.pressed ? 2 : 1

    // The grid: quarters, faint.
    Repeater {
        model: 3
        delegate: Rectangle {
            required property int index
            x: pad.width * (index + 1) / 4
            y: 8
            width: 1
            height: pad.height - 16
            color: "#1a1e26"
        }
    }
    Repeater {
        model: 3
        delegate: Rectangle {
            required property int index
            x: 8
            y: pad.height * (index + 1) / 4
            width: pad.width - 16
            height: 1
            color: "#1a1e26"
        }
    }
    // The cross through the dot, glowing as it is moved.
    Rectangle {
        x: dot.x + dot.width / 2 - 1
        y: 6
        width: 2
        height: pad.height - 12
        color: pad.colour
        opacity: dragArea.pressed ? 0.8 : 0.45
    }
    Rectangle {
        x: 6
        y: dot.y + dot.height / 2 - 1
        width: pad.width - 12
        height: 2
        color: pad.colour
        opacity: dragArea.pressed ? 0.8 : 0.45
    }
    Rectangle {
        id: dot
        objectName: "padXYDot"
        readonly property real room: 10
        width: 22
        height: 22
        radius: 11
        x: room + pad.xValue * (pad.width - 2 * room - width)
        y: room + (1 - pad.yValue) * (pad.height - 2 * room - height)
        color: pad.colour
        border.color: "white"
        border.width: 2
        Behavior on x { enabled: !dragArea.pressed; NumberAnimation { duration: 120 } }
        Behavior on y { enabled: !dragArea.pressed; NumberAnimation { duration: 120 } }
    }
    // The axes' names, along the edges.
    Text {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 6 }
        text: "−  " + pad.xLabel + "  +"
        color: pad.muted
        font.family: Theme.fontFamily
        font.pixelSize: 10
        font.weight: Font.DemiBold
    }
    Text {
        anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: -width / 2 + 12 }
        rotation: -90
        text: "−  " + pad.yLabel + "  +"
        color: pad.muted
        font.family: Theme.fontFamily
        font.pixelSize: 10
        font.weight: Font.DemiBold
    }
    MouseArea {
        id: dragArea
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        function moveTo(mx, my) {
            const room = dot.room + dot.width / 2
            const x = Math.max(0, Math.min(1, (mx - room) / (pad.width - 2 * room)))
            const y = Math.max(0, Math.min(1, 1 - (my - room) / (pad.height - 2 * room)))
            pad.moved(x, y)
        }
        onPressed: (mouse) => moveTo(mouse.x, mouse.y)
        onPositionChanged: (mouse) => { if (pressed) moveTo(mouse.x, mouse.y) }
    }
}
