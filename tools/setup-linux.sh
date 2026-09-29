#!/usr/bin/env bash
# Sets up a Linux machine (or WSL) to build GigChain Keys. Safe to run again.
#
#   sudo bash tools/setup-linux.sh --system   system packages, GCC 13, Surge XT (root)
#   bash tools/setup-linux.sh --user          CMake, Qt 6.10.2, vcpkg (your user)
#   bash tools/setup-linux.sh --check         what is installed, and what is missing
#
# In WSL the system part can run as root without a password:
#   wsl -u root bash tools/setup-linux.sh --system
set -euo pipefail

QT_VERSION=6.10.2
QT_DIR="$HOME/Qt"
VCPKG_DIR="$HOME/vcpkg"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

system_part() {
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y software-properties-common
    # GCC 13: Ubuntu 22.04's GCC 11 lacks C++20 parts the code uses.
    add-apt-repository -y ppa:ubuntu-toolchain-r/test
    apt-get update
    apt-get install -y \
        gcc-13 g++-13 build-essential ninja-build pkg-config git curl zip unzip tar \
        python3-pip python3-venv autoconf autoconf-archive automake libtool bison flex \
        libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libxkbcommon-x11-0 \
        libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
        libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libx11-dev \
        libx11-xcb-dev libxcb1-dev libxrandr-dev libxext-dev libxcursor-dev \
        libfontconfig1-dev libfreetype6-dev libasound2-dev libpulse-dev libjack-jackd2-dev \
        libgstreamer1.0-0 libgstreamer-plugins-base1.0-0 imagemagick x11-utils
    # Surge XT: a free Linux VST3 instrument for the tests that need a real one.
    if [ ! -d /usr/lib/vst3/Surge\ XT.vst3 ]; then
        local url
        url="$(curl -fsSL https://api.github.com/repos/surge-synthesizer/releases-xt/releases/latest \
            | grep -o '"browser_download_url": *"[^"]*\.deb"' | grep -iE 'linux|x86_64|x64' | head -1 \
            | sed 's/.*"\(https[^"]*\)"/\1/')"
        if [ -z "$url" ]; then
            echo "Could not find Surge XT's Linux .deb on GitHub" >&2
            exit 1
        fi
        curl -fL "$url" -o /tmp/surge-xt.deb
        apt-get install -y /tmp/surge-xt.deb
    fi
}

user_part() {
    # CMake: the project needs 3.24 or newer (22.04 has 3.22).
    python3 -m pip install --user --upgrade cmake aqtinstall
    export PATH="$HOME/.local/bin:$PATH"
    # Qt, the official binaries (as on Windows).
    if [ ! -d "$QT_DIR/$QT_VERSION/gcc_64" ]; then
        # (From /tmp: aqt leaves its log in the folder it runs in.)
        (cd /tmp && aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -m qtmultimedia qtshadertools -O "$QT_DIR")
    fi
    # vcpkg at the manifest's baseline.
    local baseline
    baseline="$(grep -o '"builtin-baseline": *"[0-9a-f]*"' "$ROOT/vcpkg.json" | grep -o '[0-9a-f]\{40\}')"
    if [ ! -d "$VCPKG_DIR/.git" ]; then
        git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
    fi
    git -C "$VCPKG_DIR" fetch --quiet origin
    git -C "$VCPKG_DIR" checkout --quiet "$baseline"
    "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
}

check() {
    export PATH="$HOME/.local/bin:$PATH"
    local missing=0
    report() { if [ -n "$2" ]; then echo "  $1: $2"; else echo "  $1: MISSING"; missing=1; fi; }
    echo "GigChain Keys build tools:"
    report "g++-13" "$(g++-13 --version 2>/dev/null | head -1 || true)"
    report "cmake" "$(cmake --version 2>/dev/null | head -1 || true)"
    report "ninja" "$(ninja --version 2>/dev/null || true)"
    report "Qt $QT_VERSION" "$( [ -d "$QT_DIR/$QT_VERSION/gcc_64" ] && echo "$QT_DIR/$QT_VERSION/gcc_64" || true)"
    report "vcpkg" "$( [ -x "$VCPKG_DIR/vcpkg" ] && echo "$VCPKG_DIR" || true)"
    report "Surge XT" "$( [ -d "/usr/lib/vst3/Surge XT.vst3" ] && echo "/usr/lib/vst3/Surge XT.vst3" || true)"
    return $missing
}

case "${1:-}" in
    --system) system_part ;;
    --user) user_part ;;
    --check) check ;;
    *) echo "usage: $0 --system | --user | --check" >&2; exit 2 ;;
esac
