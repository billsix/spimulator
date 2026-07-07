# Audit string-equality code in the examples for a stops-short bug

**Status:** audit COMPLETE 2026-07-07 — **no stops-short bug found**; open
pending Bill's pointer to what he observed (question at bottom).
**Created:** 2026-07-07
**Scope note (Bill):** the suspected bug is in the **example code**, not in
spimulator itself.

## Report (Bill, 2026-07-07)

String-equality code somewhere "stops short": it can report two strings equal
without requiring both the same bytes *and* the same length (a prefix passing
as equal).

## Audit results (2026-07-07) — every comparison routine, all clean

The buggy shapes looked for: `strncmp(a,b,strlen(a))`-style prefix equality;
a loop that tests `*a == 0` *before* comparing against `*b`; an asm loop
whose `beq $tX, $zero, equal` precedes the `bne $tX, $tY, differ`; a
length-counted compare missing the length check.

| Site | Verdict |
|---|---|
| `testStringsForEquality.{c,-1.asm}` `str_eq`/`streq` | **correct** both sides (`bne` differ-check precedes the NUL check). NB: returns **0 on equal / 1 on differ** — deliberately inverted, documented in the header. |
| `transforms/head/head.{c,asm}` `str_eq` (`-n` flag) | correct both sides (returns 1 on equal) |
| `transforms/tail/tail.{c,asm}` `str_eq` | correct both sides |
| `fileio/comm/comm.{c,asm}` `line_cmp` | correct both sides (strcmp shape; NUL handled after differ-check) |
| `transforms/uniq/uniq.{c,asm}` `lines_equal`/`emit_if_new` | correct both sides — the length-counted case; **both check `prevLen == curLen` before comparing bytes** |
| every other demo (`seq`, `factor`, `cp`, `touch`, `base64`, `nl`, `od`, `cut`, `cat`, recursion/algorithms sets) | no string-equality code (grep-swept for comparison loops; hits were comments or numeric compares) |
| `pgu/` (book + asm ports) | no string-equality listings (swept `src/`, `upstreamSource/`, `docs/source/*.rst`) |
| simulator: `streq` (spim.h), symbol table, `map_string_to_name_val_val` (the shared register/keyword lookup, spim-utils.c:414) | correct — the lookup's match test requires **both** strings at NUL; prefix in either direction is a miss. `str_prefix` in the REPL is prefix-matching **by design** (command abbreviation). |

## Question for Bill

No current code exhibits the described bug. Three possibilities:

1. **The inverted convention read as a bug**: `testStringsForEquality`
   returns 0 for equal / 1 for differ (inherited from the book demo it came
   from, noted in its header). If this is what you saw, the fix is a naming/
   convention decision, not a logic fix — say the word and I'll flip it (and
   the demo's golden) to the intuitive 1-equal convention, or rename it
   `str_cmp`-style so the 0 reads naturally.
2. **Already fixed**: you saw an older revision.
3. **Somewhere I didn't look**: if you can name the demo (or the behavior
   you observed — inputs and wrong output), I'll pin it directly.
