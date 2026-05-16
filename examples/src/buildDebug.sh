#!/usr/bin/env bash
# Build all demos in a freestanding (-nostdlib) configuration via os.h.
#
# No musl, no libc — each demo defines its own _start and reaches the
# kernel through inline-asm syscalls.  Plain clang or gcc on the host
# toolchain is enough.

set -euo pipefail

mkdir -p build buildInstall
cd build

cmake -DCMAKE_INSTALL_PREFIX=../buildInstall -DCMAKE_BUILD_TYPE=Debug ../
cmake --build . --target all
cmake --build . --target install
