import QtQuick
import QtQuick.Layouts

// One section of a channel strip, as Logic's: a recessed well with a dark
// edge, a shadow along its top inside and a light line under it, and a small
// engraved label ("INSTRUMENT", "AUDIO FX"). Its content goes in a column.
Rectangle {
    id: section

    property string label: ""
    default property alias content: column.data
    property alias spacing: column.spacing

    implicitWidth: column.implicitWidth + 8
    implicitHeight: column.implicitHeight + 6 + (label !== "" ? title.height + 2 : 0)
    radius: Theme.radius
    border.color: Theme.wellBorder
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.wellTop }
        GradientStop { position: 1.0; color: Theme.wellBottom }
    }
    // The shadow inside its top edge (it is set into the strip).
    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 2; radius: 1; color: Theme.wellShadow }
    // The light catching the strip's surface under it.
    Rectangle { x: 2; y: parent.height; width: parent.width - 4; height: 1; color: Theme.bevelLight }

    Text {
        id: title
        visible: section.label !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        y: 3
        text: section.label
        color: Theme.engraved
        font.pixelSize: 8
        font.bold: true
        font.letterSpacing: 1.2
    }

    ColumnLayout {
        id: column
        x: 4
        y: section.label !== "" ? title.y + title.height + 2 : 3
        width: parent.width - 8
        height: section.height - y - 3 // (a section that fills its room lets its content fill too: the fader)
        spacing: 3
    }
}
