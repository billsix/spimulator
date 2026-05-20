#!/usr/bin/env python3
"""Extract keyword surface from spim's include/op.h into JSON.

Each OP(NAME, TOKEN, TYPE, ENCODING) entry becomes one record.  Output
groups keywords by category so grammar.js can build choice() rules
without re-walking the file.

Usage:
    extract-keywords.py /path/to/spim/include/op.h > keywords.json
"""

import json
import re
import sys

# OP("name", TOK_FOO, KIND, encoding).  KIND may be mixed-case
# (e.g. FP_R2ds_TYPE_INST).  Some entries span two source lines.
# Match across line breaks by reading the full file as one buffer.
OP_RE = re.compile(
    r'OP\("([^"]+)",\s*(TOK_[A-Z_0-9]+),\s*([A-Za-z_][A-Za-z_0-9]*),'
    r'\s*(-?\d+|0x[0-9a-fA-F]+)\)',
    re.MULTILINE,
)


def main():
    if len(sys.argv) != 2:
        print("usage: extract-keywords.py <op.h>", file=sys.stderr)
        sys.exit(2)

    by_kind = {}
    all_keywords = []

    with open(sys.argv[1]) as f:
        text = f.read()

    for m in OP_RE.finditer(text):
        name, token, kind, encoding = m.groups()
        # Compute line number of the match start for traceability.
        lineno = text.count('\n', 0, m.start()) + 1
        entry = {
            "name": name,
            "token": token,
            "kind": kind,
            "encoding": encoding,
            "line": lineno,
        }
        by_kind.setdefault(kind, []).append(entry)
        all_keywords.append(entry)

    # Sort each kind by name for deterministic output.
    for kind in by_kind:
        by_kind[kind].sort(key=lambda e: e["name"])
    all_keywords.sort(key=lambda e: e["name"])

    output = {
        "all": all_keywords,
        "by_kind": by_kind,
        "kinds": sorted(by_kind.keys()),
        "count": len(all_keywords),
    }
    json.dump(output, sys.stdout, indent=2)
    print()


if __name__ == "__main__":
    main()
