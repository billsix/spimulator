#!/bin/bash
# One round of the annotate loop: type-check the Haskell port (-fno-code),
# then let the three fixers act on GHC's errors (add the forall an outer type
# variable needs, add missing imports or qualifiers, add missing class
# constraints).  Run it until it reports no errors; whatever the fixers
# cannot settle is printed for a hand edit.
#
# Usage: typecheck-loop.sh SCRATCH   (SCRATCH holds GHC's output and log)
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
port=$(cd "$here/../../../port/haskell" && pwd)
scratch=${1:?usage: typecheck-loop.sh SCRATCH}
mkdir -p "$scratch"
cd "$port"
ghc --make -fno-code -isrc -outputdir "$scratch/tc" -Wall -Werror -fmax-errors=0 src/Main.hs \
  > "$scratch/tc.log" 2>&1 < /dev/null
echo "rc=$? errors=$(grep -cE '^src/.*error' "$scratch/tc.log")"
python3 "$here/scope_foralls.py" "$scratch/tc.log"
python3 "$here/add_imports.py" "$scratch/tc.log"
python3 "$here/add_constraints.py" "$scratch/tc.log"
