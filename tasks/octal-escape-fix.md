# Fix .asciiz octal-escape parser

## Status — landed

Single-line scanner.l fix landed 2026-05-19; regression test
`tests/tt.octal_escape.s` guards against regression.

## Bug

In `src/scanner.l`, the `copy_str` function (which decodes
`.asciiz "..."` source strings into their in-memory bytes)
mis-decoded octal escapes of the form `\ooo`.

Original code (line 493):

```c
int b = (*(str + 1) - '0') << 3;   /* WRONG */
```

The first digit was shifted left by 3 bits instead of 6 bits,
so the contribution of the high digit collapsed into the same
position as the middle digit.  Net effect: `\134` (intended
backslash, 0x5C) decoded to 0x24 (`$`).  Generally
`\abc` → `(a + b) * 8 + c` instead of `a * 64 + b * 8 + c`.

The neighboring `scan_escape` function (used for `'\ooo'`
character literals) didn't even support octal — only the
recognized named escapes and `\x..`.  So octal was unusable
in two of the three places one might expect to find it.

## Why this surfaced

`/examples/src/35-od/35-od.asm` (od -c demo) needs to print
a literal `\` character.  spim's copy_str supports `\n`,
`\t`, `\"`, and the `\NNN` octal form, but **not** `\\`
(double-backslash falls into the `default:` branch, which
emits a literal `\` followed by reinterpreting the next char
— so `\\n` becomes `\` + newline, not `\` + `n`).

So `\134` was the only available encoding.  And it was
silently wrong.

## Fix

Change `<< 3` to `<< 6` on the line shown.  Three regression
checks in `tests/tt.octal_escape.s`:

| Source     | Expected byte | Decoded char |
|------------|---------------|--------------|
| `"\134"`   | 0x5C          | `\`          |
| `"\044"`   | 0x24          | `$`          |
| `"\120"`   | 0x50          | `P`          |

## Related, not done

- `scan_escape` (used for `'\X'` char literals) still does not
  support octal.  Could be added for symmetry, but no demo
  needs it; curriculum currently uses `li $a0, 92` to load a
  backslash character literal.
- `copy_str` does not support `\\` as "literal backslash".
  Adding it would be a friendly fix but might break existing
  programs that lean on the current default-branch behavior.
  Left alone for now.
