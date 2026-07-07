# Audit string-equality code for a stops-short bug

**Status:** proposed — candidates located, audit not finished
**Created:** 2026-07-07

## Report (Bill, 2026-07-07)

There's suspected string-equality code that "stops short": it can report two
strings equal when one is a prefix of the other. Correct equality requires
both the same bytes *and* the same length. Find it and fix it.

## Candidate sites (initial sweep, 2026-07-07)

| Site | Implementation | Initial verdict |
|---|---|---|
| `include/spim.h:32` `streq()` | `strcmp(a,b) == 0` | correct |
| `src/symbol-table.c:85` | uses `streq` | correct |
| `src/spim.c` `str_prefix()` (REPL command dispatch) | prefix match | prefix-matching is *deliberate* there (command abbreviation), but audit every caller — a caller using it for true equality would be the bug |
| `src/scanner.c` keyword/mnemonic lookup | **not yet audited** — if it matches via `strncmp(tok, keyword, strlen(keyword))` without checking the token ends there, `addiu` could match `addi`. Prime suspect. |
| `examples .../testStringsForEquality.c:31` `str_eq` | loop exits at first mismatch; a NUL-vs-non-NUL byte *is* a mismatch, so prefixes correctly compare unequal | looks correct (note: returns 0 on equal — inverted by design, documented) |
| `examples .../head/head.{c,asm}` `str_eq` (flag check `-n`) | same loop shape, returns 1 on equal | looks correct |
| `examples .../tail/tail.{c,asm}` `str_eq` | **not yet audited** |

The three `str_eq` loops audited so far all share the shape
`while (*a == *b) { if (*a == 0) return EQ; a++; b++; } return NEQ;` — that
shape handles length correctly (when the shorter string hits NUL, NUL != the
other's next byte, so the loop exits to the not-equal path).

## Remaining work

1. Audit `src/scanner.c`'s keyword table lookup and any `strncmp`/`memcmp`
   with a computed length used as an equality test, across `src/` and
   `include/` (`grep -n 'strncmp\|memcmp\|str_prefix' src/*.c`).
2. Audit `tail.{c,asm}` and any other demo-local comparison loops
   (`grep -rn 'str_eq\|strcmp' examples/src pgu/src`).
3. Audit the pgu book's asm ports for the same pattern.
4. For each hit: fix, and add a regression (e.g. a scanner test proving `addiu`
   doesn't parse as `addi`, or a demo golden showing `"abc"` vs `"abcdef"`
   compares unequal).
5. Record here which site was the one Bill hit, once found.
