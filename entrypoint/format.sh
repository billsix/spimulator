#!/bin/env bash

cd /spimulator || exit 1

# pgu/upstreamSource/ holds third-party sources kept byte-identical to
# upstream -- never reformat them (Bill, 2026-07-29).
find . -path ./pgu/upstreamSource -prune -o \( -iname "*.c" -o -iname "*.cpp" -o -iname "*.h" -o -iname "*.hpp" \) -print0 | xargs -0 clang-format -i
