#!/bin/bash
# Build the Haskell port of the spimulator engine with GHC's own libraries
# only, offline: build/spimulator-haskell.
#
#   [CONTAINER] port/haskell/build.sh
#
# Plain `ghc --make`, not cabal: cabal wants a Hackage package index even
# when every dependency ships with GHC, and the image has none
# (tasks/engine-port-haskell.md, Log).  The launcher spimulator-haskell runs
# this when the binary is missing or older than a source file.
set -eu
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "$here"
mkdir -p build
export HOME=${HOME:-$here/build}
exec ghc --make -O1 -isrc -outputdir build/obj -o build/spimulator-haskell \
  -Wall -Werror -rtsopts=ignoreAll src/Main.hs
