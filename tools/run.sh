#!/usr/bin/env bash
# Builds (if needed) and starts GigChain Keys on Linux (or WSL, through WSLg),
# as tools\run.ps1 does on Windows. Plugin windows are X11 windows, so the app
# runs through Qt's X11 platform (xcb) unless QT_QPA_PLATFORM says otherwise.
# Its log: ~/.cache/gigchain/run.log (the app also keeps its own log file).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="$HOME/.local/bin:$PATH"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export QT_ROOT_DIR="${QT_ROOT_DIR:-$HOME/Qt/6.10.2/gcc_64}"
PRESET="${PRESET:-linux-release}"
BUILD="$HOME/.cache/gigchain/build/$PRESET"

cmake --preset "$PRESET" >/dev/null
cmake --build --preset "$PRESET"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
APP="$(find "$BUILD" -maxdepth 3 -type f -name 'GigChain*' -perm -u+x ! -name '*Scan*' | head -1)"
if [ -z "$APP" ]; then
    echo "The app was not found under $BUILD" >&2
    exit 1
fi
mkdir -p "$HOME/.cache/gigchain"
exec "$APP" "$@" >"$HOME/.cache/gigchain/run.log" 2>&1
