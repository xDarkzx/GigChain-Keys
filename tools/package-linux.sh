#!/usr/bin/env bash
# What Linux players download: an AppImage (one file, runs on most
# distributions) and a .deb (installs on Ubuntu, Debian, Mint...), both from
# one Release build with every test run on it first. Built on Ubuntu 22.04:
# what it links against is that old or older, so newer systems run it.
#   bash tools/package-linux.sh             build, test, stage, check, pack
#   bash tools/package-linux.sh --skip-tests only when the same commit was just tested
# Out: dist/GigChainKeys-<version>-x86_64.AppImage and
#      dist/gigchain-keys_<version>_amd64.deb
#
# Steps: Release build -> ctest -> cmake --install into an AppDir ->
# linuxdeploy with its Qt plugin (Qt's libraries, plugins and the QML the app
# uses, next to the app; the libraries find each other by RPATH) -> checks
# (every library found inside the package or on any Linux system; the
# scanner starts on its own) -> the AppImage, and the .deb from the same
# files under /opt.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SKIP_TESTS=0
[ "${1:-}" = "--skip-tests" ] && SKIP_TESTS=1
export PATH="$HOME/.local/bin:$PATH"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export QT_ROOT_DIR="${QT_ROOT_DIR:-$HOME/Qt/6.10.2/gcc_64}"
PRESET=linux-release
BUILD="${GIGCHAIN_BUILD_DIR:-$HOME/.cache/gigchain/build/$PRESET}"
TOOLS="$HOME/.cache/gigchain/tools"
DIST="$ROOT/dist"

branding() { sed -n "s/^set($1 \"\\([^\"]*\\)\").*/\\1/p" "$ROOT/branding.cmake"; }
EXE="$(branding PRODUCT_EXECUTABLE)"
VERSION="$(branding PRODUCT_VERSION)"
EXT="$(branding PRODUCT_FILE_EXTENSION)"
WEBSITE="$(branding PRODUCT_WEBSITE)"
NAME="GigChain Keys"           # (branding.cmake's PRODUCT_NAME)
PACKAGE=gigchain-keys          # the .deb's name, the desktop file's and the icon's
[ -n "$EXE" ] && [ -n "$VERSION" ] || { echo "Could not read the name and version from branding.cmake" >&2; exit 1; }

step() { echo "== $*"; }

# ------------------------------------------------------------ build and test
step configure
cmake --preset "$PRESET" >/dev/null
step build
cmake --build --preset "$PRESET"
if [ "$SKIP_TESTS" = 0 ]; then
    step tests
    QT_QPA_PLATFORM=offscreen ctest --preset "$PRESET"
fi

# ------------------------------------------------------------ the tools
# linuxdeploy and its Qt plugin (AppImages themselves: run extracted, so no
# FUSE is needed, as in WSL or a container).
mkdir -p "$TOOLS"
fetch() {
    [ -x "$TOOLS/$1" ] && return
    curl -fsSL "$2" -o "$TOOLS/$1.part"
    chmod +x "$TOOLS/$1.part"
    mv "$TOOLS/$1.part" "$TOOLS/$1"
}
fetch linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fetch appimagetool-x86_64.AppImage \
    https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
export APPIMAGE_EXTRACT_AND_RUN=1 PATH="$TOOLS:$PATH"

# ------------------------------------------------------------ stage
WORK="$BUILD/linux-package"
APPDIR="$WORK/AppDir"
rm -rf "$WORK"
mkdir -p "$APPDIR/usr/bin" "$DIST"
step install
cmake --install "$BUILD" --prefix "$APPDIR/usr/bin" >/dev/null
[ -x "$APPDIR/usr/bin/$EXE" ] && [ -x "$APPDIR/usr/bin/${EXE}Scan" ] || { echo "The app or its scanner was not installed" >&2; exit 1; }

# The menu entry, the icon, and setlists opening in the app.
cat > "$WORK/$PACKAGE.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$NAME
GenericName=Live keyboard rig
Comment=Play your VST3 instruments live: setlists, sounds, chord charts
Exec=$EXE %f
Icon=$PACKAGE
Terminal=false
Categories=AudioVideo;Audio;Music;Midi;
Keywords=keyboard;synth;piano;vst;live;setlist;mainstage;
MimeType=application/x-$PACKAGE-setlist;
StartupWMClass=$EXE
EOF
cp "$ROOT/branding/app-icon.png" "$WORK/$PACKAGE.png" # (256 x 256)
cat > "$WORK/$PACKAGE.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-$PACKAGE-setlist">
    <comment>$NAME setlist</comment>
    <sub-class-of type="application/json"/>
    <glob pattern="*.$EXT.json" weight="80"/>
    <glob pattern="*.$EXT"/>
  </mime-type>
</mime-info>
EOF

# Qt and the app's own libraries, next to it. The sound and graphics systems
# (ALSA, PulseAudio, PipeWire, JACK, the GPU's OpenGL) stay the player's own:
# a copy from the build machine would not talk to their sound server or GPU.
step linuxdeploy
export QMAKE="$QT_ROOT_DIR/bin/qmake" QML_SOURCES_PATHS="$ROOT/src/ui" LD_LIBRARY_PATH="$QT_ROOT_DIR/lib:${LD_LIBRARY_PATH:-}"
export EXTRA_QT_MODULES="multimedia" # (backing tracks: Qt's FFmpeg plugin and libraries)
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/$EXE" --executable "$APPDIR/usr/bin/${EXE}Scan" \
    --desktop-file "$WORK/$PACKAGE.desktop" --icon-file "$WORK/$PACKAGE.png" \
    --exclude-library 'libasound.so*' --exclude-library 'libpulse*.so*' --exclude-library 'libpipewire*.so*' \
    --exclude-library 'libjack.so*' --exclude-library 'libGL*.so*' --exclude-library 'libEGL*.so*' \
    --plugin qt
mkdir -p "$APPDIR/usr/share/mime/packages"
cp "$WORK/$PACKAGE.xml" "$APPDIR/usr/share/mime/packages/"
# Qt's database drivers for ODBC, PostgreSQL and Mimer need libraries no
# player has: they could never load (the app uses no database).
rm -f "$APPDIR"/usr/plugins/sqldrivers/libqsql{odbc,psql,mimer}.so

# ------------------------------------------------------------ checks
step checks
unset LD_LIBRARY_PATH # (linuxdeploy's: the package must find its libraries by itself)
problems=()
# Every program and library finds what it loads: inside the package, or a
# system library every desktop Linux has (not one only the build machine has).
while IFS= read -r -d '' file; do
    file -b "$file" | grep -q '^ELF' || continue
    links=$(env -i PATH=/usr/bin:/bin ldd "$file" 2>/dev/null || true)
    missing=$(grep 'not found' <<<"$links" || true)
    [ -n "$missing" ] && problems+=("${file#"$APPDIR/"}: $missing")
    leaked=$(grep -E "$QT_ROOT_DIR|$VCPKG_ROOT|$BUILD/" <<<"$links" | grep -v "$APPDIR" || true)
    [ -n "$leaked" ] && problems+=("${file#"$APPDIR/"} loads from the build machine: $leaked")
done < <(find "$APPDIR/usr" -type f \( -perm -u+x -o -name '*.so*' \) -print0)
for needed in "usr/bin/$EXE" "usr/bin/${EXE}Scan" usr/lib/libQt6Core.so.6 usr/lib/libQt6Quick.so.6 usr/plugins/platforms/libqxcb.so \
    usr/qml/QtQuick/Controls usr/bin/LICENSE.txt usr/bin/THIRD-PARTY-NOTICES.txt; do
    [ -e "$APPDIR/$needed" ] || problems+=("missing: $needed")
done
ls "$APPDIR"/usr/plugins/multimedia/*ffmpeg* >/dev/null 2>&1 || problems+=("missing: Qt's FFmpeg plugin (backing tracks)")
# The scanner starts with only the package's libraries: it answers a missing
# plugin with its usage error (exit 2), not a missing library.
set +e
env -i PATH=/usr/bin:/bin "$APPDIR/usr/bin/${EXE}Scan" >/dev/null 2>"$WORK/scanner-check.txt"
code=$?
set -e
[ "$code" = 2 ] || problems+=("the packaged scanner did not start (exit $code): $(head -c 300 "$WORK/scanner-check.txt")")
if [ ${#problems[@]} -gt 0 ]; then
    printf 'The package is not ready:\n' >&2
    printf '  %s\n' "${problems[@]}" >&2
    exit 1
fi
echo "every library found; the scanner starts on its own"

# ------------------------------------------------------------ the AppImage
step appimage
APPIMAGE="$DIST/$EXE-$VERSION-x86_64.AppImage"
rm -f "$APPIMAGE"
ARCH=x86_64 appimagetool-x86_64.AppImage --no-appstream "$APPDIR" "$APPIMAGE" >/dev/null
chmod +x "$APPIMAGE"

# ------------------------------------------------------------ the .deb
# The same files under /opt/gigchain-keys; a command on the PATH, the menu
# entry, the icon and the file type where the desktop finds them (dpkg's
# triggers refresh the menus and file types).
step deb
DEB="$WORK/deb"
OPT="/opt/$PACKAGE"
mkdir -p "$DEB$OPT" "$DEB/usr/bin" "$DEB/usr/share/applications" "$DEB/usr/share/icons/hicolor/256x256/apps" \
    "$DEB/usr/share/mime/packages" "$DEB/usr/share/doc/$PACKAGE" "$DEB/DEBIAN"
cp -a "$APPDIR/usr/." "$DEB$OPT/"
ln -s "$OPT/bin/$EXE" "$DEB/usr/bin/$PACKAGE"
sed "s|^Exec=.*|Exec=$OPT/bin/$EXE %f|" "$WORK/$PACKAGE.desktop" > "$DEB/usr/share/applications/$PACKAGE.desktop"
cp "$WORK/$PACKAGE.png" "$DEB/usr/share/icons/hicolor/256x256/apps/"
cp "$WORK/$PACKAGE.xml" "$DEB/usr/share/mime/packages/"
cp "$APPDIR/usr/bin/LICENSE.txt" "$DEB/usr/share/doc/$PACKAGE/copyright"
SIZE=$(du -sk "$DEB" | cut -f1)
cat > "$DEB/DEBIAN/control" <<EOF
Package: $PACKAGE
Version: $VERSION
Architecture: amd64
Maintainer: $NAME contributors <noreply@github.com>
Installed-Size: $SIZE
Section: sound
Priority: optional
Homepage: $WEBSITE
Depends: libc6 (>= 2.35), libasound2, libpulse0, libgl1, libegl1, libfontconfig1, libfreetype6, libx11-6, libx11-xcb1, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-shape0, libxcb-xinerama0, libxcb-xkb1, libxkbcommon0, libxkbcommon-x11-0, libdbus-1-3
Recommends: pipewire-pulse | pulseaudio
Suggests: jackd2
Description: Live VST3 host for keyboard players
 $NAME plays your VST3 instruments live: setlists of songs and sounds,
 splits and layers, chord charts that follow the song, a loop station and
 a practice mode. Free and open source (GPL-3.0-or-later).
EOF
DEBFILE="$DIST/${PACKAGE}_${VERSION}_amd64.deb"
rm -f "$DEBFILE"
dpkg-deb --build --root-owner-group "$DEB" "$DEBFILE" >/dev/null

for f in "$APPIMAGE" "$DEBFILE"; do
    printf '%s  %s MB  SHA256 %s\n' "$(basename "$f")" "$(du -m "$f" | cut -f1)" "$(sha256sum "$f" | cut -d' ' -f1)"
done
