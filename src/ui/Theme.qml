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
    readonly property color accentBlue: "#4a8fe7"
    readonly property color mixerBackground: "#17191d"
    readonly property color stripBackground: "#24272d"
    readonly property color stripSelected: "#2c3340"
    readonly property color stripBorder: "#34383f"
    readonly property color slotBackground: "#31353d"
    readonly property color slotHover: "#3b4049"
    readonly property color readoutBackground: "#0f1113"
    readonly property color readoutText: "#9fe0a8"
    readonly property color knobFace: "#3a3f47"
    readonly property color knobRing: "#1b1d21"
    readonly property color faderGroove: "#0c0d0f"
    readonly property color faderCapTop: "#c9ccd1"
    readonly property color faderCapMid: "#8d9198"
    readonly property color faderCapBottom: "#5d6167"
    readonly property color meterBackground: "#0c0d0f"
    readonly property color meterMid: "#e0c526"
    readonly property color menuBackground: "#2a2d33"
    readonly property color slotEmpty: "#191b1f"
    readonly property color slotEmptyBorder: "#2c3036"
    readonly property color slotLoaded: "#3d73c4"
    readonly property color slotInstrument: "#3f8f6a"
    readonly property color slotBypassed: "#3a3d44"
    readonly property color muteColor: "#4ab3e7"
    readonly property color soloColor: "#e0c526"

    readonly property int spacing: 8
    readonly property int radius: 4
    readonly property int sidePanelWidth: 270
    readonly property int stripWidth: 88
    readonly property int mixerHeight: 360
    readonly property int fontSize: 13
    readonly property int smallFontSize: 11
    readonly property int headerFontSize: 20
    readonly property int performTitleSize: 84
    readonly property int performSubtitleSize: 30
    readonly property string fontFamily: "Segoe UI"
}
