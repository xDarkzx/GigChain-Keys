import QtQuick

// An LCD set into the panel, as MainStage's displays: dark blue-black glass
// in a bezel, a shadow along its top inside, and a faint reflection across
// its upper half. Its content (lit text in Theme.lcdText) goes inside.
Rectangle {
    id: lcd

    radius: Theme.radiusSmall
    border.color: "#000000"
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.lcdTop }
        GradientStop { position: 1.0; color: Theme.lcdBottom }
    }
    // The shadow inside the bezel's top edge.
    Rectangle { x: 1; y: 1; width: parent.width - 2; height: 2; radius: 1; color: "#80000000" }
    // The glass's reflection.
    Rectangle {
        x: 2
        y: 2
        width: parent.width - 4
        height: parent.height * 0.45
        radius: lcd.radius
        color: Theme.lcdGlare
    }
    // The bezel's lit lower edge, where the panel catches the light.
    Rectangle { x: 2; y: parent.height; width: parent.width - 4; height: 1; color: Theme.bevelLight }
}
