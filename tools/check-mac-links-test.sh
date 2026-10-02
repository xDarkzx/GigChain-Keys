#!/usr/bin/env bash
# Tests tools/check-mac-links.sh at home (Linux, LLVM 18): builds real Mach-O
# files for Apple Silicon with LLVM's linker into a fake app bundle and checks
# what the script says about them.
#   bash tools/check-mac-links-test.sh
#   MAC_BASH=/opt/bash32/bin/bash bash tools/check-mac-links-test.sh
#     runs the script under the bash macOS ships (3.2), as the Mac does.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export OTOOL=llvm-otool-18
CC=(clang-18 --target=arm64-apple-macos13 -fuse-ld=lld -nostdlib -Wl,-platform_version,macos,13.0,13.0)
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if [ -n "${MAC_BASH:-}" ]; then
    mkdir -p "$work/macbash" && ln -s "$MAC_BASH" "$work/macbash/bash"
    export PATH="$work/macbash:$PATH"
fi
echo "the script runs under: $(bash --version | head -1)"
echo 'int f(void) { return 1; }' > "$work/f.c"
echo 'int f(void); int main(void) { return f(); }' > "$work/main.c"
# A stand-in for macOS's libSystem (every Mac program loads it from /usr/lib).
echo 'void binder(void) __asm__("dyld_stub_binder"); void binder(void) {}' > "$work/system.c"
"${CC[@]}" -shared -Wl,-install_name,/usr/lib/libSystem.B.dylib "$work/system.c" -o "$work/libSystem.B.dylib"
CC+=("$work/libSystem.B.dylib")

app="$work/Test App.app/Contents"
mkdir -p "$app/MacOS" "$app/Frameworks" "$app/PlugIns/platforms"
# A library of the app's own (named @rpath/...), a plugin with a bare name,
# and a program loading the library through @rpath.
"${CC[@]}" -shared -Wl,-install_name,@rpath/libgood.dylib "$work/f.c" -o "$app/Frameworks/libgood.dylib"
"${CC[@]}" -shared -Wl,-install_name,libqcocoa.dylib "$work/f.c" -o "$app/PlugIns/platforms/libqcocoa.dylib"
"${CC[@]}" "$work/main.c" "$app/Frameworks/libgood.dylib" -Wl,-rpath,@executable_path/../Frameworks -o "$app/MacOS/Test App"
echo 'not a binary' > "$app/Info.plist"
# A universal library (x86_64 and arm64, as Qt's frameworks are): otool
# prints a line per architecture that names the file itself.
for arch in x86_64 arm64; do
    clang-18 --target=$arch-apple-macos13 -fuse-ld=lld -nostdlib -Wl,-platform_version,macos,13.0,13.0 -shared \
        -Wl,-install_name,@rpath/QtFake.framework/Versions/A/QtFake "$work/f.c" -o "$work/QtFake.$arch"
done
mkdir -p "$app/Frameworks/QtFake.framework/Versions/A"
llvm-lipo-18 -create "$work/QtFake.x86_64" "$work/QtFake.arm64" -output "$app/Frameworks/QtFake.framework/Versions/A/QtFake"

fail=0
if ! out="$(bash "$ROOT/tools/check-mac-links.sh" "$work/Test App.app" 2>&1)"; then
    echo "FAIL: a sound bundle was refused:"; echo "$out"; fail=1
else
    echo "PASS: a sound bundle (spaces, own names, a bare plugin name) is accepted"
fi

# A library from the build machine (Homebrew): refused, naming it.
"${CC[@]}" -shared -Wl,-install_name,/opt/homebrew/lib/libbad.dylib "$work/f.c" -o "$work/libbad.dylib"
"${CC[@]}" "$work/main.c" "$work/libbad.dylib" -o "$app/MacOS/helper"
if out="$(bash "$ROOT/tools/check-mac-links.sh" "$work/Test App.app" 2>&1)"; then
    echo "FAIL: a Homebrew library was accepted"; fail=1
elif ! grep -q "MacOS/helper: /opt/homebrew/lib/libbad.dylib" <<<"$out"; then
    echo "FAIL: the refusal does not name it:"; echo "$out"; fail=1
else
    echo "PASS: a library from outside is refused, named"
fi
exit "$fail"
