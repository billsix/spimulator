#!/usr/bin/env bash
# For every teaching .c under examples/src (not the io helpers, the three
# lib sources, or the template), print: path, the helper sources its
# undefined symbols resolve to (transitively), whether it includes a lib
# header, and whether the header already shows a native or spimulator line.
# Writes data/inventory.tsv. Run from anywhere; needs gcc and nm.
set -u
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
src=$root/examples/src
out=$(cd "$(dirname "$0")" && pwd)/data/inventory.tsv
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$(dirname "$out")"
cd "$src"
helpers=$(ls *.c; echo lib/libctype/libctype.c lib/libstdlib/libstdlib.c lib/libstr/libstr.c)
inc="-I. -Ilib/libctype -Ilib/libstdlib -Ilib/libstr"
declare -A def
for h in $helpers; do
  gcc -std=gnu23 -O0 -fno-stack-protector $inc -c "$h" -o "$tmp/h.o"
  for s in $(nm --defined-only "$tmp/h.o" | awk '$2 ~ /[TDRB]/ {print $3}'); do def[$s]=$h; done
  nm -u "$tmp/h.o" | awk '{print $2}' > "$tmp/$(echo "$h" | tr / _).undef"
done
printf 'demo\thelpers\tnative_line\tspim_line\n' > "$out"
find . -name '*.c' -not -path './lib/libctype/*' -not -path './lib/libstdlib/*' \
     -not -path './lib/libstr/*' -not -path './template/*' | sed 's|^\./||' | sort | while read -r c; do
  case " $(echo $helpers) " in *" $c "*) continue ;; esac
  gcc -std=gnu23 -O0 -fno-stack-protector $inc -c "$c" -o "$tmp/d.o"
  queue=$(nm -u "$tmp/d.o" | awk '{print $2}'); need=""
  while [ -n "$queue" ]; do
    next=""
    for s in $queue; do
      h=${def[$s]:-}; [ -z "$h" ] && continue
      case " $need " in *" $h "*) continue ;; esac
      need="$need $h"; next="$next $(cat "$tmp/$(echo "$h" | tr / _).undef")"
    done
    queue=$next
  done
  nat=$(grep -c 'gcc \|cc -std' "$c"); sp=$(grep -c 'spimulator ' "$c")
  printf '%s\t%s\t%s\t%s\n' "$c" "$(echo $need)" "$nat" "$sp" >> "$out"
done
echo "wrote $out"
