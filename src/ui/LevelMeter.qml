import QtQuick

// A vertical level meter; `level` is linear 0..1.
Rectangle {
    id: meter
    property real level: 0

    color: Theme.background
    radius: 2
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: parent.height * Math.max(0, Math.min(1, meter.level))
        radius: 2
        color: meter.level > 0.9 ? Theme.meterHigh : Theme.meterLow
    }
}
