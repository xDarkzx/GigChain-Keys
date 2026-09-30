#!/usr/bin/env bash
# The Mac's .dmg: Qt inside the app (macdeployqt), the whole bundle ad-hoc
# signed and verified, then a compressed disk image with an Applications
# link and the licences.
#   bash tools/package-mac.sh <build dir> <version>
set -euo pipefail
BUILD="$1"
VERSION="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$BUILD/GigChain Keys.app"
DMG="$BUILD/GigChain Keys-$VERSION-arm64.dmg"
[ -d "$APP" ] || { echo "No app bundle at $APP" >&2; exit 1; }
[ -x "$APP/Contents/MacOS/GigChainKeysScan" ] || { echo "The plugin scanner is missing from $APP" >&2; exit 1; }
[ -n "${QT_ROOT_DIR:-}" ] || { echo "QT_ROOT_DIR is not set (Qt's folder, e.g. from install-qt-action)" >&2; exit 1; }

# Qt, its plugins and the QML the app uses, into the bundle; the scanner
# (also in Contents/MacOS) gets its Qt paths fixed too.
"$QT_ROOT_DIR/bin/macdeployqt" "$APP" -qmldir="$ROOT/src/ui" \
    -executable="$APP/Contents/MacOS/GigChainKeysScan" -verbose=1
# Qt's database drivers for ODBC, PostgreSQL and Mimer come along, but the
# libraries they need do not (macdeployqt: "no file at …"): they could never
# load, and the app uses no database. SQLite's (self-contained) stays.
rm -f "$APP/Contents/PlugIns/sqldrivers/libqsqlodbc.dylib" \
      "$APP/Contents/PlugIns/sqldrivers/libqsqlpsql.dylib" \
      "$APP/Contents/PlugIns/sqldrivers/libqsqlmimer.dylib"

# Every binary loads only what is inside the app or part of macOS: a path
# into the build machine (Homebrew, Qt's install) would fail on a player's Mac.
bad="$(find "$APP/Contents" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 |
       while IFS= read -r -d '' f; do
           file -b "$f" | grep -q 'Mach-O' || continue
           otool -L "$f" | tail -n +2 | awk -v f="${f#"$APP"/}" '{print f ": " $1}'
       done | grep -vE ': (@rpath/|@loader_path/|@executable_path/|/System/|/usr/lib/)' || true)"
if [ -n "$bad" ]; then
    echo "Libraries loaded from outside the app and macOS:" >&2
    echo "$bad" >&2
    exit 1
fi

# The licences travel with the app (a dragged-out app has no .dmg around it)
# and sit in the .dmg: the notices, and the texts they point to.
NOTICES="$BUILD/installer/THIRD-PARTY-NOTICES.txt"
TRIPLET=arm64-osx-13 # (the mac-release preset's)
LICENCES=("$ROOT/LICENSE" "$NOTICES" "$BUILD/_deps/vst3sdk-src/LICENSE.txt")
for port in rtaudio rtmidi tl-expected; do LICENCES+=("$BUILD/vcpkg_installed/$TRIPLET/share/$port/copyright"); done
for file in "${LICENCES[@]}"; do
    [ -f "$file" ] || { echo "A licence the app must carry is missing: $file" >&2; exit 1; }
done
RES="$APP/Contents/Resources"
mkdir -p "$RES/licenses"
cp "$ROOT/LICENSE" "$RES/LICENSE.txt"
cp "$NOTICES" "$RES/THIRD-PARTY-NOTICES.txt"
cp "$BUILD/_deps/vst3sdk-src/LICENSE.txt" "$RES/licenses/vst3sdk.txt"
for port in rtaudio rtmidi tl-expected; do
    cp "$BUILD/vcpkg_installed/$TRIPLET/share/$port/copyright" "$RES/licenses/$port.txt"
done
# Ad-hoc signed as a whole (no hardened runtime: plugins signed by anyone
# load), then checked: a bundle that fails the check is never shipped.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
cp "$RES/LICENSE.txt" "$RES/THIRD-PARTY-NOTICES.txt" "$STAGE/"
cp -R "$RES/licenses" "$STAGE/licenses"
rm -f "$DMG"
hdiutil create -volname "GigChain Keys" -srcfolder "$STAGE" -format UDZO -ov "$DMG"
echo "Made $DMG"
