#!/usr/bin/env bash
# Inventory every name in the C core (src/, include/) and the words they are made of.
#
# Usage, from anywhere inside the repository:
#   tasks/adhoc/spelled-out-names-c-core/discover.sh BUILDDIR
# BUILDDIR is any configured meson build directory (it needs compile_commands.json); clang and
# hunspell's en_US dictionary must be installed.
#
# Writes, under tasks/adhoc/spelled-out-names-c-core/data/:
#   names.tsv   kind, name, path, line  -- every declaration clang's AST sees (list_names.py)
#   macros.tsv  MacroDefinition, name, path, line  -- every #define (the AST does not show macros)
#   words.tsv   word, in_dictionary, name_count, use_count, kinds, example names, keyword_only
#               (split_words.py, then classify_words.py)
# glossary.tsv is written by hand from words.tsv; this script does not touch it.
set -u
status=0
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(git -C "$here" rev-parse --show-toplevel)
builddir=$(cd "$1" && pwd)
cd "$root" || exit 1

python3 "$here/list_names.py" "$builddir" > "$here/data/names.tsv" || status=1

# Every #define in the core, with its location. Include guards (NAME_H) are listed too;
# split_words.py drops them.
git grep -n -E '^\s*#\s*define\s+[A-Za-z_][A-Za-z0-9_]*' -- src include \
  | sed -E 's/^([^:]+):([0-9]+):\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*).*/MacroDefinition\t\3\t\1\t\2/' \
  > "$here/data/macros.tsv" || status=1

python3 "$here/split_words.py" "$here/data/names.tsv" "$here/data/macros.tsv" \
  > "$here/data/words.unclassified.tsv" || status=1
python3 "$here/classify_words.py" "$here/data/words.unclassified.tsv" \
  "$here/data/names.tsv" "$here/data/macros.tsv" > "$here/data/words.tsv" || status=1
rm -f "$here/data/words.unclassified.tsv"
exit $status
