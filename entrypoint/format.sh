#!/bin/env bash

cd /spimulator

find . \( -iname "*.c" -o -iname "*.cpp" -o -iname "*.h" -o -iname "*.hpp" \) -print0 | xargs -0 clang-format -i
