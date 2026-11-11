#!/usr/bin/env bash
# Run every regression test of tests/run-test.sh (except the teaching
# mode, which the port does not have) against an engine, default the Rust
# port.  Prints FAIL lines and a count.
#
# Usage: tasks/adhoc/engine-port-rust/run-regression.sh [ENGINE]
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
engine=${1:-$repo/port/rust/spimulator-rust}
# The list in meson.build, less the teaching mode.
mapfile -t tests < <(sed -n "/^regression_tests = \[/,/^]/p" "$repo/meson.build" \
    | grep -v '^ *#' | grep -o "'[^']*'" | tr -d "'" | grep -vx explain)
log=$(mktemp)
trap 'rm -f "$log"' EXIT
failed=0
for t in "${tests[@]}"; do
    if ! timeout 120 bash "$repo/tests/run-test.sh" "$engine" "$repo/src/exceptions.s" "$t" > "$log" 2>&1; then
        echo "FAIL $t"
        tail -5 "$log"
        failed=$((failed + 1))
    fi
done
echo "$(( ${#tests[@]} - failed )) passed, $failed failed (of ${#tests[@]})"
[ "$failed" = 0 ]
