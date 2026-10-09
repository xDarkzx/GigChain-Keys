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
    // ---- keys, as the computer names them
    readonly property bool mac: Qt.platform.os === "osx" || Qt.platform.os === "macos"
    // A shortcut written the Windows way ("Ctrl+Shift+Z", "F1") as this
    // computer shows it: on a Mac "⇧⌘Z" (Qt's Ctrl is the Command key there)
    // and "⌘?" for help. Elsewhere as written.
    // (`forMac`: as a Mac shows it, or not, whatever this computer is.)
    function keys(sequence, forMac) {
        if (!(forMac === undefined ? mac : forMac)) return sequence
        if (sequence === "F1") return "⌘?"
        const parts = sequence.split("+")
        const key = parts.pop()
        const symbols = { "Ctrl": "⌘", "Shift": "⇧", "Alt": "⌥", "Meta": "⌃" }
        const order = ["Meta", "Alt", "Shift", "Ctrl"] // Apple's order: ⌃⌥⇧⌘
        return order.filter(m => parts.indexOf(m) >= 0).map(m => symbols[m]).join("") + key
    }

    // ---- surfaces: neutral graphite, as MainStage's (not blue-tinted)
    readonly property color background: "#141414"
    readonly property color panel: "#1d1d1e"
    readonly property color panelRaised: "#29292b"
    readonly property color border: "#3a3a3d"
    // Toolbars and headers: a raised strip, lighter at the top.
    readonly property color barTop: "#3a3a3d"
    readonly property color barBottom: "#232325"
    // Panels (side panel, mixer): a gentle fall from top to bottom.
    readonly property color panelTop: "#232325"
    readonly property color panelBottom: "#19191a"

    // ---- hardware: metal caps, LEDs and the displays
    readonly property color metalLight: "#a9adb3"
    readonly property color metalMid: "#6c7076"
    readonly property color metalDark: "#3c3f44"
    readonly property color metalShine: "#30ffffff"
    readonly property color knobSkirt: "#141516"
    readonly property color knobWell: "#0b0b0c"
    readonly property color knobTick: "#5a5d63"
    readonly property color knobPointer: "#ffffff"
    readonly property color ledBlue: "#4aa3ff"
    readonly property color ledGreen: "#43d36b"
    readonly property color ledAmber: "#ffb43a"
    readonly property color ledRed: "#ff4d4f"
    readonly property color ledOff: "#2a2b2e"
    // A strip's section: a well set into the strip.
    readonly property color wellTop: "#151516"
    readonly property color wellBottom: "#1c1c1e"
    readonly property color wellBorder: "#070708"
    readonly property color wellShadow: "#55000000"
    readonly property color engraved: "#8a8d93" // small labels cut into a panel
    // An LCD: dark glass, lit text, a reflection across its top.
    readonly property color lcdTop: "#1c2733"
    readonly property color lcdBottom: "#0c1218"
    readonly property color lcdText: "#d8ecff"
    readonly property color lcdTextDim: "#7f9bb5"
    readonly property color lcdAccent: "#6fc3ff"
    readonly property color lcdGlare: "#14ffffff"

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

    // ---- buttons: a gradient per state (raised grey keys, as MainStage's)
    readonly property color buttonTop: "#4a4a4e"
    readonly property color buttonBottom: "#2e2e31"
    readonly property color buttonHoverTop: "#56565b"
    readonly property color buttonHoverBottom: "#353539"
    readonly property color buttonDownTop: "#1e1e20"
    readonly property color buttonDownBottom: "#2c2c2f"
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
    readonly property color mixerBackground: "#111112"
    // Strips: lighter graphite standing on the console, as Logic's, so their dark sections stand out.
    readonly property color stripTop: "#47474c"
    readonly property color stripBottom: "#323236"
    readonly property color stripBackground: "#3a3a3e"
    readonly property color stripSelected: "#33415a"
    readonly property color stripBorder: "#0b0b0c"
    readonly property color slotBackground: "#38383c"
    readonly property color slotHover: "#444449"
    readonly property color readoutBackground: "#0b0c0e"
    readonly property color readoutText: "#9fe0a8"
    readonly property color knobFace: "#3a3d42"
    readonly property color knobRing: "#1b1c1f"
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
    readonly property int touchTarget: 48   // Perform's buttons: a fingertip on a touch screen
    readonly property int iconSize: 16
    readonly property int sidePanelWidth: 270
    readonly property int stripWidth: 100
    readonly property int mixerHeight: 480 // a whole strip, name plate and all (stripHeight and the mixer's margins)
    readonly property int looperHeight: 88 // the loop station strip over the mixer (its name, its buttons, its gap)
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
