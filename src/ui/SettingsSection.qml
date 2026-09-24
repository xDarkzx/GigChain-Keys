import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A section heading on a Settings page.
Label {
    property string title

    text: title
    Layout.fillWidth: true
    Layout.topMargin: 12
    leftPadding: 20
    font.pixelSize: Theme.fontSize
    font.bold: true
    color: Theme.text
}
