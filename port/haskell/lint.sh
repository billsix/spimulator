#!/usr/bin/env bash
# hlint over the Haskell port, advisory: it prints its hints and always exits 0
# (William Emerison Six <billsix@gmail.com>, 2026-11-11; the Haskell standard
# treats hlint as advice unless a project gates on it).  The compiler tier is
# already a gate: build.sh compiles with -Wall -Werror.  The meson test
# port-haskell-hlint runs it; read its output in the meson test log.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
hlint "$here/src" "$here/tools" || true
exit 0
