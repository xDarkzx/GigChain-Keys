#!/bin/sh
# Starts the app the Linux packages install (tools/package-linux.sh puts this
# file in the .deb as the command and the menu entry's, and in the AppImage
# as AppRun, with @APP@ replaced by where the app is).
#
# On PipeWire (most current distributions), JACK's devices are there only for
# an app started with pw-jack: PipeWire's libjack is not on the loader path
# (#70). So the app is started through pw-jack when it is installed and
# PipeWire is running. Where PipeWire's libjack is already the system's,
# pw-jack changes nothing; where JACK itself runs (no PipeWire), it is not used.
#   GIGCHAIN_NO_PW_JACK=1   start it without pw-jack, whatever is installed
#
# The file and folder dialogs are the desktop's own (#93): Qt's portal theme,
# which the packages carry, hands them to the XDG desktop portal (GNOME's,
# KDE's... chooser, in the player's language). Without a portal running, Qt's
# own dialog, as before. A theme the player chose (QT_QPA_PLATFORMTHEME) stays.
#
# Through pw-jack, JACK runs at PipeWire's graph size, which stays at its
# default (often 1024 samples) unless the app asks for less: so the buffer
# size saved in Settings > Audio is asked for with PIPEWIRE_LATENCY (#83),
# unless PIPEWIRE_LATENCY is already set.
APP=@APP@
SETTINGS_FILE="${XDG_CONFIG_HOME:-$HOME/.config}/@ORGANIZATION@/@NAME@.conf"

# "<frames>/<rate>" from the saved settings, or nothing (no file, no size).
saved_latency() {
    [ -r "$SETTINGS_FILE" ] || return 1
    awk -F= '
        { sub(/\r$/, "") }
        /^\[/ { section = $0; next }
        section == "[audio]" && $1 == "bufferFrames" { frames = $2 }
        section == "[audio]" && $1 == "sampleRate" { rate = $2 }
        END {
            if (frames !~ /^[0-9]+$/ || frames + 0 < 16 || frames + 0 > 8192) exit 1
            if (rate !~ /^[0-9]+$/ || rate + 0 < 8000) rate = 48000
            print frames "/" rate
        }' "$SETTINGS_FILE"
}

pipewire_running() {
    remote="${PIPEWIRE_REMOTE:-pipewire-0}"
    case "$remote" in
        /*) socket="$remote" ;;
        *) socket="${PIPEWIRE_RUNTIME_DIR:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}}/$remote" ;;
    esac
    [ -S "$socket" ]
}

if [ -z "${QT_QPA_PLATFORMTHEME:-}" ]; then
    QT_QPA_PLATFORMTHEME=xdgdesktopportal
    export QT_QPA_PLATFORMTHEME
fi

if [ -z "${GIGCHAIN_NO_PW_JACK:-}" ] && command -v pw-jack >/dev/null 2>&1 && pipewire_running; then
    if [ -z "${PIPEWIRE_LATENCY:-}" ] && latency="$(saved_latency)"; then
        PIPEWIRE_LATENCY="$latency"
        export PIPEWIRE_LATENCY
    fi
    exec pw-jack "$APP" "$@"
fi
exec "$APP" "$@"
