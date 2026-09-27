import QtQuick
import QtQuick.Controls

// The grip between two resizable areas: a raised bar with three engraved
// dots in the middle, lit blue while dragged.
Rectangle {
    id: handle

    property bool vertical: false // a vertical bar (areas side by side)

    implicitWidth: 9
    implicitHeight: 9
    border.color: Theme.outline
    gradient: Gradient {
        orientation: handle.vertical ? Gradient.Horizontal : Gradient.Vertical
        GradientStop { position: 0.0; color: handle.SplitHandle.pressed ? Theme.accentTop : (handle.SplitHandle.hovered ? Theme.buttonHoverTop : Theme.barTop) }
        GradientStop { position: 1.0; color: handle.SplitHandle.pressed ? Theme.accentBottom : (handle.SplitHandle.hovered ? Theme.buttonHoverBottom : Theme.barBottom) }
    }

    Row {
        visible: !handle.vertical
        anchors.centerIn: parent
        spacing: 4
        Repeater {
            model: 3
            Rectangle { width: 4; height: 4; radius: 2; color: Theme.dividerDark; border.color: "#30ffffff" }
        }
    }
    Column {
        visible: handle.vertical
        anchors.centerIn: parent
        spacing: 4
        Repeater {
            model: 3
            Rectangle { width: 4; height: 4; radius: 2; color: Theme.dividerDark; border.color: "#30ffffff" }
        }
    }
}
