#!/bin/env bash
set -euo pipefail

cd /spimulator/

if [ -d builddir ]; then
    CC=clang CXX=clang++ meson setup --reconfigure builddir --buildtype=debug -Dwarning_level=3
else
    CC=clang CXX=clang++ meson setup builddir --buildtype=debug -Dwarning_level=3
fi

meson compile -C builddir
meson install -C builddir

ln -sf builddir/compile_commands.json compile_commands.json
