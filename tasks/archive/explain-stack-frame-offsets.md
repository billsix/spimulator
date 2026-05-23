# Expand base+offset offset diversity in tt.explain.s

## Goal

Cover the canonical stack-frame walk (`sw $a0, 4($sp); sw $a1,
8($sp); sw $a2, 12($sp); sw $ra, 16($sp)` and the matching loads)
and negative offsets (`-4($fp)`-style locals) in
`tests/tt.explain.s`.  Both shapes show up in essentially every
real MIPS function and neither is exercised today.

## Why

Bill observed that `tt.explain.s` shows `lw`/`sw` with offset 0
and one isolated offset-4 case, but nothing that looks like the
multi-slot save/restore pattern a student will see the moment
they open any real .asm file.  The narration template already
prints `Effective address = $rs + N = 0xXXX + N = 0xYYY` for
each — the gap is purely coverage in the test file, so adding
the cases is the lowest-risk way to surface the existing
narration on the patterns that matter.

Negative offsets are pedagogically distinct because the
immediate is sign-extended, so `-4($sp)` decodes as
`0xfffffffc + $sp`.  Worth showing the arithmetic once.

## State of the world

`src/explain.c`:

- `tpl_load`  (line ~698) renders
  `Effective address = $%s + %d  =  0x%08x + %d  =  0x%08x.`
- `tpl_store` (line ~737) renders the same line for stores.
- Both are wired into all 8 integer families:
  `TOK_LW_OPCODE` (1873), `TOK_LB_OPCODE` (1876),
  `TOK_LBU_OPCODE` (1880), `TOK_LH_OPCODE` (1884),
  `TOK_LHU_OPCODE` (1888), `TOK_SW_OPCODE` (1894),
  `TOK_SB_OPCODE` (1897), `TOK_SH_OPCODE` (1900).

`tests/tt.explain.s` — base+offset usage today:

| Line | Instruction          | Offset |
|------|---------------------|--------|
| 51   | `lw  $t7, 0($t6)`   | 0      |
| 52   | `lb  $t8, 0($t6)`   | 0      |
| 54   | `sw  $t7, 4($sp)`   | +4     |
| 55   | `lw  $t9, 4($sp)`   | +4     |
| 88   | `sb  $t8, 0($t7)`   | 0      |
| 93   | `sh  $t8, 2($t7)`   | +2     |
| 148  | `lb  $s6, 0($t0)`   | 0      |
| 149  | `lbu $s7, 0($t0)`   | 0      |
| 150  | `lh  $t2, 2($t0)`   | +2     |
| 151  | `lhu $t3, 2($t0)`   | +2     |

No multi-slot stack-frame walk; no negative offsets.

## The change

Append two focused blocks to `tests/tt.explain.s`:

**Block A — stack-frame save/restore idiom.**  Allocate a small
frame, write four consecutive slots, read them back.  Exercises
the same template at four distinct positive offsets in sequence
so the `Effective address = $sp + N` arithmetic varies in a way
a student can pattern-match.

```asm
        # ── Block A: canonical stack-frame save/restore ──
        addiu   $sp, $sp, -20      # 5-slot frame
        sw      $a0,  4($sp)       # save arg 0
        sw      $a1,  8($sp)       # save arg 1
        sw      $a2, 12($sp)       # save arg 2
        sw      $ra, 16($sp)       # save return address
        lw      $a0,  4($sp)       # restore arg 0
        lw      $a1,  8($sp)       # restore arg 1
        lw      $a2, 12($sp)       # restore arg 2
        lw      $ra, 16($sp)       # restore return address
        addiu   $sp, $sp, 20       # tear down frame
```

**Block B — negative-offset access via $fp.**  Sets `$fp` to
`$sp`, then accesses locals at `-4($fp)` / `-8($fp)`.  Shows the
sign-extended immediate path through the same template.

```asm
        # ── Block B: negative-offset local-variable access ──
        addiu   $sp, $sp, -8       # 2-word locals area
        move    $fp, $sp
        li      $t0, 111
        sw      $t0, -4($fp)       # store local 0
        li      $t1, 222
        sw      $t1, -8($fp)       # store local 1
        lw      $t2, -4($fp)       # load local 0
        lw      $t3, -8($fp)       # load local 1
        addiu   $sp, $sp, 8        # tear down
```

Place both blocks near the existing load/store exercises so the
golden diff is localized.

## Sign-extension spot check

Before regenerating the golden file, eyeball one negative-offset
emission to confirm `tpl_load` / `tpl_store` render the immediate
sensibly.  Two acceptable shapes:

- `Effective address = $fp + -4  =  0x7fffffd8 + -4  =  0x7fffffd4.`
- `Effective address = $fp + 0xfffffffc  =  ...`

The format string is `%d`, so the first shape is what will
appear — readable.  No code change needed unless the L3 hex/bin
table or L4 progressive decoder display the immediate field
ambiguously for negative values; if so, file a small follow-up.

## Verification

1. `ninja -C builddir` clean.
2. Run by hand:
   `./builddir/spimulator -exception_file src/exceptions.s -explain=2 -f tests/tt.explain.s | less`
   — scroll to the two new blocks; confirm each line prints
   `Effective address = $rs + N = 0xXXX + N = 0xYYY` with the
   right values, and that the negative-offset arithmetic is
   right.
3. Regenerate the golden:
   `./builddir/spimulator -exception_file src/exceptions.s -explain=2 -f tests/tt.explain.s > tests/tt.explain.expected`
4. `meson test -C builddir`  → 22/22 green.
5. Re-skim the diff in `tests/tt.explain.expected` to confirm
   only the new blocks (plus their L3 hex/bin table and L4
   decoder narration) appear in the change.

## Out of scope

- Coverage for lwl/lwr/swl/swr, ll/sc, lwc1/swc1/ldc1/sdc1 —
  see [`explain-missing-load-store-families.md`](explain-missing-load-store-families.md).
  Those need explain.c template work, not just test additions.
- New addressing-mode narration features (e.g. role-tagging
  `$sp`/`$fp` as "stack pointer" in the prose).  Separate
  polish.

## Status

Landed 2026-05-23.  Two blocks appended to `tests/tt.explain.s`
after the existing base+offset section (around line 56):

- 4-slot stack-frame save/restore at $sp+4/+8/+12/+16
- Negative-offset locals at -4($fp) / -8($fp)

Golden file regenerated: 3373 → 3940 lines (+567).  22/22 meson
tests green.  Templates rendered correctly without code change:
the `tpl_load` / `tpl_store` "Effective address" line works for
positive and (sign-extended) negative immediates alike.

### Cosmetic fixes folded in (same session)

Surfaced by the negative-offset block; small enough to fix
inline.

- **`s8` → `fp` in `int_reg_names[]`** (`src/display-utils.c:19`).
  Register 30's canonical display name is now `$fp`.  The
  scanner already accepted both `fp` and `s8` as input, so this
  is a display-only change.  All disassembly, narration, and
  `print` hints now consistently say `$fp`.
- **`+ -N` → `- N` for negative offsets** in `tpl_load` /
  `tpl_store` (`src/explain.c` around lines 691/735).  Sign of
  `off` is extracted once; the effective-address format string
  uses `%s %d` for the separator+magnitude so it reads
  `$fp - 4 = 0x7fffffdc - 4 = 0x7fffffd8` instead of
  `$fp + -4 = 0x7fffffdc + -4 = 0x7fffffd8`.  Other places that
  echo the raw immediate (`offset = -4 (0xfffc)`, the `print
  -4($fp)` REPL hint) were left as-is — they're showing the
  literal field / the syntax the student wrote, both correct.

Golden re-regenerated after these fixes; still 22/22 green.
