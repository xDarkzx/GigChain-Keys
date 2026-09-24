import QtQuick

// A small dark numeric readout box (volume / peak), as in Logic's strips.
Rectangle {
    id: readout
    property string text
    property bool alarm: false

    implicitHeight: 16
    radius: 2
    color: alarm ? Theme.meterHigh : Theme.readoutBackground
    Text {
        anchors.centerIn: parent
        text: readout.text
        color: readout.alarm ? "white" : Theme.readoutText
        font.pixelSize: 9
        font.family: "Consolas"
    }
}
