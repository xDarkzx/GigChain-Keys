import QtQuick

// A raised surface: a top-to-bottom gradient, a dark outline and a lit top
// edge. `bar` for toolbars and headers (stronger), otherwise a panel.
Rectangle {
    id: surface

    property bool bar: false
    property bool outlined: true
    property color topColor: bar ? Theme.barTop : Theme.panelTop
    property color bottomColor: bar ? Theme.barBottom : Theme.panelBottom

    border.color: outlined ? Theme.outline : "transparent"
    gradient: Gradient {
        GradientStop { position: 0.0; color: surface.topColor }
        GradientStop { position: 1.0; color: surface.bottomColor }
    }

    // The lit top edge.
    Rectangle {
        x: surface.outlined ? 1 : 0
        y: surface.outlined ? 1 : 0
        width: parent.width - (surface.outlined ? 2 : 0)
        height: 1
        color: Theme.bevelLight
    }
}
