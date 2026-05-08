# Re-entry notes — teaching mode

Quick orientation for the next session. Detailed history lives in
`teaching-mode.md` (original spec + bug list) and
`teaching-mode-coverage.md` (the audit, what's done, what's open).

## What landed in the most recent session (encoding/pedagogy polish)

Five connected changes that overhauled how each instruction is presented:

1. **ENCODING was always 0** — root-caused to a `#ifndef OP_H` guard in
   `include/op.h` wrapping its X-macro entries. `inst.c` includes
   `op.h` three times to build three tables, but only the first
   inclusion expanded. Fix: split the guard to cover only the
   type-macro definitions (renamed `OP_H_TYPES`); leave the `OP()`
   list outside. After fix, `inst_encode()` works, every disassembly
   line shows the real binary word.
2. **Bit-layout ASCII diagram** for every instruction (R/I/J formats).
3. **Mnemonic-decoding annotation** under the diagram: "opcode=0x00
   (SPECIAL) + funct=0x21 selects `addu`", etc.
4. **Consistent register vocabulary**: disassembler now uses ABI
   names (`sw $t2, 4($at)` not `sw $10, 4($1)`), and the "What it
   does" line substitutes `$rs`/`$rt`/`$rd` placeholders with the
   concrete register names of the current instance.
5. Helpers added: `inst_op_name()` in `src/inst.c`, `subst_field_regs()`
   and `write_bits()` / three `render_*_layout()` in `src/explain.c`.

A typical instruction's output is now: disassembly line (ABI names) →
bit-layout box (binary + bit positions + field decode + mnemonic
annotation) → "What it does" (concrete register names) → Inputs/Will
write/Try it yourself → after-step diff.

## Build + run

The fedora container has `meson`, `flex`, `bison`, `gcc` pre-installed
(verified at session end). `builddir` sometimes disappears between
sessions — if `ninja -C builddir` errors with "chdir to 'builddir' - No
such file or directory", just run `meson setup builddir` from the repo
root first.

```sh
cd /spimulator
meson setup builddir          # only if builddir is missing
ninja -C builddir
./builddir/spimulator -exception_file src/exceptions.s -explain -f tests/tt.explain.s
```

The `-exception_file src/exceptions.s` is required because the binary
isn't installed at `/usr/local/share/spimulator/`. Without it the run
errors with `Cannot open file: /usr/local/share/spimulator/exceptions.s`.

A clean end-to-end run currently emits **3901 lines** of explain output,
exits 0, with zero `(no detailed explanation)` fallthroughs. Use that
count as a quick smoke signal — substantial drift up or down means
something changed.

## State of play

**Done (this session and prior):**
- MVP teaching mode (`-explain` / `-x` / `-noexplain` flags)
- 45+ integer opcodes narrated: arithmetic, logical R-type and immediate,
  shifts (signed and unsigned), set-less-than family, all 5 load widths
  and all 3 store widths, 2-reg and zero-compare branches,
  jumps/calls/returns (j, jal, jalr, jr), mult/div signed and unsigned,
  HI/LO transfer, lui, syscall (1, 4, 5, 8, 10, 11 with $v0-aware text)
- Pseudo-op narration for 40 integer pseudo-ops via a table in
  `src/explain.c` + continuation tracking across multi-instruction
  expansions (la, li with large constants, comparison-branches, etc.)
- `print N($reg)` REPL syntax — `sw $t7, 4($sp)` in the source means
  the student can type `print 4($sp)` to inspect that memory
- Three pedagogical bug fixes: jal/jalr PC+4-vs-PC+8 prediction, silent
  loads in the post-step diff, shift instructions mis-rendered as `nop`
- Smoke test (`tests/tt.explain.s`) covering every category of the
  above end-to-end, ~220 lines
- **ENCODING-zero root-cause fix** (`include/op.h`): the X-macro file
  had an `#ifndef OP_H` guard wrapping the `OP(...)` entries, so the
  second and third `#include "op.h"` in `inst.c` (for `i_opcode_tbl`
  and `a_opcode_tbl`) silently expanded to nothing. `inst_encode()`
  therefore failed every lookup and returned 0, leaving `ENCODING(inst)`
  zero for all source-assembled instructions. Fix: scope the guard to
  just the type-macro definitions (renamed `OP_H_TYPES`) and leave the
  `OP()` list outside any guard, so it can be re-included. After the
  fix, `format_an_inst` prints the real binary word (e.g.
  `0x01095020  add $10, $8, $9`).
- **Bit-layout ASCII diagram** in `explain.c` (`explain_bit_layout`,
  `render_r_layout`, `render_i_layout`, `render_j_layout`,
  `write_bits` helper). Each instruction now shows a labeled
  bit-field diagram between the disassembly line and "What it does"
  — opcode/rs/rt/rd/shamt/funct for R-type, opcode/rs/rt/imm for
  I-type, opcode/target for J-type. Classification is by top-6
  opcode field; coprocessor formats render as I-type. `nop`
  (encoding 0) skips the diagram. I-type immediate shows signed
  decimal and unsigned hex side-by-side so sign-extension is visible.
  J-type shows the computed jump address
  `(PC[31:28] | target<<2)`. Smoke output grew from 2814 to ~3724
  lines; still exit 0, still zero "no detailed explanation"
  fallthroughs.
- **ABI register names in the disassembler** (`src/inst.c`
  `format_an_inst`). Previously every disassembled line printed raw
  register numbers (`sw $10, 4($1)`) while the source comment used
  ABI names (`sw $t2, 4($at)`) — two different vocabularies in the
  same line. Replaced all integer `$%d` register slots in the
  per-type print cases with `$%s` using `int_reg_names[]` (B1,
  I1s, I1t, I2, B2, I2a, R1s, R1d, R2td, R2st, R2ds, R2sh, R3,
  R3sh, FP_I2a base register, FP_R2ts integer half,
  MOVC_TYPE_INST, plus the addr-expr formatter's `($N)` slot).
  FP register slots (`$f%d`) kept as-is. Now `sw $t2, 4($at)`
  throughout.
- **Mnemonic-decoding annotation** under each bit-layout diagram.
  After the field values, a short "->" line tells the student which
  bit pattern picked this mnemonic. For R-type: "opcode=0x00
  (SPECIAL) tells the CPU to look at funct; funct=0x21 selects the
  `addu` instruction." For I-type and J-type: "opcode=0xNN selects
  the `<mnemonic>` instruction." Requires a small helper
  `inst_op_name(instruction*)` added to `src/inst.c` (exposed in
  `include/inst.h`) that returns the canonical mnemonic via the
  existing `name_tbl` lookup.
- **Concrete-register substitution in template text** —
  `subst_field_regs()` in `explain.c` rewrites the generic
  placeholders `$rs`/`$rt`/`$rd` inside `op_text` arguments into
  the actual register names used by the current instance. Wired
  into `tpl_r3_arith` (rs/rt/rd), `tpl_i2_arith` (rs/rt), and
  `tpl_shift` (rt/rd). This makes each "What it does" line use
  one consistent vocabulary — e.g. `compute $a2 + $v0` instead of
  the previous mixed-vocabulary `compute $rs + $rt with Result is
  placed in $a2`. Call sites in the opcode switch keep their
  generic strings (concise, readable at the source); the
  substitution happens at render time. The `pseudo_ops[]` table
  intentionally keeps `$rs`/`$rt`/`$rd` un-substituted, since
  those entries describe the generic pseudo-op pattern, not a
  specific instance. Added `#include <ctype.h>` for `isalnum`.

**Open / declared stretch:**
- FP family (~70 opcodes Y_*_S_OP / Y_*_D_OP / c.*) — Gap 1 P8. Biggest
  defensible add for an H&P-aligned course; would benefit from a
  table-driven approach (Option B in `teaching-mode.md`).
- Runtime REPL toggle (`explain` / `noexplain` commands) — needs
  scanner.l / parser.y changes (flex/bison regen).
- Golden expected-output file (`tests/tt.explain.expected`) — easy
  regression test now that the format has stabilized.
- ABI register **role** annotations ("$a0 (argument register)", "$ra
  (return address)", "$s0..$s7 (callee-saved)") in the per-template
  prose. **Note**: the disassembly half of the line and the
  `int_reg_names[]`-based register printing already use ABI **names**
  ($a0, $sp, etc.). What's still missing is the *role tag* in
  parentheses on the "Inputs read" / "Will write" lines — a separate
  small table keyed by register number.
- Syscall string dump — when syscall 4 fires, show the bytes at $a0.
- Stack-frame intent hint — when `sw $ra, k($sp)` runs, prepend a
  "(saving return address — callee-save convention)" line.
- 9 FP-flavor pseudo-ops (l.s, l.d, s.s, s.d, li.s, li.d, ld-as-FP,
  mfc1.d, mtc1.d) — sit alongside the FP opcode gap.
- Niche: Gap 1 P2 (likely/and-link branches), P3 (LL/SC, LWL/LWR/SWL/SWR),
  P4 (traps), P5 (madd family), P6/P7 (CP0/TLB/COP2). All pedagogically
  marginal for intro MIPS — skip unless there's a specific audience.

## Recommended next-step ordering

If picking up cold:

1. **ABI annotations + syscall string dump + stack-frame hint** —
   bundle these three as a "polish pass." Maybe a day's work total.
   High pedagogical clarity per line of code.
2. **Golden expected-output file** — cheap regression test. Pin the
   current `/tmp/explain6.out` shape (or re-run and capture) as
   `tests/tt.explain.expected`, add a meson test target.
3. **FP support (Gap 1 P8)** — the biggest single add. Best done
   table-driven: extend the X-macro in `op.h` (or add a parallel table
   in explain.c) to carry the FP-instruction type tag so a small driver
   can format add.s / sub.s / mul.s / c.lt.s / cvt.* uniformly without
   70 hand-written cases. Some hand-writing still needed for mfc1/mtc1
   and the branch-on-FP-flag (bc1t/bc1f).
4. **REPL toggle (`explain` / `noexplain`)** — only if you have an
   audience of teachers running interactive sessions.

## Files touched, summary

- `src/spim.c` — flag parsing, REPL `print N($reg)` support, usage
  string. Includes a `bool consumed_nl` local in PRINT_CMD to avoid
  flush_to_newline gobbling the next command after a peek.
- `src/run.c` — two hook calls (`explain_before` after the pre-execute
  setup, `explain_after` after `PC += BYTES_PER_WORD`).
- `src/inst.c` — three things:
  (1) shift-disassembly nop bug fix (~3 lines around R2sh_TYPE_INST
      in `format_an_inst`),
  (2) ABI-name conversion of the disassembler — every integer
      register slot in `format_an_inst`'s per-type switch (B1, I1s,
      I1t, I2, B2, I2a, R1s, R1d, R2td, R2st, R2ds, R2sh, R3, R3sh,
      FP_I2a base, FP_R2ts integer, MOVC) plus the addr-expr
      `($N)` formatter now uses `$%s` with `int_reg_names[]`,
  (3) `inst_op_name(instruction*)` helper using the existing
      `name_tbl` lookup; declared in `include/inst.h`.
- `include/op.h` — header-guard scope fix. Type-macro definitions
  (ASM_DIR, R3_TYPE_INST, etc.) live inside `#ifndef OP_H_TYPES`; the
  `OP(...)` X-macro list lives outside any guard so it can be
  re-included by each table builder.
- `src/explain.c` (~1000 lines) — everything else. Per-opcode templates
  (`tpl_r3_arith`, `tpl_i2_arith`, `tpl_shift`, `tpl_load`,
  `tpl_store`, `tpl_branch_2reg`, `tpl_branch_1reg`,
  `explain_syscall`), the `subst_field_regs()` placeholder
  rewriter that substitutes `$rs`/`$rt`/`$rd` in template text
  with the concrete register names of the current instance,
  bit-layout renderers (`render_r_layout` / `render_i_layout` /
  `render_j_layout` + `explain_bit_layout` dispatcher called from
  `explain_before`, each taking a mnemonic argument used in the
  "->" decoding annotation), the 45-case opcode switch, the
  pseudo-op `struct pseudo_info` table with 40 entries,
  continuation tracking via file-static `pending_pseudo_name` /
  `pending_pseudo_multi`, and the after-step diff with
  load-force-print logic. Requires `<ctype.h>` for `isalnum` in
  the substitution helper.
- `include/explain.h` — declares `explain_mode`, `explain_before`,
  `explain_after`.
- `meson.build` — `src/explain.c` added to `source_files`.
- `tests/tt.explain.s` — 220-line smoke test covering every category
  the explanation has a template for, plus a pseudo-op exercise block.

## Non-obvious / gotchas

- `ENCODING(inst)` is now correctly populated for source-assembled
  instructions. The previous "uniformly zero" symptom was caused by an
  `#ifndef OP_H` header guard in `include/op.h` that wrapped the X-macro
  `OP(...)` entries — `inst.c` includes `op.h` three times (once each
  for `name_tbl`, `i_opcode_tbl`, `a_opcode_tbl`), and the guard meant
  only the first include actually populated a table. With the guard
  scoped to just the type-macro definitions (now `OP_H_TYPES`), all
  three tables populate and `inst_encode()` works. Still: opcode
  detection by reading RD/RT/SHAMT directly is fine, just no longer
  required as a workaround.
- The MVP intentionally picked Option A (parallel switch in
  `explain.c`) over Option B (table-driven). Pseudo-op narration IS
  a small Option B example (the `pseudo_info` table). When FP work
  starts, reconsider: 70 FP opcodes is the threshold where Option B
  pays off.
- `SOURCE(inst)` strips labels — `done: la $a0, hello` arrives as just
  `la $a0, hello` plus the line number. The parser in
  `find_pseudo_in_source()` only handles the `NNN:` line-number prefix.
  If somehow labels do show up, the pseudo-op detection will silently
  fail to match (returns NULL → no header). Worth a unit test if
  labels-with-instructions become important.
- The `tpl_jal` "Will write" prediction now branches on the
  `delayed_branches` global. If a future change adds JAL-like
  instructions (e.g. `bgezal`), they'll need the same treatment.
- Pseudo-op narration's "expansion text" in each `pseudo_info` entry
  is a general description, not derived from the actual emitted code
  at this PC. If the assembler chose a different expansion shape for
  a particular operand pattern (e.g. `li` with small vs large
  constant), the header text describes "typical" expansion; the real
  instruction narration below is authoritative.
- `tests/tt.explain.s` has `syscall 5` and `syscall 8` (read_int,
  read_string) **commented out** because they'd block on stdin. To
  exercise their narration manually: uncomment and pipe input —
  `echo 42 | ./builddir/spimulator -exception_file src/exceptions.s -explain -f tests/tt.explain.s`.
- **Debugging pattern that cracked the ENCODING bug**: when a value
  is mysteriously wrong, add a one-shot `fprintf(stderr, ...)` at
  the *use* site that dumps both the inputs AND the table/state it
  consulted. A `static int dumped = 0;` gate prints once. The
  ENCODING-zero bug only became obvious after dumping the size and
  first entries of `i_opcode_tbl` at the moment `inst_encode`
  consulted it — `size=0` was the smoking gun. Reading code alone
  had not been enough; the X-macro header guard hid the empty-table
  failure mode from inspection.
- Each compilation unit gets its own copy of `#include "op.h"`
  expansions. The current per-TU setup is: `inst.c` builds three
  tables (name_tbl, i_opcode_tbl, a_opcode_tbl), `scanner.l`
  builds one (keyword_tbl), `dump_ops.c` builds one. If you ever
  add a fourth table in any TU, remember to `#undef OP` and
  `#define OP(...)` before the new `#include`, matching the
  existing pattern.

## Where to look first when picking up

1. Read `tasks/teaching-mode.md` (~370 lines) for the original spec and
   the running issues list.
2. Read `tasks/teaching-mode-coverage.md` (~310 lines) for the audit
   methodology, Gap 1/2/3 breakdown, and which follow-ups landed when.
3. Skim `src/explain.c` from the top — the `pseudo_info` table is
   right after the snapshot variables, and `explain_before` /
   `explain_after` are at the bottom.
4. Run the smoke test (`ninja -C builddir && ./builddir/spimulator
   -exception_file src/exceptions.s -explain -f tests/tt.explain.s`)
   and skim a few hundred lines of output to confirm the rendering
   still looks right.
