#!/usr/bin/env bash
# Prove a rename batch changed no machine code: compare each core object file's .text and
# .rodata bytes with a saved baseline.
#
# Usage, from anywhere inside the repository:
#   tasks/adhoc/spelled-out-names-c-core/compare_code.sh BUILDDIR BASELINE_DIR save
#   tasks/adhoc/spelled-out-names-c-core/compare_code.sh BUILDDIR BASELINE_DIR check
# "save" writes the baseline from the current build; "check" compares against it and exits 1
# on any difference. BUILDDIR must already be compiled (meson compile -C BUILDDIR).
#
# A pure rename leaves .text identical (symbol names live in the symbol table and debug info,
# and calls are relocations). .rodata changes only where a name reaches a string: __func__, or
# an assert's stringified expression; the core uses neither, so it must match too.
set -u
builddir=$1
baseline=$2
mode=$3
status=0
mkdir -p "$baseline"
while IFS= read -r object; do
  name=$(basename "$object")
  for section in .text .rodata; do
    current=$(mktemp)
    objcopy -O binary --only-section="$section" "$object" "$current" || status=1
    case $mode in
      save) mv "$current" "$baseline/$name$section" ;;
      check)
        if ! cmp -s "$current" "$baseline/$name$section"; then
          echo "differs: $name $section"
          status=1
        fi
        rm -f "$current"
        ;;
      *) echo "usage: $0 BUILDDIR BASELINE_DIR save|check" >&2; exit 2 ;;
    esac
  done
done < <(find "$builddir" -name 'src_*.c.o' | sort)
exit $status
