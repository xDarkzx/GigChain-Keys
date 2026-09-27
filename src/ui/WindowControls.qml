import QtQuick

// The window's own minimise, zoom and close buttons: macOS-style coloured
// lights, placed where Windows puts them (close last, at the far right).
// Their symbols show while the mouse is over the group.
Row {
    id: controls

    spacing: 8
    HoverHandler { id: groupHover }

    component Light: Rectangle {
        id: light
        property color lit
        property string symbol
        property bool showSymbol
        signal pressed()
        width: 13
        height: 13
        radius: 6.5
        color: area.pressed ? Qt.darker(lit, 1.25) : lit
        border.color: Qt.darker(lit, 1.45)
        // A gloss on the top half, as on the real thing.
        Rectangle {
            x: 2
            y: 1
            width: light.width - 4
            height: light.height / 2 - 1
            radius: height / 2
            color: "#30ffffff"
        }
        Text {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -0.5
            visible: light.showSymbol
            text: light.symbol
            color: "#5a1a14"
            opacity: 0.8
            font.pixelSize: 10
            font.bold: true
        }
        MouseArea {
            id: area
            anchors.fill: parent
            anchors.margins: -2
            onClicked: light.pressed()
        }
    }

    Light {
        objectName: "windowMinimise"
        lit: "#febc2e"
        symbol: "−"
        showSymbol: groupHover.hovered
        onPressed: controls.Window.window.showMinimized()
    }
    Light {
        objectName: "windowZoom"
        lit: "#28c840"
        symbol: controls.Window.window !== null && controls.Window.window.visibility === Window.Maximized ? "‹›" : "+"
        showSymbol: groupHover.hovered
        onPressed: {
            const w = controls.Window.window
            if (w.visibility === Window.Maximized) w.showNormal()
            else w.showMaximized()
        }
    }
    Light {
        objectName: "windowClose"
        lit: "#ff5f57"
        symbol: "×"
        showSymbol: groupHover.hovered
        onPressed: controls.Window.window.close()
    }
}
