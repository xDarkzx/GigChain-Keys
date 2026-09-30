#!/usr/bin/env bash
# Tests tools/package-mac.sh at home (Linux, LLVM 18): a fake build folder
# laid out as the mac-release build is, a real Mach-O app in it, and
# stand-ins for macdeployqt, codesign and hdiutil that record what they are
# asked. Checks the licences land in the app and the .dmg, the order (licences
# before signing), and that a missing licence fails the package.
#   bash tools/package-mac-test.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export OTOOL=llvm-otool-18
CC=(clang-18 --target=arm64-apple-macos13 -fuse-ld=lld -nostdlib -Wl,-platform_version,macos,13.0,13.0)
echo 'void binder(void) __asm__("dyld_stub_binder"); void binder(void) {}' > "$work/system.c"
"${CC[@]}" -shared -Wl,-install_name,/usr/lib/libSystem.B.dylib "$work/system.c" -o "$work/libSystem.B.dylib"
echo 'int main(void) { return 0; }' > "$work/main.c"

build="$work/build/mac-release"
app="$build/GigChain Keys.app/Contents"
mkdir -p "$app/MacOS" "$app/Resources" "$build/installer" "$build/_deps/vst3sdk-src"
"${CC[@]}" "$work/libSystem.B.dylib" "$work/main.c" -o "$app/MacOS/GigChain Keys"
"${CC[@]}" "$work/libSystem.B.dylib" "$work/main.c" -o "$app/MacOS/GigChainKeysScan"
echo notices > "$build/installer/THIRD-PARTY-NOTICES.txt"
echo vst3 > "$build/_deps/vst3sdk-src/LICENSE.txt"
for port in rtaudio rtmidi tl-expected; do
    mkdir -p "$build/vcpkg_installed/arm64-osx-13/share/$port"
    echo "$port" > "$build/vcpkg_installed/arm64-osx-13/share/$port/copyright"
done

# Stand-ins: each records its call; hdiutil records what the .dmg would hold.
bin="$work/bin"
mkdir -p "$bin" "$work/qt/bin"
log="$work/calls.log"
cat > "$work/qt/bin/macdeployqt" <<EOF
#!/usr/bin/env bash
echo "macdeployqt \$*" >> "$log"
EOF
cat > "$bin/codesign" <<EOF
#!/usr/bin/env bash
echo "codesign \$* licences=\$(ls "$app/Resources/licenses" 2>/dev/null | tr '\n' ' ')" >> "$log"
EOF
cat > "$bin/hdiutil" <<EOF
#!/usr/bin/env bash
src=""; out=""
while [ \$# -gt 0 ]; do case "\$1" in -srcfolder) src="\$2"; shift ;; -volname|-format) shift ;; -ov) ;; *) out="\$1" ;; esac; shift; done
(cd "\$src" && find . | sort) > "$work/dmg-contents.txt"
touch "\$out"
echo "hdiutil -> \$out" >> "$log"
EOF
chmod +x "$work/qt/bin/macdeployqt" "$bin/codesign" "$bin/hdiutil"

fail=0
check() { if eval "$2"; then echo "PASS: $1"; else echo "FAIL: $1"; fail=1; fi; }

PATH="$bin:$PATH" QT_ROOT_DIR="$work/qt" bash "$ROOT/tools/package-mac.sh" "$build" 9.9.9 > "$work/out.txt" 2>&1 ||
    { echo "FAIL: packaging stopped:"; cat "$work/out.txt"; exit 1; }
check "macdeployqt was given the scanner too" 'grep -q "executable=.*GigChainKeysScan" "$log"'
check "the licences were in the app before it was signed" 'grep -q "codesign --force.*licences=rtaudio.txt rtmidi.txt tl-expected.txt vst3sdk.txt" "$log"'
check "the app carries the licence and the notices" '[ -f "$app/Resources/LICENSE.txt" ] && [ -f "$app/Resources/THIRD-PARTY-NOTICES.txt" ]'
check "the .dmg holds the app, Applications, the licence, the notices and the licences folder" \
    'grep -qx "./GigChain Keys.app" "$work/dmg-contents.txt" && grep -qx "./Applications" "$work/dmg-contents.txt" &&
     grep -qx "./LICENSE.txt" "$work/dmg-contents.txt" && grep -qx "./THIRD-PARTY-NOTICES.txt" "$work/dmg-contents.txt" &&
     grep -qx "./licenses/vst3sdk.txt" "$work/dmg-contents.txt"'
check "the .dmg is named with the version" 'grep -q "GigChain Keys-9.9.9-arm64.dmg" "$log"'

# A licence missing: the package stops, naming it.
rm "$build/vcpkg_installed/arm64-osx-13/share/rtmidi/copyright"
if PATH="$bin:$PATH" QT_ROOT_DIR="$work/qt" bash "$ROOT/tools/package-mac.sh" "$build" 9.9.9 > "$work/out2.txt" 2>&1; then
    echo "FAIL: a missing licence was packaged anyway"; fail=1
else
    check "a missing licence stops the package, named" 'grep -q "missing: .*rtmidi/copyright" "$work/out2.txt"'
fi
exit "$fail"
