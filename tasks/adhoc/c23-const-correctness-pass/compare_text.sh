#!/usr/bin/env bash
# Compare the machine code of the spimulator objects in two -O2 builds.
#
# Usage: compare_text.sh BASE_BUILDDIR NEW_BUILDDIR
#
# NEW_BUILDDIR is rebuilt first. For each object, the .text section is
# extracted with objcopy and compared byte for byte; relocation targets are
# not part of the section bytes, so a table that moved from .data to
# .rodata does not show. A `const` must not change any function's code, so
# every object should print "same".
set -u
base=$1
new=$2
meson compile -C "$new" spimulator >/dev/null || exit 1
status=0
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
for object in "$base"/spimulator.p/*.o; do
  name=$(basename "$object")
  objcopy -O binary --only-section=.text "$object" "$scratch/base.bin"
  objcopy -O binary --only-section=.text "$new/spimulator.p/$name" "$scratch/new.bin"
  if cmp -s "$scratch/base.bin" "$scratch/new.bin"; then
    echo "same     $name"
  else
    echo "DIFFERS  $name"
    status=1
  fi
done
exit $status
