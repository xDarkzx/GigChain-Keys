pragma ComponentBehavior: Bound

import QtQuick

// One layer of the app's own instruments: its light, its name and job, its
// sound drawn, a note about it (a stack's voices), and its level as a bar to drag.
Item {
    id: row

    required property var owner // the instrument's window (BuiltinPanel)
    required property int onId
    required property int levelId
    required property string title
    required property string role
    required property color colour
    // How its sound is drawn: 0 saw, 1 square, 2 sine, 3 noise, 4 a struck string (rings, then fades).
    property int shape: 0
    property string note
    property string lightName
    property string levelName
    readonly property bool on: owner.value(onId) >= 0.5
    readonly property real level: Math.max(0, owner.value(levelId))
    height: 30

    Rectangle {
        anchors.fill: parent
        radius: 6
        color: rowArea.containsMouse ? "#1c2029" : "transparent"
    }
    // The light: on or off.
    Rectangle {
        id: light
        objectName: row.lightName
        anchors { left: parent.left; leftMargin: 6; verticalCenter: parent.verticalCenter }
        width: 26; height: 14; radius: 7
        color: row.on ? row.colour : "#2a2f39"
        Rectangle {
            width: 10; height: 10; radius: 5
            anchors.verticalCenter: parent.verticalCenter
            x: row.on ? parent.width - width - 2 : 2
            color: row.on ? "#ffffff" : "#6b7484"
            Behavior on x { NumberAnimation { duration: 120 } }
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            cursorShape: Qt.PointingHandCursor
            onClicked: row.owner.set(row.onId, row.on ? 0 : 1)
        }
    }
    Column {
        id: names
        anchors { left: light.right; leftMargin: 10; verticalCenter: parent.verticalCenter }
        width: 150
        Text {
            text: row.title
            color: row.on ? row.owner.text : row.owner.muted
            font.family: Theme.fontFamily
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
        Text {
            text: row.role
            color: row.owner.muted
            font.family: Theme.fontFamily
            font.pixelSize: 8
        }
    }
    // Its sound, drawn.
    Canvas {
        id: waveIcon
        anchors { left: names.right; leftMargin: 4; verticalCenter: parent.verticalCenter }
        width: 34; height: 16
        property int shape: row.shape
        property bool lit: row.on
        onShapeChanged: requestPaint()
        onLitChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = lit ? row.colour : "#4a5262"
            ctx.lineWidth = 1.5
            ctx.lineJoin = "round"
            ctx.beginPath()
            const h = height - 2
            let seed = 11
            for (let x = 0; x <= width; ++x) {
                const t = (x / width * 2) % 1
                let y = 0
                if (shape === 0) y = 1 - 2 * t
                else if (shape === 1) y = t < 0.5 ? 1 : -1
                else if (shape === 2) y = Math.sin(t * 2 * Math.PI)
                else if (shape === 3) { seed = (seed * 16807) % 2147483647; y = (seed / 2147483647) * 2 - 1 }
                else y = Math.sin(x / width * 10 * Math.PI) * Math.exp(-3 * x / width)
                const py = 1 + (1 - y) / 2 * h
                if (x === 0) ctx.moveTo(x, py); else ctx.lineTo(x, py)
            }
            ctx.stroke()
        }
    }
    Text {
        id: noteText
        anchors { left: waveIcon.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
        width: 34
        text: row.note
        color: row.owner.muted
        font.family: Theme.fontFamily
        font.pixelSize: 10
    }
    // The level: drag along the bar.
    Item {
        id: bar
        objectName: row.levelName
        anchors { left: noteText.right; leftMargin: 6; right: dbText.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
        height: 18
        Rectangle {
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
            height: 6; radius: 3
            color: row.owner.track
        }
        Rectangle {
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: Math.max(6, parent.width * row.level)
            height: 6; radius: 3
            color: row.on ? row.colour : "#4a5262"
        }
        Rectangle {
            x: parent.width * row.level - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 12; height: 12; radius: 6
            color: "#ffffff"
            visible: levelArea.containsMouse || levelArea.pressed
        }
        MouseArea {
            id: levelArea
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            function setFrom(x) { row.owner.set(row.levelId, x / width) }
            onPressed: (mouse) => setFrom(mouse.x)
            onPositionChanged: (mouse) => { if (pressed) setFrom(mouse.x) }
            onWheel: (wheel) => row.owner.set(row.levelId, row.level + (wheel.angleDelta.y > 0 ? 0.01 : -0.01))
        }
    }
    Text {
        id: dbText
        anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
        width: 52
        horizontalAlignment: Text.AlignRight
        text: row.owner.describe("db", row.level)
        color: row.on ? row.owner.text : row.owner.muted
        font.family: Theme.fontFamily
        font.pixelSize: 10
    }
    MouseArea { id: rowArea; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
}
