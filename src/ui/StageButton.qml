import QtQuick
import QtQuick.Controls

// The app's button: raised and lit from above (a gradient with a light top
// edge), a dark outline, and clear states: hover lifts it, pressing sinks
// it, "on" (checked, highlighted) and the accent tone light it blue, the
// danger tone red. Text, an icon (a white SVG from icons/), or both.
Button {
    id: control

    property string iconSource: ""
    property string tone: "normal" // "normal" | "accent" | "danger"
    // Size of the icon; the button grows with it.
    property int iconSize: Theme.iconSize
    property string tip: ""

    readonly property bool lit: control.checked || control.highlighted || control.tone === "accent"
    readonly property color topColor: !control.enabled ? Theme.buttonBottom
                                      : control.down ? (control.tone === "danger" ? Qt.darker(Theme.dangerBottom, 1.2)
                                                                                  : control.lit ? Qt.darker(Theme.accentBottom, 1.15)
                                                                                                : Theme.buttonDownTop)
                                      : control.tone === "danger" ? (control.hovered ? Qt.lighter(Theme.dangerTop, 1.08) : Theme.dangerTop)
                                      : control.lit ? (control.hovered ? Qt.lighter(Theme.accentTop, 1.08) : Theme.accentTop)
                                      : control.hovered ? Theme.buttonHoverTop : Theme.buttonTop
    readonly property color bottomColor: !control.enabled ? Theme.buttonBottom
                                         : control.down ? (control.tone === "danger" ? Theme.dangerBottom
                                                                                     : control.lit ? Theme.accentBottom : Theme.buttonDownBottom)
                                         : control.tone === "danger" ? Theme.dangerBottom
                                         : control.lit ? Theme.accentBottom
                                         : control.hovered ? Theme.buttonHoverBottom : Theme.buttonBottom

    implicitHeight: Math.max(Theme.controlHeight, iconSize + 10)
    implicitWidth: Math.max(implicitHeight, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: text !== "" ? 12 : 6
    rightPadding: leftPadding
    topPadding: 0
    bottomPadding: 0
    focusPolicy: Qt.NoFocus
    font.pixelSize: Theme.fontSize
    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.45

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            Image {
                visible: control.iconSource !== ""
                anchors.verticalCenter: parent.verticalCenter
                source: control.iconSource
                sourceSize: Qt.size(control.iconSize, control.iconSize)
                width: control.iconSize
                height: control.iconSize
            }
            Text {
                visible: control.text !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.lit || control.tone === "danger" ? Theme.textOnAccent : Theme.text
                elide: Text.ElideRight
            }
        }
    }

    background: Rectangle {
        implicitWidth: control.implicitHeight
        implicitHeight: control.implicitHeight
        radius: Theme.radiusSmall
        border.color: Theme.outline
        gradient: Gradient {
            GradientStop { position: 0.0; color: control.topColor }
            GradientStop { position: 1.0; color: control.bottomColor }
        }
        // The lit top edge (gone while pressed: the button sinks).
        Rectangle {
            x: 1
            y: 1
            width: parent.width - 2
            height: 1
            radius: 1
            visible: !control.down && control.enabled
            color: control.lit || control.tone === "danger" ? "#40ffffff" : Theme.bevelLight
        }
    }

    ToolTip.visible: hovered && tip !== ""
    ToolTip.text: tip
    ToolTip.delay: 500
}
