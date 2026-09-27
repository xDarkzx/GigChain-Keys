import QtQuick

// A window without the Windows frame still resizes from its edges and
// corners: thin grab areas along the border hand the drag to the system.
// Off while maximised or full screen.
Item {
    id: frame

    required property Window window
    property int grip: 5

    anchors.fill: parent
    z: 1000
    visible: window.visibility === Window.Windowed

    // Inline components cannot see this file's ids: the window comes in.
    component Edge: MouseArea {
        id: edge
        required property Window target
        property int sides: 0
        hoverEnabled: true
        onPressed: edge.target.startSystemResize(edge.sides)
    }

    Edge { target: frame.window; sides: Qt.LeftEdge; cursorShape: Qt.SizeHorCursor
        x: 0; y: frame.grip; width: frame.grip; height: frame.height - 2 * frame.grip }
    Edge { target: frame.window; sides: Qt.RightEdge; cursorShape: Qt.SizeHorCursor
        x: frame.width - frame.grip; y: frame.grip; width: frame.grip; height: frame.height - 2 * frame.grip }
    Edge { target: frame.window; sides: Qt.TopEdge; cursorShape: Qt.SizeVerCursor
        x: frame.grip; y: 0; width: frame.width - 2 * frame.grip; height: frame.grip }
    Edge { target: frame.window; sides: Qt.BottomEdge; cursorShape: Qt.SizeVerCursor
        x: frame.grip; y: frame.height - frame.grip; width: frame.width - 2 * frame.grip; height: frame.grip }
    Edge { target: frame.window; sides: Qt.TopEdge | Qt.LeftEdge; cursorShape: Qt.SizeFDiagCursor
        x: 0; y: 0; width: frame.grip * 2; height: frame.grip * 2 }
    Edge { target: frame.window; sides: Qt.TopEdge | Qt.RightEdge; cursorShape: Qt.SizeBDiagCursor
        x: frame.width - frame.grip * 2; y: 0; width: frame.grip * 2; height: frame.grip * 2 }
    Edge { target: frame.window; sides: Qt.BottomEdge | Qt.LeftEdge; cursorShape: Qt.SizeBDiagCursor
        x: 0; y: frame.height - frame.grip * 2; width: frame.grip * 2; height: frame.grip * 2 }
    Edge { target: frame.window; sides: Qt.BottomEdge | Qt.RightEdge; cursorShape: Qt.SizeFDiagCursor
        x: frame.width - frame.grip * 2; y: frame.height - frame.grip * 2; width: frame.grip * 2; height: frame.grip * 2 }
}
