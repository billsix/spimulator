#!/usr/bin/env bash
# Same as buildDebug.sh, but emits a Code::Blocks project file too.

set -euo pipefail

mkdir -p build buildInstall
cd build

cmake -DCMAKE_INSTALL_PREFIX=../buildInstall -DCMAKE_BUILD_TYPE=Debug \
      -G "CodeBlocks - Unix Makefiles" ../
cmake --build . --target all
cmake --build . --target install

codeblocks build/examples.cbp
