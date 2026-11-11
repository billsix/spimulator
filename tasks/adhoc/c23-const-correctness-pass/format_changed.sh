#!/usr/bin/env bash
# clang-format only the lines changed since a commit, in src/ and include/.
#
# Usage (from the repo root): format_changed.sh [BASE]   (default HEAD)
#
# The tree has formatting differences that predate this pass, so the whole
# files are not reformatted: each changed hunk's new line range is passed to
# clang-format --lines.
set -u
base=${1:-HEAD}
status=0
for file in $(git diff --name-only "$base" -- src include | grep -E '\.[ch]$'); do
  ranges=$(git diff -U0 "$base" -- "$file" |
    sed -nE 's/^@@ -[0-9,]+ \+([0-9]+)(,([0-9]+))? @@.*/\1 \3/p' |
    while read -r start count; do
      count=${count:-1}
      [ "$count" -eq 0 ] && continue
      printf -- '--lines=%d:%d ' "$start" $((start + count - 1))
    done)
  [ -n "$ranges" ] || continue
  # shellcheck disable=SC2086  # the ranges are separate arguments
  clang-format -i $ranges "$file" || status=1
done
exit $status
