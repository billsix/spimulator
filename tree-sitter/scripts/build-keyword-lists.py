#!/usr/bin/env python3
"""Emit scripts/keyword-lists.js from scripts/keywords.json.

Reads the JSON dump produced by extract-keywords.py and writes a
JavaScript module that grammar.js can `require()` to pull in the
per-category keyword string lists.

The output module's shape:
    module.exports = {
      ASM_DIR:   ['.alias', '.align', ...],
      R3:        ['add', 'addu', ...],
      I2:        ['addi', 'andi', ...],
      ...
    };

The category names drop the `_TYPE_INST` suffix for readability
(R3_TYPE_INST → R3) and convert PSEUDO_OP → PSEUDO, NOARG_TYPE_INST
→ NOARG, ASM_DIR stays as-is.
"""

import json
import sys


def short_name(kind):
    """Map op.h kind name to grammar.js category name."""
    if kind.endswith('_TYPE_INST'):
        return kind[: -len('_TYPE_INST')]
    if kind == 'PSEUDO_OP':
        return 'PSEUDO'
    return kind


def main():
    if len(sys.argv) != 3:
        print("usage: build-keyword-lists.py <keywords.json> <out.js>", file=sys.stderr)
        sys.exit(2)

    with open(sys.argv[1]) as f:
        data = json.load(f)

    out_lines = [
        '/* Generated from spim include/op.h via',
        ' * scripts/build-keyword-lists.py — do not edit by hand. */',
        '',
        'module.exports = {',
    ]
    for kind in data['kinds']:
        cat = short_name(kind)
        names = [e['name'] for e in data['by_kind'][kind]]
        names_lit = ', '.join(json.dumps(n) for n in names)
        out_lines.append(f'  {cat}: [{names_lit}],')
    out_lines.append('};')
    out_lines.append('')

    with open(sys.argv[2], 'w') as f:
        f.write('\n'.join(out_lines))


if __name__ == "__main__":
    main()
