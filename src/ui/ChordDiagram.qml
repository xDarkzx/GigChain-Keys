pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// How to play a chord, for a player who forgot: a keyboard with a dot on
// each key to press (black on white keys, white on black keys, as Chordify
// shows them), ringed blue for the left hand and gold for the right; the
// notes by name; its inversions to look through, and the one to keep for
// this chord in this song (Practice plays it; the diagram opens on it).
// DocumentController.chordDiagram() says which keys.
StageDialog {
    id: diagram
    objectName: "chordDiagram"

    property DocumentController doc: null
    property string chord: ""
    property int inversion: -1 // the one shown (-1: the song's choice, else root position)
    // (Read again when the song's choices change: the binding names them.)
    readonly property var info: diagram.doc !== null && diagram.chord !== "" && diagram.doc.currentChordInversions !== undefined
                                ? diagram.doc.chordDiagram(diagram.chord, diagram.inversion) : ({})
    readonly property bool understood: diagram.info.understood === true
    readonly property var left: diagram.understood ? diagram.info.left : []
    readonly property var right: diagram.understood ? diagram.info.right : []

    function show(name) {
        diagram.chord = name
        diagram.inversion = -1
        diagram.open()
    }

    title: diagram.understood ? qsTr("How to play %1").arg(diagram.chord) : diagram.chord
    width: Math.max(560, keyboard.width + 48)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // The keys shown: whole octaves round the notes.
    function isBlack(note) { return [1, 3, 6, 8, 10].indexOf(((note % 12) + 12) % 12) >= 0 }
    readonly property int lowest: {
        const all = diagram.left.concat(diagram.right)
        const low = all.length > 0 ? Math.min(...all) : 48
        return low - (low % 12)
    }
    readonly property int highest: {
        const all = diagram.left.concat(diagram.right)
        const high = all.length > 0 ? Math.max(...all) : 71
        return high + (11 - high % 12)
    }
    readonly property var whites: {
        const list = []
        for (let n = diagram.lowest; n <= diagram.highest; ++n) if (!diagram.isBlack(n)) list.push(n)
        return list
    }
    readonly property real keyWidth: Math.min(26, 640 / Math.max(1, diagram.whites.length))
    readonly property real keyHeight: diagram.keyWidth * 4.6
    function keyX(note) {
        if (!diagram.isBlack(note)) return diagram.whites.indexOf(note) * diagram.keyWidth
        return (diagram.whites.indexOf(note - 1) + 1) * diagram.keyWidth - diagram.keyWidth * 0.31
    }
    function hand(note) { return diagram.right.indexOf(note) >= 0 ? "right" : diagram.left.indexOf(note) >= 0 ? "left" : "" }

    ColumnLayout {
        width: diagram.width
        spacing: Theme.spacing

        Label {
            visible: !diagram.understood
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            wrapMode: Text.Wrap
            text: qsTr("\"%1\" is not a chord the app can read, so it cannot show its keys. Fix its name in the chart.").arg(diagram.chord)
            color: Theme.textDim
        }

        // The keyboard, a dot on each key to press.
        Item {
            id: keyboard
            objectName: "chordDiagramKeys"
            visible: diagram.understood
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingLarge
            Layout.preferredWidth: diagram.whites.length * diagram.keyWidth
            Layout.preferredHeight: diagram.keyHeight
            implicitWidth: diagram.whites.length * diagram.keyWidth
            Repeater {
                model: diagram.whites
                delegate: Rectangle {
                    id: white
                    required property int modelData
                    x: diagram.keyX(white.modelData)
                    width: diagram.keyWidth - 1
                    height: diagram.keyHeight
                    radius: 3
                    color: "#f4f4f4"
                    border.color: "#555"
                    Text {
                        visible: white.modelData % 12 === 0
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        bottomPadding: 2
                        text: "C" + (Math.floor(white.modelData / 12) - 1)
                        color: "#888"
                        font.pixelSize: 9
                    }
                }
            }
            Repeater {
                model: {
                    const list = []
                    for (let n = diagram.lowest; n <= diagram.highest; ++n) if (diagram.isBlack(n)) list.push(n)
                    return list
                }
                delegate: Rectangle {
                    id: black
                    required property int modelData
                    x: diagram.keyX(black.modelData)
                    width: diagram.keyWidth * 0.62
                    height: diagram.keyHeight * 0.62
                    radius: 2
                    color: "#15171b"
                    border.color: "#000"
                    z: 1
                }
            }
            // The dots: on the keys to press.
            Repeater {
                model: diagram.left.concat(diagram.right)
                delegate: Rectangle {
                    id: dot
                    objectName: "chordDiagramDot"
                    required property int modelData
                    readonly property bool onBlack: diagram.isBlack(dot.modelData)
                    readonly property real keyW: dot.onBlack ? diagram.keyWidth * 0.62 : diagram.keyWidth - 1
                    z: 2
                    width: Math.min(dot.keyW - 4, 16)
                    height: width
                    radius: width / 2
                    x: diagram.keyX(dot.modelData) + (dot.keyW - width) / 2
                    y: (dot.onBlack ? diagram.keyHeight * 0.62 : diagram.keyHeight) - height - (dot.onBlack ? 6 : 16)
                    color: dot.onBlack ? "white" : "#111"
                    border.width: 2.5
                    border.color: diagram.hand(dot.modelData) === "left" ? Theme.accent : Theme.chord
                }
            }
        }

        // The notes by name.
        RowLayout {
            visible: diagram.understood
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacingLarge * 2
            Row {
                spacing: 6
                Rectangle { width: 12; height: 12; radius: 6; color: "#111"; border.width: 2.5; border.color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                Label {
                    objectName: "chordDiagramLeft"
                    text: qsTr("Left hand: %1").arg(diagram.info.leftNames !== undefined ? diagram.info.leftNames : "")
                    color: Theme.text
                    font.pixelSize: Theme.fontSize + 2
                }
            }
            Row {
                spacing: 6
                Rectangle { width: 12; height: 12; radius: 6; color: "#111"; border.width: 2.5; border.color: Theme.chord; anchors.verticalCenter: parent.verticalCenter }
                Label {
                    objectName: "chordDiagramRight"
                    text: qsTr("Right hand: %1").arg(diagram.info.rightNames !== undefined ? diagram.info.rightNames : "")
                    color: Theme.text
                    font.pixelSize: Theme.fontSize + 2
                    font.bold: true
                }
            }
        }

        // Its inversions: tap one to see it.
        Row {
            visible: diagram.understood
            Layout.alignment: Qt.AlignHCenter
            spacing: -1
            Repeater {
                model: diagram.understood ? diagram.info.inversions : []
                delegate: StageButton {
                    required property string modelData
                    required property int index
                    objectName: "chordInversion" + index
                    text: modelData + (index === diagram.info.chosen ? "  ✓" : "")
                    checked: index === diagram.info.inversion
                    onClicked: diagram.inversion = index
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingLarge
            spacing: Theme.spacing
            StageButton {
                objectName: "chordDiagramKeep"
                visible: diagram.understood
                enabled: diagram.info.chosen !== diagram.info.inversion
                text: diagram.info.chosen === diagram.info.inversion ? qsTr("Used for %1 in this song").arg(diagram.chord)
                                                                    : qsTr("Use this one for %1 in this song").arg(diagram.chord)
                tip: qsTr("Practice plays it, and this diagram opens on it")
                onClicked: {
                    const shown = diagram.info.inversion
                    if (diagram.doc.setChordInversion(diagram.chord, shown)) diagram.inversion = shown
                }
            }
            StageButton {
                objectName: "chordDiagramForget"
                visible: diagram.understood && diagram.info.chosen >= 0
                text: qsTr("Forget the choice")
                onClicked: {
                    diagram.doc.setChordInversion(diagram.chord, -1)
                    diagram.inversion = 0
                }
            }
            Item { Layout.fillWidth: true }
            StageButton {
                text: qsTr("Close")
                tone: "accent"
                onClicked: diagram.close()
            }
        }
    }
}
