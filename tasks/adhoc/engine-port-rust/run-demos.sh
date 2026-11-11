#!/usr/bin/env bash
# Run every examples demo of examples/tests/run-demo.sh against an engine,
# default the Rust port.  The C demo binaries come from a meson build
# directory (default /tmp/claude-0/rsport-ref).
#
# Usage: tasks/adhoc/engine-port-rust/run-demos.sh [ENGINE] [BUILDDIR]
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
engine=${1:-$repo/port/rust/spimulator-rust}
builddir=${2:-/tmp/claude-0/rsport-ref}
mapfile -t demos < <(sed -n "/^lib_demo_tests = \[/,/^]/p" "$repo/examples/src/meson.build" \
    | grep -o "'[^']*'" | tr -d "'")
log=$(mktemp)
trap 'rm -f "$log"' EXIT
failed=0
for d in "${demos[@]}"; do
    if ! timeout 120 bash "$repo/examples/tests/run-demo.sh" "$builddir/examples/src" \
            "$engine" "$repo/src/exceptions.s" "$d" > "$log" 2>&1; then
        echo "FAIL $d"
        tail -8 "$log"
        failed=$((failed + 1))
    fi
done
echo "$(( ${#demos[@]} - failed )) passed, $failed failed (of ${#demos[@]})"
[ "$failed" = 0 ]
