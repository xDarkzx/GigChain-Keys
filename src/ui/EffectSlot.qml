import QtQuick
import QtQuick.Controls

// A Logic-style plug-in slot. Loaded: blue slot with the plugin's name, a
// power dot on the left (bypass) and a menu arrow on the right; grey when
// bypassed. Empty: a dark recess that opens the picker when clicked.
Rectangle {
    id: slot

    property string text
    property bool loaded: text !== ""
    property bool bypassed: false
    property bool showPower: true // instrument slots have no bypass
    property color loadedColor: Theme.slotLoaded

    signal clicked()        // body click (loaded) or click (empty)
    signal menuRequested()  // arrow click or right-click
    signal powerToggled()

    // The instrument slot is green, effects blue; each lit from above.
    readonly property color baseColor: bypassed ? Theme.slotBypassed : (hover.hovered ? Qt.lighter(loadedColor, 1.12) : loadedColor)

    implicitHeight: 20
    radius: Theme.radiusSmall
    border.color: loaded ? Theme.outline : Theme.slotEmptyBorder
    readonly property color emptyColor: hover.hovered ? Theme.slotHover : Theme.slotEmpty
    gradient: Gradient {
        GradientStop { position: 0.0; color: slot.loaded ? Qt.lighter(slot.baseColor, 1.25) : slot.emptyColor }
        GradientStop { position: 1.0; color: slot.loaded ? Qt.darker(slot.baseColor, 1.15) : slot.emptyColor }
    }
    // A loaded slot is raised (lit top edge); an empty one is recessed
    // (shaded top edge).
    Rectangle {
        x: 1
        y: 1
        width: parent.width - 2
        height: 1
        color: slot.loaded ? "#30ffffff" : Theme.bevelDark
    }

    HoverHandler { id: hover }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton && slot.loaded) slot.menuRequested()
            else slot.clicked()
        }
    }

    // power (bypass) dot
    Rectangle {
        visible: slot.loaded && slot.showPower
        width: 8
        height: 8
        radius: 4
        x: 5
        anchors.verticalCenter: parent.verticalCenter
        color: slot.bypassed ? Theme.textDim : "#bfe3ff"
        border.color: Qt.darker(color, 1.6)
        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            onClicked: slot.powerToggled()
        }
    }

    Text {
        anchors.fill: parent
        anchors.leftMargin: slot.loaded && slot.showPower ? 17 : 6
        anchors.rightMargin: slot.loaded ? 14 : 4
        text: slot.loaded ? slot.text : "+"
        color: !slot.loaded ? Theme.textDim : (slot.bypassed ? Theme.textDim : "white")
        font.pixelSize: 10
        font.bold: slot.loaded
        elide: Text.ElideRight
        horizontalAlignment: slot.loaded ? Text.AlignLeft : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    // menu arrow
    Text {
        visible: slot.loaded && hover.hovered
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: "▾"
        color: "white"
        font.pixelSize: 10
        MouseArea {
            anchors.fill: parent
            anchors.margins: -4
            onClicked: slot.menuRequested()
        }
    }
}
