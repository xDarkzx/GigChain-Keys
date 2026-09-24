import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// "Label   value" in a card's details. Rows with no value are left out.
RowLayout {
    id: row

    property string label
    property string value
    property bool link: false // opens in the browser when clicked
    property bool wrap: false

    visible: value !== ""
    Layout.fillWidth: true
    spacing: 6

    Label {
        Layout.preferredWidth: 66
        Layout.alignment: Qt.AlignTop
        text: row.label
        color: Theme.textDim
        font.pixelSize: 10
    }
    Label {
        Layout.fillWidth: true
        text: row.value
        color: row.link ? Theme.accentBlue : Theme.text
        font.pixelSize: 10
        font.underline: row.link && linkArea.containsMouse
        elide: row.wrap ? Text.ElideNone : Text.ElideRight
        wrapMode: row.wrap ? Text.WrapAnywhere : Text.NoWrap
        MouseArea {
            id: linkArea
            anchors.fill: parent
            enabled: row.link
            hoverEnabled: true
            cursorShape: row.link ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: Qt.openUrlExternally(row.value)
        }
    }
}
