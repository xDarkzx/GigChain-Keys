pragma Singleton
import QtQuick

// Every colour, size and font in one place: restyling the app means editing
// this file. The look is a dark stage console with depth: surfaces and
// buttons are lit from above (a gradient, a light top edge), every control
// has a dark outline, and panels are separated by engraved grooves.
//
// Colours have one job each: blue marks what is selected or active; red is
// Panic and errors only; amber is warnings only; green is meters and "on";
// gold is the rating stars.
QtObject {
    // ---- surfaces
    readonly property color background: "#121418"
    readonly property color panel: "#1b1e23"
    readonly property color panelRaised: "#252930"
    readonly property color border: "#353a44"
    // Toolbars and headers: a raised strip, lighter at the top.
    readonly property color barTop: "#2e333b"
    readonly property color barBottom: "#1f2228"
    // Panels (side panel, mixer): a gentle fall from top to bottom.
    readonly property color panelTop: "#20242a"
    readonly property color panelBottom: "#181a1e"

    // ---- depth
    readonly property color outline: "#08090b"      // the dark edge round every control and panel
    readonly property color bevelLight: "#1affffff" // the lit top edge of raised things (10% white)
    readonly property color bevelDark: "#66000000"  // the shaded bottom edge
    readonly property color dividerDark: "#07080a"  // an engraved groove: dark line...
    readonly property color dividerLight: "#12ffffff" // ...with a light line under it
    readonly property color shadow: "#80000000"
    readonly property color overlay: "#99000000"    // behind a dialog

    // ---- text
    readonly property color text: "#e6e8ec"
    readonly property color textDim: "#98a0ad"
    readonly property color textOnAccent: "#ffffff"

    // ---- buttons: a gradient per state
    readonly property color buttonTop: "#3b4049"
    readonly property color buttonBottom: "#2a2e35"
    readonly property color buttonHoverTop: "#474d58"
    readonly property color buttonHoverBottom: "#31363e"
    readonly property color buttonDownTop: "#1f2227"
    readonly property color buttonDownBottom: "#2b2f36"
    readonly property color accentTop: "#5ea0f2"
    readonly property color accentBottom: "#3570c4"
    readonly property color dangerTop: "#f0585d"
    readonly property color dangerBottom: "#b92f34"

    // ---- colours with one job
    readonly property color accent: "#4a8fe7"       // selected, active, focus
    readonly property color accentText: "#ffffff"
    readonly property color accentBlue: accent      // older name for the same blue
    readonly property color danger: "#e5484d"
    readonly property color info: "#4a8fe7"
    readonly property color warning: "#e0a526"
    readonly property color star: "#e8b73a"
    readonly property color chord: "#f0bf45" // chord names over the lyrics
    readonly property color selection: "#2f3b52"
    readonly property color meterLow: "#3fb950"
    readonly property color meterMid: "#e0c526"
    readonly property color meterHigh: "#e5484d"
    readonly property color performBackground: "#000000"

    // ---- mixer
    readonly property color mixerBackground: "#15171b"
    readonly property color stripTop: "#2a2e35"
    readonly property color stripBottom: "#1e2126"
    readonly property color stripBackground: "#24272d"
    readonly property color stripSelected: "#2c3544"
    readonly property color stripBorder: "#0b0c0e"
    readonly property color slotBackground: "#31353d"
    readonly property color slotHover: "#3b4049"
    readonly property color readoutBackground: "#0b0c0e"
    readonly property color readoutText: "#9fe0a8"
    readonly property color knobFace: "#3a3f47"
    readonly property color knobRing: "#1b1d21"
    readonly property color faderGroove: "#0a0b0d"
    readonly property color faderCapTop: "#d4d7dc"
    readonly property color faderCapMid: "#8d9198"
    readonly property color faderCapBottom: "#5d6167"
    readonly property color meterBackground: "#0a0b0d"
    readonly property color menuBackground: "#262a31"
    readonly property color slotEmpty: "#141619"
    readonly property color slotEmptyBorder: "#2a2e35"
    readonly property color slotLoaded: "#3d73c4"
    readonly property color slotLoadedTop: "#4d86d6"
    readonly property color slotInstrument: "#3f8f6a"
    readonly property color slotInstrumentTop: "#4fa47c"
    readonly property color slotBypassed: "#3a3d44"
    readonly property color muteColor: "#4ab3e7"
    readonly property color soloColor: "#e0c526"

    // ---- sizes: spacing on a 4/8/16 grid
    readonly property int spacingSmall: 4
    readonly property int spacing: 8
    readonly property int spacingLarge: 16
    readonly property int radiusSmall: 3  // small controls
    readonly property int radius: 4
    readonly property int radiusCard: 6   // cards and panels
    readonly property int radiusDialog: 10
    readonly property int controlHeight: 28 // a button you can hit quickly on stage
    readonly property int iconSize: 16
    readonly property int sidePanelWidth: 270
    readonly property int stripWidth: 88
    readonly property int mixerHeight: 360
    // A channel strip's fixed height (REAPER/Audacity size): never stretched to the window.
    readonly property int stripHeight: 470

    // ---- type scale
    readonly property int tinyFontSize: 10  // labels inside strips
    readonly property int smallFontSize: 11
    readonly property int fontSize: 13      // body text
    readonly property int titleFontSize: 16 // dialog and section titles
    readonly property int headerFontSize: 20
    readonly property int performTitleSize: 84
    readonly property int performSubtitleSize: 30
    readonly property string fontFamily: "Segoe UI"
}
