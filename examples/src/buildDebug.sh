#!/usr/bin/env bash
# Build all demos via meson, mirroring how /spimulator builds.
# Each demo is freestanding (-nostdlib) and reaches the kernel
# through the inline-asm syscall wrappers in os.h.

set -euo pipefail
cd "$(dirname "$0")"

if [ -d builddir ]; then
    meson setup --reconfigure builddir --buildtype=debug
else
    meson setup builddir --buildtype=debug
fi

meson compile -C builddir

# For IDE tooling — clangd, vscode, etc. — surface the
# compile-commands at the project root.
ln -sf builddir/compile_commands.json compile_commands.json
