import QtQuick
import QtQuick.Controls

// OpenStage's menu look (dark, rounded, blue highlight, like Logic's menus).
// Stays inside the window: long menus scroll instead of running off screen.
Menu {
    id: menu

    margins: 8
    overlap: 2
    padding: 4

    background: Rectangle {
        implicitWidth: 220
        color: Theme.menuBackground
        border.color: Theme.stripBorder
        radius: 6
    }

    delegate: StageMenuItem {}
}
