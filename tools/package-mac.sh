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
# Ad-hoc signed as a whole (no hardened runtime: plugins signed by anyone
# load), then checked: a bundle that fails the check is never shipped.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
if [ -f "$BUILD/installer/THIRD-PARTY-NOTICES.txt" ]; then cp "$BUILD/installer/THIRD-PARTY-NOTICES.txt" "$STAGE/"; fi
rm -f "$DMG"
hdiutil create -volname "GigChain Keys" -srcfolder "$STAGE" -format UDZO -ov "$DMG"
echo "Made $DMG"
