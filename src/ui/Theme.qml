pragma Singleton
import QtQuick

// Every colour, size and font in one place: restyling OpenStage means editing
// this file. Dark stage look for now.
QtObject {
    readonly property color background: "#14161a"
    readonly property color panel: "#1d2025"
    readonly property color panelRaised: "#262a31"
    readonly property color border: "#353a44"
    readonly property color text: "#e6e8ec"
    readonly property color textDim: "#98a0ad"
    readonly property color accent: "#e0a526"
    readonly property color accentText: "#14161a"
    readonly property color danger: "#e5484d"
    readonly property color selection: "#2f3b52"
    readonly property color meterLow: "#3fb950"
    readonly property color meterHigh: "#e5484d"
    readonly property color performBackground: "#000000"
    readonly property color keyWhite: "#f2f2f2"
    readonly property color keyBlack: "#1a1a1a"

    readonly property int spacing: 8
    readonly property int radius: 4
    readonly property int sidePanelWidth: 270
    readonly property int stripWidth: 92
    readonly property int fontSize: 13
    readonly property int smallFontSize: 11
    readonly property int headerFontSize: 20
    readonly property int performTitleSize: 84
    readonly property int performSubtitleSize: 30
    readonly property string fontFamily: "Segoe UI"
}
