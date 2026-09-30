#!/usr/bin/env bash
# Fails when any binary in a Mac app loads a library from outside the app and
# macOS (a path into the build machine, e.g. Homebrew or Qt's install, would
# fail on a player's Mac). Reads every Mach-O file, universal ones too.
#   bash tools/check-mac-links.sh <app bundle>
# (OTOOL=llvm-otool checks one from elsewhere: tools/check-mac-links-test.sh.)
set -euo pipefail
APP="${1:?the app bundle}"
OTOOL="${OTOOL:-otool}"
[ -d "$APP/Contents" ] || { echo "No app bundle at $APP" >&2; exit 1; }

bad="$(find "$APP/Contents" -type f -print0 | while IFS= read -r -d '' f; do
    file -b "$f" | grep -q 'Mach-O' || continue
    # A library's own name (its install name, once per architecture): not
    # something it loads. Programs and plugin bundles have none.
    self="$("$OTOOL" -D "$f" | grep -v ':$' || true)"
    # What it loads: the tab-indented lines (the others name the file and,
    # in a universal binary, each architecture).
    "$OTOOL" -L "$f" | grep -E $'^\t' | awk '{print $1}' | sort -u | while IFS= read -r lib; do
        if [ -n "$self" ] && printf '%s\n' "$self" | grep -Fxq "$lib"; then continue; fi
        case "$lib" in
            @rpath/* | @loader_path/* | @executable_path/* | /System/* | /usr/lib/*) ;;
            *) echo "${f#"$APP"/}: $lib" ;;
        esac
    done
done)"
if [ -n "$bad" ]; then
    echo "Libraries loaded from outside the app and macOS:" >&2
    echo "$bad" >&2
    exit 1
fi
echo "Every binary in $(basename "$APP") loads only the app's own libraries and macOS's"
