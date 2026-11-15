#!/bin/bash
# Fail unless every compiled port's binary exists and is no older than its
# sources.
#
#   [CONTAINER] port/check-binaries-fresh.sh
#
# The launchers (port/haskell/spimulator-haskell, port/rust/spimulator-rust)
# build their binary on first use when it is missing or stale.  The image
# build must not leave that to a test: a test that runs under `timeout 10`
# would then time the compiler, and fail whenever the build is slow (a loaded
# or many-core machine).  The Dockerfile builds both binaries in their own
# layer, and runs this script before `meson test` so a missing or broken
# build step fails every image build with a message, not some of them with a
# timeout.
#
# Each check is the same test the launcher's `stale` function makes; keep the
# two in sync.
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
status=0

# check NAME BINARY SOURCE... : BINARY must be executable and no SOURCE file
# may be newer than it.
check() {
  local name=$1 binary=$2
  shift 2
  if [ ! -x "$binary" ]; then
    echo "check-binaries-fresh: the $name port binary $binary is missing;" \
      "the Dockerfile's port-build layer should have built it" >&2
    status=1
    return
  fi
  local newer
  newer=$(find "$@" -newer "$binary" -print -quit 2>/dev/null)
  if [ -n "$newer" ]; then
    echo "check-binaries-fresh: the $name port binary $binary is older than" \
      "$newer; the Dockerfile's port-build layer must run after the port sources are copied" >&2
    status=1
  fi
}

check haskell "$here/haskell/build/spimulator-haskell" "$here/haskell/src" -name '*.hs'
check rust "$here/rust/target/release/spimulator-rust" "$here/rust/src" "$here/rust/Cargo.toml"

exit $status
