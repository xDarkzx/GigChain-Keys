#!/usr/bin/env bash
# Builds GigChain Keys on Linux (or WSL) and runs every test: the Linux
# counterpart of tools\verify.ps1's build and test steps.
#   bash tools/verify.sh              the debug build, every test
#   bash tools/verify.sh <regex>      only the tests matching <regex>
#   PRESET=linux-asan bash tools/verify.sh
# First run: tools/setup-linux.sh (--system, then --user).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="$HOME/.local/bin:$PATH"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export QT_ROOT_DIR="${QT_ROOT_DIR:-$HOME/Qt/6.10.2/gcc_64}"
PRESET="${PRESET:-linux-debug}"

cmake --preset "$PRESET"
cmake --build --preset "$PRESET"
if [ $# -gt 0 ]; then
    ctest --preset "$PRESET" -R "$1"
else
    ctest --preset "$PRESET"
fi
