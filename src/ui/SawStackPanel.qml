pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Saw Stack Pro's window, drawn by the app (it is the app's own instrument):
// the type of stack, as keys with a light, and its five knobs. Everything
// else (its eight layers and the effects behind them) is the type's.
// A knob turned here can be learned to a keyboard knob with MIDI Learn above.
Item {
    id: panel

    required property EditorService service

    // [{id, name, choices}], the type first (the one with choices).
    property var parameters: []
    readonly property var typeParameter: parameters.find((p) => p.choices.length > 0)
    readonly property var knobs: parameters.filter((p) => p.choices.length === 0)
    property real typeValue: 0
    readonly property int typeIndex: typeParameter ? Math.round(typeValue * (typeParameter.choices.length - 1)) : -1
    property var values: ({})

    function reload() {
        parameters = service.builtinParameters()
        refresh()
    }
    // The values as the instrument has them now (a learned keyboard knob, a
    // new type, a loaded song move them).
    function refresh() {
        if (typeParameter) typeValue = service.builtinValue(typeParameter.id)
        const next = {}
        for (const k of knobs) next[k.id] = service.builtinValue(k.id)
        values = next
    }
    function chooseType(index) {
        service.setBuiltinValue(typeParameter.id, index / Math.max(1, typeParameter.choices.length - 1))
        refresh() // its knobs start where the type starts them
    }

    Component.onCompleted: reload()
    Connections {
        target: panel.service
        function onTargetChanged() { panel.reload() }
    }
    Timer {
        interval: 150
        repeat: true
        running: panel.visible
        onTriggered: panel.refresh()
    }

    // How a knob's value reads, as players think of it.
    function describe(name, v) {
        if (v < 0) return ""
        const seconds = 0.0005 * Math.pow(16000, v)
        switch (name) {
        case "Cutoff": {
            const octaves = (v - 0.5) * 10
            return Math.abs(octaves) < 0.05 ? qsTr("As designed") : (octaves > 0 ? "+" : "") + octaves.toFixed(1) + qsTr(" oct")
        }
        case "Attack":
        case "Release":
            return seconds < 1 ? Math.round(seconds * 1000) + " ms" : seconds.toFixed(2) + " s"
        case "Spread":
            return Math.abs(v - 0.5) < 0.005 ? qsTr("As designed") : Math.round(v * 200) + "%"
        default:
            return Math.round(v * 100) + "%"
        }
    }

    // From the top, scrolling when the area is shorter than the panel (the
    // chart and the mixer share the screen with it).
    Flickable {
        objectName: "sawStackScroll"
        anchors.fill: parent
        contentHeight: card.height + 2 * Theme.spacing
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    Rectangle {
        id: card
        objectName: "sawStackPanel"
        x: (panel.width - width) / 2
        y: Theme.spacing
        width: Math.min(panel.width - 2 * Theme.spacing, 820)
        height: column.implicitHeight + 28
        radius: Theme.radiusCard
        border.color: Theme.outline
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.panelTop }
            GradientStop { position: 1.0; color: Theme.panelBottom }
        }
        Rectangle { x: 2; y: 1; width: parent.width - 4; height: 1; color: Theme.bevelLight }

        ColumnLayout {
            id: column
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
            spacing: 12

            // The name, and the type playing on a little display.
            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Text {
                    text: qsTr("SAW STACK PRO")
                    color: Theme.text
                    font.pixelSize: Theme.headerFontSize
                    font.bold: true
                    font.letterSpacing: 2
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: Theme.radius
                    border.color: Theme.outline
                    gradient: Gradient {
                        GradientStop { position: 0; color: Theme.lcdTop }
                        GradientStop { position: 1; color: Theme.lcdBottom }
                    }
                    Text {
                        objectName: "sawStackTypeShown"
                        anchors.centerIn: parent
                        text: panel.typeParameter && panel.typeIndex >= 0 ? panel.typeParameter.choices[panel.typeIndex] : ""
                        color: Theme.lcdText
                        font.pixelSize: Theme.fontSize + 3
                        font.bold: true
                    }
                }
            }

            // The knobs first: what is turned while playing stays in sight
            // when the area is short.
            StripSection {
                Layout.fillWidth: true
                label: qsTr("SOUND")
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 6
                    Layout.topMargin: 16
                    spacing: 6
                    Repeater {
                        model: panel.knobs
                        delegate: ColumnLayout {
                            id: slot
                            required property var modelData
                            readonly property real current: panel.values[modelData.id] !== undefined ? panel.values[modelData.id] : -1
                            Layout.fillWidth: true
                            spacing: 4
                            Dial {
                                id: dial
                                objectName: "sawStackKnob_" + slot.modelData.name
                                Layout.alignment: Qt.AlignHCenter
                                implicitWidth: 64
                                implicitHeight: 64
                                from: 0
                                to: 1
                                focusPolicy: Qt.NoFocus
                                Binding on value {
                                    value: Math.max(0, slot.current)
                                    when: !knobMouse.pressed
                                    restoreMode: Binding.RestoreNone
                                }
                                function moveTo(v) {
                                    const clamped = Math.max(0, Math.min(1, v))
                                    if (clamped === value) return
                                    value = clamped
                                    panel.service.setBuiltinValue(slot.modelData.id, clamped)
                                }
                                background: KnobFace {
                                    implicitWidth: 64
                                    implicitHeight: 64
                                    position: dial.position
                                    // Cutoff and Spread are "as designed" in the middle: their arc starts at the top.
                                    arcFrom: slot.modelData.name === "Cutoff" || slot.modelData.name === "Spread" ? 0.5 : 0
                                    ledColor: Theme.ledBlue
                                }
                                handle: Item {}
                                MouseArea {
                                    id: knobMouse
                                    anchors.fill: parent
                                    preventStealing: true
                                    cursorShape: Qt.SizeVerCursor
                                    property point start
                                    property real startValue: 0
                                    onPressed: (mouse) => {
                                        start = Qt.point(mouse.x, mouse.y)
                                        startValue = dial.value
                                    }
                                    onPositionChanged: (mouse) => {
                                        if (!pressed) return
                                        // 150 px across the knob's travel; up/right = more.
                                        dial.moveTo(startValue + ((mouse.x - start.x) - (mouse.y - start.y)) / 150)
                                    }
                                    onWheel: (wheel) => dial.moveTo(dial.value + (wheel.angleDelta.y > 0 ? 0.02 : -0.02))
                                }
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: slot.modelData.name.toUpperCase()
                                color: Theme.engraved
                                font.pixelSize: Theme.tinyFontSize
                                font.bold: true
                                font.letterSpacing: 1.2
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: panel.describe(slot.modelData.name, knobMouse.pressed ? dial.value : slot.current)
                                color: Theme.text
                                font.pixelSize: Theme.smallFontSize
                            }
                        }
                    }
                }
            }

            StripSection {
                Layout.fillWidth: true
                label: qsTr("TYPE")
                GridLayout {
                    Layout.fillWidth: true
                    Layout.margins: 6
                    Layout.topMargin: 14
                    columns: 5
                    rowSpacing: 6
                    columnSpacing: 6
                    Repeater {
                        model: panel.typeParameter ? panel.typeParameter.choices : []
                        delegate: Rectangle {
                            id: key
                            required property string modelData
                            required property int index
                            readonly property bool lit: index === panel.typeIndex
                            objectName: "sawStackType_" + index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            radius: Theme.radius
                            border.color: Theme.outline
                            gradient: Gradient {
                                GradientStop { position: 0; color: keyMouse.pressed ? Theme.buttonBottom : Theme.buttonTop }
                                GradientStop { position: 1; color: Theme.buttonBottom }
                            }
                            Rectangle { x: 1; y: 1; width: parent.width - 2; height: 1; color: Theme.bevelLight }
                            Rectangle {
                                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 5 }
                                height: 3
                                radius: 1.5
                                color: key.lit ? Theme.ledBlue : Theme.ledOff
                            }
                            Text {
                                anchors { left: parent.left; right: parent.right; leftMargin: 6; rightMargin: 6 }
                                y: (parent.height - height) / 2 + 3
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                text: key.modelData
                                font.pixelSize: Theme.smallFontSize
                                font.bold: true
                                color: key.lit ? Theme.text : Theme.textDim
                            }
                            MouseArea {
                                id: keyMouse
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: panel.chooseType(key.index)
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Pick a type, shape it with the knobs. Add reverb or delay in the channel's effects. To play a knob from your keyboard: MIDI Learn, turn it here, then turn yours.")
                color: Theme.textDim
                font.pixelSize: Theme.smallFontSize
            }
        }
    }
    }
}
