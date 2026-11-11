#!/bin/bash
# Every do-bind and let/where binding in the port has a written type
# (tasks/archive/2026/11/11/haskell-port-annotate-every-binding.md).
# Type-checks the port with -fno-code -fwrite-ide-info into its own build
# directory (so it never races the launcher's build), builds
# tools/BindingTypes.hs, and runs it on the .hie files with --check: it
# prints each binding without a type, with the type GHC inferred, and exits
# 1 if there is any.
#
#   [CONTAINER] port/haskell/tests/check-annotations.sh
set -eu
here=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$here"
export HOME=${HOME:-$here/build}
out=build/annotations
mkdir -p "$out"
ghc --make -v0 -fno-code -fwrite-ide-info -isrc -outputdir "$out/obj" -hiedir "$out/hie" \
  -Wall -Werror src/Main.hs
ghc -v0 -package ghc -O0 -outputdir "$out/tool" -o "$out/binding-types" tools/BindingTypes.hs
exec "$out/binding-types" "$(ghc --print-libdir)" "$out/hie" --check
