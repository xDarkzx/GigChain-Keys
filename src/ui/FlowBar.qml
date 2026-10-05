pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The song's flow: the order it is played in (Verse 1 → Pre-Chorus → Chorus
// → Verse 2 → Chorus ×3 → Outro), which chord follow keeps to: it only ever
// moves forward along it. The chart's own order until changed here: + adds
// a section of the chart, a part's menu plays it once more or less, moves it
// or takes it out; "Chart order" goes back.
Rectangle {
    id: bar
    objectName: "flowBar"

    required property DocumentController doc

    // The flow's parts, the same part played in a row as one (×n).
    readonly property var parts: bar.doc.songFlow
    readonly property var groups: {
        const list = []
        for (let i = 0; i < bar.parts.length; ++i) {
            const p = bar.parts[i]
            const last = list.length > 0 ? list[list.length - 1] : null
            if (last !== null && last.name === p.name && last.occurrence === p.occurrence) ++last.count
            else list.push({ name: p.name, occurrence: p.occurrence, label: p.label, count: 1, first: i })
        }
        return list
    }

    // The flow as a list of parts again, with `change` made to the groups.
    function save(groups) {
        const flow = []
        for (const g of groups) for (let n = 0; n < g.count; ++n) flow.push({ name: g.name, occurrence: g.occurrence })
        bar.doc.setSongFlow(flow)
    }
    function copyGroups() { return bar.groups.map(g => ({ name: g.name, occurrence: g.occurrence, label: g.label, count: g.count })) }

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
                    model: bar.groups
                    delegate: Row {
                        id: group
                        required property var modelData
                        required property int index
                        spacing: 4
                        Text {
                            visible: group.index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: "→"
                            color: Theme.textDim
                        }
                        StageButton {
                            objectName: "flowPart"
                            text: group.modelData.label + (group.modelData.count > 1 ? "  ×" + group.modelData.count : "")
                            onClicked: partMenu.popup(0, height)
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
                            g.push({ name: modelData.name, occurrence: modelData.occurrence, label: modelData.label, count: 1 })
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
