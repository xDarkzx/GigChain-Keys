pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The song's flow: the order it is played in (Verse 1 → Pre-Chorus → Chorus
// → Verse 2 → Chorus ×3 → Outro), which chord follow keeps to: it only ever
// moves forward along it. The chart's own order until changed here: a part
// clicked shows it in the chart (partChosen), dragged along the bar moves it,
// right-clicked has its menu (once more or less, earlier, later, out); an
// arrow right-clicked puts a section in there; + adds one at the end;
// "Chart order" goes back.
Rectangle {
    id: bar
    objectName: "flowBar"

    required property DocumentController doc
    // A part clicked: its section (in the chart's sections), to be shown.
    signal partChosen(int section)

    // A part being dragged along the bar: which, and where it would go (0..groups).
    property int dragFrom: -1
    property int dropAt: -1
    function insertionAt(x) {
        for (let i = 0; i < groupRepeater.count; ++i) {
            const item = groupRepeater.itemAt(i)
            if (item !== null && x < item.x + item.width / 2) return i
        }
        return groupRepeater.count
    }
    function finishDrag() {
        const from = bar.dragFrom
        const to = bar.dropAt
        bar.dragFrom = -1
        bar.dropAt = -1
        if (from < 0 || to < 0 || to === from || to === from + 1) return
        const g = bar.copyGroups()
        const moved = g.splice(from, 1)[0]
        g.splice(to > from ? to - 1 : to, 0, moved)
        bar.save(g)
    }
    // A section put into the flow at `at` (0..groups), from the menu of the arrow there.
    property int insertAt: -1

    // The flow's parts, the same part played in a row as one (×n).
    readonly property var parts: bar.doc.songFlow
    readonly property var groups: {
        const list = []
        for (let i = 0; i < bar.parts.length; ++i) {
            const p = bar.parts[i]
            const last = list.length > 0 ? list[list.length - 1] : null
            if (last !== null && last.name === p.name && last.occurrence === p.occurrence) ++last.count
            else list.push({ name: p.name, occurrence: p.occurrence, label: p.label, section: p.section, count: 1, first: i })
        }
        return list
    }

    // The flow as a list of parts again, with `change` made to the groups.
    // (Parts naming a section the chart no longer has are left out.)
    function save(groups) {
        const flow = []
        for (const g of groups) {
            if (g.section < 0) continue
            for (let n = 0; n < g.count; ++n) flow.push({ name: g.name, occurrence: g.occurrence })
        }
        bar.doc.setSongFlow(flow)
    }
    function copyGroups() {
        return bar.groups.map(g => ({ name: g.name, occurrence: g.occurrence, label: g.label, section: g.section, count: g.count }))
    }

    implicitHeight: 34
    radius: Theme.radiusCard
    color: Theme.readoutBackground
    border.color: Theme.outline

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 6
        spacing: Theme.spacing
        Label {
            text: qsTr("Flow")
            color: Theme.textDim
            font.bold: true
            ToolTip.visible: flowHover.hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("The order the song is played in. Chord follow only moves forward along it.")
            HoverHandler { id: flowHover }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: partsRow.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Row {
                id: partsRow
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Repeater {
                    id: groupRepeater
                    model: bar.groups
                    delegate: Row {
                        id: group
                        required property var modelData
                        required property int index
                        spacing: 4
                        // The arrow between two parts: right-click (or click) to put a section in there.
                        Item {
                            objectName: "flowArrow"
                            visible: group.index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: arrowText.implicitWidth + 8
                            height: 26
                            Rectangle {
                                anchors.fill: parent
                                radius: 4
                                color: arrowHover.hovered ? Theme.buttonHoverTop : "transparent"
                            }
                            Text {
                                id: arrowText
                                anchors.centerIn: parent
                                text: "→"
                                color: arrowHover.hovered ? Theme.text : Theme.textDim
                            }
                            HoverHandler { id: arrowHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onTapped: {
                                    bar.insertAt = group.index
                                    insertMenu.popup()
                                }
                            }
                            ToolTip.visible: arrowHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: qsTr("Put a section in here")
                        }
                        StageButton {
                            id: partButton
                            objectName: "flowPart"
                            text: group.modelData.label + (group.modelData.count > 1 ? "  ×" + group.modelData.count : "")
                                  + (group.modelData.section < 0 ? "  ?" : "")
                            opacity: bar.dragFrom === group.index ? 0.4 : group.modelData.section < 0 ? 0.5 : 1
                            tip: group.modelData.section < 0
                                 ? qsTr("The chart has no section \"%1\" any more: it is skipped. Take it out, or change the flow and it goes.")
                                       .arg(group.modelData.name)
                                 : qsTr("Click: show it in the chart. Drag: move it. Right-click: more")
                            onClicked: if (group.modelData.section >= 0) bar.partChosen(group.modelData.section)
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: partMenu.popup(partButton, 0, partButton.height)
                            }
                            // Dragged along the bar: moved where it is let go.
                            DragHandler {
                                target: null
                                yAxis.enabled: false
                                onActiveChanged: {
                                    if (active) bar.dragFrom = group.index
                                    else bar.finishDrag()
                                }
                                onCentroidChanged: {
                                    if (!active) return
                                    const p = partsRow.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                                    bar.dropAt = bar.insertionAt(p.x)
                                }
                            }
                            StageMenu {
                                id: partMenu
                                StageMenuItem {
                                    objectName: "flowOnceMore"
                                    text: qsTr("Play it once more (×%1)").arg(group.modelData.count + 1)
                                    onTriggered: { const g = bar.copyGroups(); ++g[group.index].count; bar.save(g) }
                                }
                                StageMenuItem {
                                    text: qsTr("Play it once less")
                                    enabled: group.modelData.count > 1
                                    onTriggered: { const g = bar.copyGroups(); --g[group.index].count; bar.save(g) }
                                }
                                StageMenuItem {
                                    text: qsTr("Move earlier")
                                    enabled: group.index > 0
                                    onTriggered: {
                                        const g = bar.copyGroups()
                                        const moved = g.splice(group.index, 1)[0]
                                        g.splice(group.index - 1, 0, moved)
                                        bar.save(g)
                                    }
                                }
                                StageMenuItem {
                                    text: qsTr("Move later")
                                    enabled: group.index < bar.groups.length - 1
                                    onTriggered: {
                                        const g = bar.copyGroups()
                                        const moved = g.splice(group.index, 1)[0]
                                        g.splice(group.index + 1, 0, moved)
                                        bar.save(g)
                                    }
                                }
                                StageMenuItem {
                                    objectName: "flowRemove"
                                    text: qsTr("Take it out")
                                    onTriggered: { const g = bar.copyGroups(); g.splice(group.index, 1); bar.save(g) }
                                }
                            }
                        }
                    }
                }
            }
            // Where a dragged part would go.
            Rectangle {
                objectName: "flowDropMark"
                visible: bar.dragFrom >= 0 && bar.dropAt >= 0 && bar.dropAt !== bar.dragFrom && bar.dropAt !== bar.dragFrom + 1
                readonly property Item before: bar.dropAt >= 0 && bar.dropAt < groupRepeater.count ? groupRepeater.itemAt(bar.dropAt) : null
                readonly property Item last: groupRepeater.count > 0 ? groupRepeater.itemAt(groupRepeater.count - 1) : null
                x: before !== null ? partsRow.x + before.x - 3 : last !== null ? partsRow.x + last.x + last.width + 1 : 0
                y: partsRow.y
                width: 3
                height: partsRow.height
                radius: 1.5
                color: Theme.accent
            }
        }
        // A section put in where an arrow was clicked.
        StageMenu {
            id: insertMenu
            objectName: "flowInsertMenu"
            Instantiator {
                model: bar.doc.chartParts
                delegate: StageMenuItem {
                    required property var modelData
                    text: qsTr("Put in %1").arg(modelData.label)
                    onTriggered: {
                        const g = bar.copyGroups()
                        g.splice(Math.max(0, Math.min(bar.insertAt, g.length)), 0,
                                 { name: modelData.name, occurrence: modelData.occurrence, label: modelData.label,
                                   section: modelData.section, count: 1 })
                        bar.save(g)
                    }
                }
                onObjectAdded: (index, object) => insertMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => insertMenu.removeItem(object)
            }
        }
        // A section of the chart added at the end.
        StageButton {
            objectName: "flowAdd"
            iconSource: "icons/plus.svg"
            tip: qsTr("Add a section to the flow (a chorus played again, after the bridge...)")
            onClicked: addMenu.popup(0, height)
            StageMenu {
                id: addMenu
                objectName: "flowAddMenu"
                Instantiator {
                    model: bar.doc.chartParts
                    delegate: StageMenuItem {
                        required property var modelData
                        text: modelData.label
                        onTriggered: {
                            const g = bar.copyGroups()
                            g.push({ name: modelData.name, occurrence: modelData.occurrence, label: modelData.label,
                                     section: modelData.section, count: 1 })
                            bar.save(g)
                        }
                    }
                    onObjectAdded: (index, object) => addMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => addMenu.removeItem(object)
                }
            }
        }
        StageButton {
            objectName: "flowChartOrder"
            visible: bar.doc.songFlowSet
            text: qsTr("Chart order")
            tip: qsTr("Back to the sections in the order the chart writes them")
            onClicked: bar.doc.setSongFlow([])
        }
    }
}
