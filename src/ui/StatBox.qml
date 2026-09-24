import QtQuick
import QtQuick.Controls

// A readout in its own fixed box: "CPU  12%". The box is as wide as the
// widest value it can ever show (`widest`), digits are fixed-width and the
// value is right-aligned, so a changing number never moves anything else.
Item {
    id: box

    property string label
    property string value
    property string widest: value // the longest value this box will show
    property color valueColor: Theme.text

    implicitWidth: labelText.implicitWidth + 6 + widestMetrics.advanceWidth
    implicitHeight: Math.max(labelText.implicitHeight, valueText.implicitHeight)

    TextMetrics {
        id: widestMetrics
        font: valueText.font
        text: box.widest
    }

    Text {
        id: labelText
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: box.label
        color: Theme.textDim
        font.pixelSize: Theme.fontSize
    }
    Text {
        id: valueText
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: widestMetrics.advanceWidth
        horizontalAlignment: Text.AlignRight
        text: box.value
        color: box.valueColor
        font.pixelSize: Theme.fontSize
        font.features: { "tnum": 1 } // tabular figures: every digit the same width
    }
}
