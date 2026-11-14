#!/usr/bin/env bash
# The names the Haskell port's imported libraries export, one per line, into
# tasks/adhoc/spelled-out-names-python-port/data/haskell.external.txt. port_names.py treats
# every other identifier of port/haskell/src and port/haskell/tests as the port's own.
#
# Usage, from the repository root: tasks/adhoc/spelled-out-names-python-port/haskell_external.sh
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(git -C "$here" rev-parse --show-toplevel)
script=$(mktemp)
modules=$(git -C "$root" ls-files 'port/haskell/src/*.hs' 'port/haskell/tests/*.hs' |
  (cd "$root" && xargs grep -h -E '^import ') |
  sed -E 's/^import +(qualified +)?([A-Za-z0-9.]+).*/\2/' | sort -u)
own=" $(git -C "$root" ls-files 'port/haskell/*.hs' | sed 's|.*/||; s|\.hs$||' | tr '\n' ' ') "
{
  echo ":browse Prelude"
  for module in $modules; do
    case "$own" in
      *" $module "*) ;;
      *) echo ":browse $module" ;;
    esac
  done
  echo ":quit"
} > "$script"
ghci -v0 -ignore-dot-ghci -package containers -package mtl -package array -package haskeline \
  -package bytestring -package directory -package filepath -package unix < "$script" |
  grep -o -E "[A-Za-z_][A-Za-z0-9_']*" | sort -u > "$here/data/haskell.external.txt"
status=${PIPESTATUS[0]}
rm -f "$script"
exit "$status"
