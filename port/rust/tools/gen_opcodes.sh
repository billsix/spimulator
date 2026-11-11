#!/usr/bin/env bash
# Regenerate port/rust/src/opcodes_table.rs from include/opcodes.h.
#
# Usage (from anywhere): port/rust/tools/gen_opcodes.sh
#
# Compiles tools/gen_opcodes.c against the C engine's include/ directory,
# runs it, and formats the result with rustfmt.  Run it after any change
# to include/opcodes.h, then commit the regenerated file.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$here/../../.." && pwd)
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cc -std=gnu23 -I "$repo/include" -o "$scratch/gen_opcodes" "$here/gen_opcodes.c"
"$scratch/gen_opcodes" > "$scratch/opcodes_table.rs"
rustfmt --edition 2021 "$scratch/opcodes_table.rs"
cp "$scratch/opcodes_table.rs" "$here/../src/opcodes_table.rs"
echo "wrote port/rust/src/opcodes_table.rs"
