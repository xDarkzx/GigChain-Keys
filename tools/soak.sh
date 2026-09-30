#!/usr/bin/env bash
# The soak on Linux (or WSL), as tools\soak.ps1 on Windows: builds the soak
# and runs it for a while against the installed plugins (Surge XT from
# tools/setup-linux.sh), silently.
#   bash tools/soak.sh [minutes (20)] [preset (linux-release)]
# Readings every 10 s go to <build>/soak.csv; its report to <build>/soak.log.
# Run it in a terminal of its own: a soak outlasts most tool time limits.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
MINUTES="${1:-20}"
PRESET="${2:-linux-release}"
export PATH="$HOME/.local/bin:$PATH"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export QT_ROOT_DIR="${QT_ROOT_DIR:-$HOME/Qt/6.10.2/gcc_64}"
BUILD="$HOME/.cache/gigchain/build/$PRESET"

cmake --preset "$PRESET" >/dev/null
cmake --build --preset "$PRESET" --target gigchain_soak
echo "Soaking for $MINUTES minutes ($PRESET); readings in $BUILD/soak.csv"
QT_QPA_PLATFORM=offscreen "$BUILD/gigchain_soak" "$MINUTES" "$BUILD/soak.csv" 2>&1 | tee "$BUILD/soak.log" |
    grep -E '^(playing|after warm-up|handles:|SOAK|  )' || true
grep -q '^SOAK PASSED' "$BUILD/soak.log"
