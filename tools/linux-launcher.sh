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
APP=@APP@

pipewire_running() {
    remote="${PIPEWIRE_REMOTE:-pipewire-0}"
    case "$remote" in
        /*) socket="$remote" ;;
        *) socket="${PIPEWIRE_RUNTIME_DIR:-${XDG_RUNTIME_DIR:-/run/user/$(id -u)}}/$remote" ;;
    esac
    [ -S "$socket" ]
}

if [ -z "${GIGCHAIN_NO_PW_JACK:-}" ] && command -v pw-jack >/dev/null 2>&1 && pipewire_running; then
    exec pw-jack "$APP" "$@"
fi
exec "$APP" "$@"
