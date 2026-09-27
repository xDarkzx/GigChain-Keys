import QtQuick
import QtQuick.Controls

// Our menu look (like Logic's menus): a raised dark panel with an outline
// and a soft shadow, rows lit blue under the mouse. Stays inside the window:
// long menus scroll instead of running off screen.
Menu {
    id: menu

    margins: 8
    overlap: 2
    padding: 4

    background: Item {
        implicitWidth: 220
        // Soft shadow.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            anchors.topMargin: -1
            radius: Theme.radiusCard + 3
            color: Theme.shadow
            opacity: 0.5
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusCard
            border.color: Theme.outline
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.lighter(Theme.menuBackground, 1.08) }
                GradientStop { position: 1.0; color: Theme.menuBackground }
            }
            Rectangle { x: 2; y: 1; width: parent.width - 4; height: 1; color: Theme.bevelLight }
        }
    }

    delegate: StageMenuItem {}
}
