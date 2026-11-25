#!/usr/bin/env bash
# Tests tools/linux-launcher.sh (#70): the app is started through pw-jack only
# when pw-jack is installed and PipeWire is running, and every argument
# reaches the app unchanged. Run by tools/package-linux.sh with the tests.
#   bash tools/linux-launcher-test.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
failures=0

# A stand-in app that says how it was started, and a stand-in pw-jack.
mkdir -p "$work/opt/bin" "$work/bin" "$work/run"
cat > "$work/opt/bin/app" <<'EOF'
#!/bin/sh
printf 'app'
for arg in "$@"; do printf ' [%s]' "$arg"; done
[ -n "${PIPEWIRE_LATENCY:-}" ] && printf ' latency=%s' "$PIPEWIRE_LATENCY"
printf '\n'
EOF
cat > "$work/bin/pw-jack" <<'EOF'
#!/bin/sh
printf 'pw-jack '
exec "$@"
EOF
chmod +x "$work/opt/bin/app" "$work/bin/pw-jack"

# The launcher as the .deb installs it (the app's path), and as the
# AppImage's AppRun (next to it, wherever the AppImage is).
names=(-e "s|@ORGANIZATION@|GigChain|" -e "s|@NAME@|GigChain Keys|") # (as tools/package-linux.sh)
sed -e "s|^APP=@APP@\$|APP=\"$work/opt/bin/app\"|" "${names[@]}" "$ROOT/tools/linux-launcher.sh" > "$work/launcher"
sed -e 's|^APP=@APP@$|APP="${APPDIR:-$(dirname "$(readlink -f "$0")")}/opt/bin/app"|' "${names[@]}" \
    "$ROOT/tools/linux-launcher.sh" > "$work/AppRun"
chmod +x "$work/launcher" "$work/AppRun"
grep -q '^[^#]*@[A-Z][A-Z]*@' "$work/launcher" "$work/AppRun" && { echo "FAIL: a placeholder was not replaced" >&2; exit 1; }

# Saved settings, as Qt writes them (~/.config/<organization>/<name>.conf).
settings() { # <folder> <file contents>
    mkdir -p "$1/GigChain"
    printf '%b' "$2" > "$1/GigChain/GigChain Keys.conf"
}
settings "$work/cfg-256" '[General]\nbufferFrames=999\n\n[audio]\ndriver=jack\nbufferFrames=256\nsampleRate=48000\n'
settings "$work/cfg-own-rate" '[audio]\nbufferFrames=128\nsampleRate=0\n'
settings "$work/cfg-crlf" '[audio]\r\nbufferFrames=64\r\nsampleRate=44100\r\n'
settings "$work/cfg-none" '[audio]\ndriver=jack\n'
settings "$work/cfg-garbage" '[audio]\nbufferFrames=lots\n'
settings "$work/cfg-huge" '[audio]\nbufferFrames=1000000\n'
settings "$work/home/.config" '[audio]\nbufferFrames=256\nsampleRate=48000\n'

# PipeWire running: its socket in the runtime folder.
python3 -c 'import socket, sys; socket.socket(socket.AF_UNIX).bind(sys.argv[1])' "$work/run/pipewire-0"
mkdir -p "$work/norun"

# expect <name> <expected output> <env...> -- <launcher> <args...>
expect() {
    local name="$1" want="$2"
    shift 2
    local envs=()
    while [ "$1" != -- ]; do envs+=("$1"); shift; done
    shift
    local got
    got="$(env -i "${envs[@]}" /bin/sh "$@" 2>&1)" || true
    if [ "$got" = "$want" ]; then
        echo "ok   $name"
    else
        echo "FAIL $name: got '$got', want '$want'"
        failures=$((failures + 1))
    fi
}
WITH_PW_JACK="PATH=$work/bin:/usr/bin:/bin"
WITHOUT_PW_JACK="PATH=$work/opt:/usr/bin:/bin" # (a folder with no pw-jack)
# (A system pw-jack would stand in for the missing one: the runner has none.)
if command -v pw-jack >/dev/null 2>&1 && [ "$(command -v pw-jack)" != "$work/bin/pw-jack" ]; then
    echo "pw-jack is installed here: its missing case cannot be tested" >&2
    WITHOUT_PW_JACK=""
fi

expect "pw-jack and PipeWire: started through pw-jack" "pw-jack app [song.gigchain]" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" -- "$work/launcher" song.gigchain
expect "arguments arrive unchanged" "pw-jack app [My Set List.gigchain] [] [a\"b'c]" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" -- "$work/launcher" "My Set List.gigchain" "" "a\"b'c"
expect "no arguments" "pw-jack app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" -- "$work/launcher"
expect "PipeWire not running (JACK itself, or PulseAudio): started directly" "app [x]" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/norun" -- "$work/launcher" x
expect "PipeWire's own runtime folder is used first" "pw-jack app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/norun" "PIPEWIRE_RUNTIME_DIR=$work/run" -- "$work/launcher"
expect "a PipeWire remote named by its full path" "pw-jack app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/norun" "PIPEWIRE_REMOTE=$work/run/pipewire-0" -- "$work/launcher"
expect "a PipeWire remote that is not there: started directly" "app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "PIPEWIRE_REMOTE=elsewhere-0" -- "$work/launcher"
expect "GIGCHAIN_NO_PW_JACK: started directly" "app [x]" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "GIGCHAIN_NO_PW_JACK=1" -- "$work/launcher" x
if [ -n "$WITHOUT_PW_JACK" ]; then
    expect "no pw-jack installed: started directly" "app [x]" \
        "$WITHOUT_PW_JACK" "XDG_RUNTIME_DIR=$work/run" -- "$work/launcher" x
fi
expect "AppImage: the app next to AppRun, through pw-jack" "pw-jack app [x]" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" -- "$work/AppRun" x
expect "AppImage: APPDIR, as the AppImage runtime sets it" "pw-jack app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "APPDIR=$work" -- "$work/AppRun"
ln -s "$work/AppRun" "$work/bin/linked-AppRun"
expect "AppImage: started through a link to AppRun" "app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/norun" -- "$work/bin/linked-AppRun"

# The buffer size saved in Settings is asked of PipeWire (#83).
expect "saved buffer: PipeWire is asked for it" "pw-jack app [x] latency=256/48000" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "XDG_CONFIG_HOME=$work/cfg-256" -- "$work/launcher" x
expect "saved buffer, the device's own rate: 48 kHz assumed" "pw-jack app latency=128/48000" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "XDG_CONFIG_HOME=$work/cfg-own-rate" -- "$work/launcher"
expect "saved buffer, Windows line ends" "pw-jack app latency=64/44100" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "XDG_CONFIG_HOME=$work/cfg-crlf" -- "$work/launcher"
expect "saved buffer under HOME's .config" "pw-jack app latency=256/48000" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "HOME=$work/home" -- "$work/launcher"
expect "PIPEWIRE_LATENCY already set: left as it is" "pw-jack app latency=512/48000" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "XDG_CONFIG_HOME=$work/cfg-256" "PIPEWIRE_LATENCY=512/48000" -- "$work/launcher"
expect "no buffer saved: nothing asked" "pw-jack app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "XDG_CONFIG_HOME=$work/cfg-none" -- "$work/launcher"
expect "a buffer that is not a number: nothing asked" "pw-jack app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "XDG_CONFIG_HOME=$work/cfg-garbage" -- "$work/launcher"
expect "an impossible buffer: nothing asked" "pw-jack app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "XDG_CONFIG_HOME=$work/cfg-huge" -- "$work/launcher"
expect "no settings file: nothing asked" "pw-jack app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/run" "XDG_CONFIG_HOME=$work/nowhere" -- "$work/launcher"
expect "not through pw-jack: nothing asked" "app" \
    "$WITH_PW_JACK" "XDG_RUNTIME_DIR=$work/norun" "XDG_CONFIG_HOME=$work/cfg-256" -- "$work/launcher"

if [ "$failures" -gt 0 ]; then
    echo "$failures launcher check(s) failed" >&2
    exit 1
fi
echo "the launcher starts the app as it should"
