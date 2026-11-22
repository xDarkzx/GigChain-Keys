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
sed "s|^APP=@APP@\$|APP=\"$work/opt/bin/app\"|" "$ROOT/tools/linux-launcher.sh" > "$work/launcher"
sed 's|^APP=@APP@$|APP="${APPDIR:-$(dirname "$(readlink -f "$0")")}/opt/bin/app"|' "$ROOT/tools/linux-launcher.sh" > "$work/AppRun"
chmod +x "$work/launcher" "$work/AppRun"
grep -q '^APP=@APP@' "$work/launcher" "$work/AppRun" && { echo "FAIL: @APP@ was not replaced" >&2; exit 1; }

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

if [ "$failures" -gt 0 ]; then
    echo "$failures launcher check(s) failed" >&2
    exit 1
fi
echo "the launcher starts the app as it should"
