pragma ComponentBehavior: Bound

import QtQuick

// The instruments' windows of their own (InstrumentWindows): one
// InstrumentWindow per open window, made and closed as the list changes.
Instantiator {
    id: list

    required property InstrumentWindows windows
    required property DocumentController doc
    required property EngineStatus engineStatus
    property Window owner: null

    model: list.windows.windows
    delegate: InstrumentWindow {
        id: ownWindow
        required property var modelData
        service: ownWindow.modelData
        windows: list.windows
        doc: list.doc
        engineStatus: list.engineStatus
        owner: list.owner
    }
}
