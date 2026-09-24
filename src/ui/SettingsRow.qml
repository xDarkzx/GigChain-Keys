import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Label ....... control" on a Settings page.
RowLayout {
    id: row

    property string label
    default property alias content: holder.data

    Layout.leftMargin: 20
    Layout.rightMargin: 20
    spacing: 12

    Label {
        Layout.preferredWidth: 140
        text: row.label
        color: Theme.textDim
    }
    RowLayout {
        id: holder
        Layout.fillWidth: true
    }
}
