import QtQuick

// An engraved groove between areas: a dark line with a light line beside
// it, so the panels read as separate pieces of the console.
Item {
    id: divider

    property bool vertical: false

    implicitWidth: vertical ? 2 : 10
    implicitHeight: vertical ? 10 : 2

    Rectangle {
        x: 0
        y: 0
        width: divider.vertical ? 1 : divider.width
        height: divider.vertical ? divider.height : 1
        color: Theme.dividerDark
    }
    Rectangle {
        x: divider.vertical ? 1 : 0
        y: divider.vertical ? 0 : 1
        width: divider.vertical ? 1 : divider.width
        height: divider.vertical ? divider.height : 1
        color: Theme.dividerLight
    }
}
