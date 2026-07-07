# Audit string-equality code in the examples for a stops-short bug

**Status:** proposed — candidates located, audit not finished
**Created:** 2026-07-07
**Scope note (Bill, 2026-07-07):** the suspected bug is in the **example
code** (`examples/src`, possibly `pgu/src`), not in spimulator itself. Audit
the demos' comparison routines first; the simulator sites below are checked
and fine.

## Report (Bill, 2026-07-07)

String-equality code somewhere "stops short": it can report two strings equal
without requiring both the same bytes *and* the same length (a prefix passing
as equal). Find it and fix it.

## Example-code sites (the real scope)

| Site | Status |
|---|---|
| `examples/src/extras/testStringsForEquality/testStringsForEquality.{c,-1.asm}` | C audited — the `while (*a == *b) { if (*a == 0) ... }` loop handles length correctly. **The .asm port still needs a careful read.** |
| `examples/src/transforms/head/head.{c,asm}` `str_eq` (the `-n` flag check) | both audited — same correct loop shape |
| `examples/src/transforms/tail/tail.{c,asm}` | **not yet audited** |
| every other demo with a comparison loop — `comm`, `cp`, `factor`, `seq`, `base64`, `od`, the recursion/algorithms sets | **not yet audited** — inventory via `grep -rn 'str_eq\|strcmp\|streq' examples/src pgu/src` |
| `pgu/src` asm ports (book code) | **not yet audited** |

Watch specifically for the buggy shapes: `strncmp(a, b, strlen(a)) == 0`
(prefix passes), a loop that exits on `*a == 0` *before* comparing that byte
against `*b`, or an asm loop whose `beq $t0, $zero, equal` sits before the
`bne $t0, $t1, differ` (checking end-of-string before checking mismatch —
declares "equal" when only the *first* string ended).

## Simulator sites (audited 2026-07-07 — all fine, kept for the record)

- `include/spim.h:32` `streq()` = `strcmp()==0` — correct;
  `src/symbol-table.c` uses it — correct.
- `src/spim.c` `str_prefix()` REPL command dispatch — prefix matching is
  deliberate there (command abbreviation).
- `src/scanner.c` keyword lookup — was the initial suspect, but per Bill the
  bug is in the examples; only re-check if the example audit comes up empty.

## Remaining work

1. Grep-inventory every comparison routine across `examples/src` and
   `pgu/src` (C and asm), audit each against the buggy shapes above.
2. Fix the offender(s); update the paired C and asm together. (The embedded-C
   comment blocks in the .asm headers were removed 2026-07-07 —
   `archive/2026/07/07/c-asm-comment-parity.md` — so there's nothing extra to
   keep in sync.)
3. Add a demo-level regression: a comparison case where one string is a
   prefix of the other (`"abc"` vs `"abcdef"`) pinned in the golden output.
4. Record here which site Bill actually hit, once found.
