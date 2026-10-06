#!/usr/bin/env bash
# The fuzzers on Linux (Clang's libFuzzer with AddressSanitizer), as
# tools\fuzz.ps1 on Windows: each fuzzer for a while, keeping what it finds.
#   bash tools/fuzz.sh [seconds per fuzzer (300)] [only this fuzzer]
# Inputs that crash, hang or break a promise are saved to
# <build>/crashes/<fuzzer>/; once fixed, copy one into
# tests/fuzz/corpus/<fuzzer>/ so ctest keeps it fixed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
SECONDS_EACH="${1:-300}"
ONLY="${2:-}"
export PATH="$HOME/.local/bin:$PATH"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
export QT_ROOT_DIR="${QT_ROOT_DIR:-$HOME/Qt/6.10.2/gcc_64}"
BUILD="$HOME/.cache/gigchain/build/linux-fuzz"

cmake --preset linux-fuzz >/dev/null
cmake --build --preset linux-fuzz --target fuzz_setlist_json fuzz_chart fuzz_plugin_state fuzz_plugin_files fuzz_midi
failed=()
for seeds in tests/fuzz/corpus/*/; do
    name="$(basename "$seeds")"
    if [ -n "$ONLY" ] && [ "$name" != "$ONLY" ]; then continue; fi
    corpus="$BUILD/corpus/$name"
    crashes="$BUILD/crashes/$name"
    mkdir -p "$corpus" "$crashes"
    echo "== $name for $SECONDS_EACH s"
    log="$BUILD/$name.log"
    # (Without address randomisation: Clang 15's sanitizer runtime crashes at
    # random at start-up where the kernel randomises with more than 28 bits;
    # see tests/fuzz/CMakeLists.txt.)
    if ! setarch "$(uname -m)" -R "$BUILD/$name" "$corpus" "$seeds" "-max_total_time=$SECONDS_EACH" -timeout=10 -rss_limit_mb=4096 \
        "-artifact_prefix=$crashes/" -print_final_stats=1 >"$log" 2>&1; then
        failed+=("$name")
        echo "$name stopped; the end of $log:"
        tail -25 "$log"
    fi
    grep -E '^Done|stat::number_of_executed_units' "$log" || true
done
if [ ${#failed[@]} -gt 0 ]; then
    echo "Fuzzers found problems: ${failed[*]} (inputs in $BUILD/crashes)"
    exit 1
fi
echo "No fuzzer found a problem."
