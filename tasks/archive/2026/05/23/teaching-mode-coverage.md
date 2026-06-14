# Teaching Mode — Opcode Coverage Gaps

Audit performed 2026-05-11 against `src/explain.c` as it stands after the
MVP. The MVP intentionally covered a "first-week-of-MIPS" subset; this file
captures everything that still falls through to the
`(no detailed explanation for this opcode yet)` fallback, plus the
pseudo-ops that get no narration at all.

## Test coverage gaps in `tests/tt.explain.s`

Separate from opcode coverage in `explain.c`: even the opcodes the MVP
*does* narrate are mostly unexercised by the smoke test. Benchmark is the
MIPS subset every undergrad sees in Patterson & Hennessy *Computer
Organization and Design* Chapter 2–3, and the canonical SPIM programs
(read/print int, sum-an-array, recursive factorial, string reverse).

### Task 1 — base+offset addressing `N($reg)` (DONE 2026-05-11)

This is THE canonical load/store form in undergrad MIPS: stack frames,
array indexing, struct field access. The original test used only the
symbol-form `lw $t5, src` which goes through the `lui+lw` pseudo-op
expansion — a different code path than the literal `lw $t0, 8($sp)` form.
`tpl_load`/`tpl_store` in `src/explain.c` already render the effective
address formula correctly (they read `BASE`/`IOFFSET` and compute
`R[base] + off`), so this was a test-only fix. Added a `la`-then-offset
block including a small stack-frame idiom (`addiu $sp, $sp, -N` /
`sw $t, k($sp)` / `lw $t, k($sp)` / `addiu $sp, $sp, N`) plus an `lb`
byte-load via base+offset.

### Remaining gaps, by priority

Tasks 2–11 all landed in `tests/tt.explain.s` on 2026-05-11.
None required any change to `src/explain.c` — every opcode added below was
already in the MVP's case list; they were just unexercised. The test now
covers what an undergraduate would see in H&P Chapter 2–3.

2. **Procedure call + return.** ✓ Added `jal square` from main, and a
   `square:` helper at the bottom of the file (`mult $a0, $a0; mflo $v0;
   jr $ra`). Exercises `Y_JAL_OP` template and `Y_JR_OP` return.

3. **A real loop.** ✓ Added a 3-iteration counter loop using `addu`
   accumulator + `addi -1` decrement + `bne $t2, $zero, loop`. The label
   sits above the branch so this is a backward-taken branch — the
   pedagogically important case for explain output.

4. **`mult`/`div` with `mfhi`/`mflo`.** ✓ Added `mult 7,6 → mflo/mfhi`,
   then `div 100,7 → mflo/mfhi` (showing quotient and remainder both).

5. **Zero-compare branches.** ✓ Added `bgtz` (1>0), `bgez` (1≥0), `bltz`
   (-1<0), `blez` (-1≤0). All four taken; the not-taken branch case is
   already covered upstream by the `beq` at the top of the file.

6. **Set-less-than family.** ✓ Added `slt`, `slti`, `sltu`, `sltiu`.

7. **Input syscalls 5 and 8.** ✓ Added but commented-out, with
   instructions in the comment for how to feed stdin
   (`echo 42 | spimulator -explain ...`). Uncommented they'd block the
   test; this keeps the smoke test non-interactive by default while
   leaving the lines in place for manual exercise.

8. **Unsigned arithmetic variants.** ✓ Added `addu`, `subu`. `addiu`
   was already added in Task 1 (stack frame), and `sltu`/`sltiu` come
   from Task 6.

9. **Logical immediate + the other shifts.** ✓ Added `andi`, `ori`
   (direct form, not the pseudo-op `li` expansion), `xori`, `srl`,
   `sra`.

10. **Byte / halfword stores.** ✓ Added `sb` and `sh` via base+offset
    into `dst`. Combined with the `lb` from Task 1, the entire
    narrow-memory width=1 / width=2 path in `tpl_load`/`tpl_store` is
    now exercised.

11. **Direct `lui` (not via `la`/`li`).** ✓ Added the H&P Fig 2.18
    idiom verbatim: `lui $t9, 0xdead; ori $t9, $t9, 0xbeef`.

12. **Pseudo-op narration of `la`/`li`/`move`.** Still open — the test
    triggers them but `explain.c` has no narration for the pseudo-op
    itself. See Gap 2 ("Pseudo-ops have zero narration") below. The
    test will start exercising the pseudo-op narration path
    automatically once that feature lands; no test change needed in
    advance.

### Follow-ups landed 2026-05-11 (continued)

17. **`print` REPL command now accepts the `N($reg)` form.** ✓ Added to
    `src/spim.c` PRINT_CMD. After reading a `Y_INT` token, peek for `(`
    and (if matched) `Y_REG` and `)`, then compute `loc = R[reg] +
    int_val` and `print_mem(loc)`. Falls back to the existing
    absolute-address interpretation if the next token isn't `(`.
    Tracks Y_NL consumption across the peek so `flush_to_newline`
    doesn't eat the next command's tokens.

    Pedagogical value: a student writing `sw $t7, 4($sp)` can now type
    `print 4($sp)` in the REPL to inspect that exact memory location
    — same syntax in source and inspection. Negative offsets work
    too (`print -4($sp)`).

    The load/store templates in `src/explain.c` now suggest both
    forms in the "Try it yourself" block:
    ```
        print 0x7ffff49c          # inspect that memory
        print 4($sp)          # same memory, using the N($reg) form you wrote
    ```

    Verified with `printf 'print $sp\nprint 0($sp)\nprint 4($sp)\nprint -4($sp)\nquit\n' | spimulator ...` — addresses resolved as $sp, $sp+0, $sp+4, $sp-4.

### Follow-ups from the 2026-05-11 thoroughness review

Came out of running the rebuilt simulator end-to-end against the
extended test and asking: is this a defensible coverage of what an
undergraduate is expected to know?

13. **Missing core opcodes for an H&P-tracking course.** The categories
    below are universally taught and used in canonical assignments
    (sum-an-array, string reverse, factorial, etc.) but are still
    absent from the test:
    - **R-type bitwise**: `or`, `xor`, `nor`. We have the *immediate*
      forms (`andi`/`ori`/`xori`) and R-type `and`, but the register
      forms of `or`/`xor`/`nor` aren't exercised. `nor` is especially
      load-bearing — H&P uses it to explain why MIPS doesn't need a
      `not` opcode (it's `nor reg, reg, $zero`).
    - **Signed-vs-unsigned arith trap contrast**: `sub` (signed,
      traps), `multu`, `divu`. We have the no-trap `subu` and signed
      `mult`/`div`. H&P spends real time on the trap-on-overflow
      semantics — the contrast itself is the lesson.
    - **`lbu`** (load byte unsigned, zero-extend). The `lb` vs `lbu`
      distinction is a classic student bug for ASCII processing — any
      byte ≥ 0x80 sign-extends to a negative number under `lb`, which
      breaks comparisons against character constants. We narrate `lb`
      but not `lbu`.
    - **`lh`, `lhu`** — halfword loads. Common for `short[]` arrays.
      The store-narrow path is covered (`sh`); the load-narrow path
      isn't for halfwords.
    - **`j label`** — plain unconditional jump. Used for goto-style
      escapes from loops. We have `jal`/`jr` only.
    - **`jalr`** — indirect call (function pointers). MVP `explain.c`
      has a case for it; the test doesn't exercise it.
    - **`syscall 1` (print_int)** — every "compute and print" student
      assignment. MVP advertises `$v0`-aware narration for it but the
      test never invokes it.
    - **`syscall 11` (print_char)** — common for character output. Same
      status as syscall 1.

    Note on `multu`/`divu`: as of 2026-05-11 these still fall through
    to the generic fallback in `explain.c` (they're in Gap 1 P1 below,
    "direct analogs of opcodes already covered"). Adding them to the
    test is the trigger to also add their case bodies — they're ~6
    lines apiece mirroring `mult`/`div` with "(unsigned)" relabels.

14. **`tpl_jal` predicts `$ra ← PC+8` but spim writes `PC+4`.** ✓ Fixed
    2026-05-11. `Y_JAL_OP` and `Y_JALR_OP` in `src/explain.c` now read
    the `delayed_branches` global and emit either "PC+4, the next
    instruction" or "PC+8, skipping the delay slot" with the actual
    link value. Mirrors the same expression used in `src/run.c:314`.

15. **"(no register changes)" on a load whose value matches existing
    register contents.** ✓ Fixed 2026-05-11. `explain_after` in
    `src/explain.c` now opens with a small switch on `OPCODE(inst)`
    that flags loads (`Y_LW_OP`, `Y_LB_OP`, `Y_LBU_OP`, `Y_LH_OP`,
    `Y_LHU_OP`) and force-prints the destination register with a
    "(loaded from memory; same value as before — load still happened)"
    annotation when the post-load value matches the snapshot.
    Confirmed by running the rebuilt simulator — the very first lw at
    `0x00400000` (which loads argc=1 into an already-1 $a0) now reads:
    `$a0: 0x00000001  (loaded from memory; same value as before — load still happened)`.

16. **Shift disassembly mis-renders every shift as `nop`.** ✓ Fixed
    2026-05-11. Pre-existing spimulator bug in
    `src/inst.c:R2sh_TYPE_INST`: the nop detection checked
    `ENCODING(inst) == 0`, but the assembler never populates
    `ENCODING` for source-assembled instructions, so every shift
    rendered as "nop" in the "About to execute" line. Now checks
    `RD == 0 && RT == 0 && SHAMT == 0` (the canonical nop encoding) so
    real shifts disassemble correctly while genuine `sll $0, $0, 0`
    still prints as `nop`. Confirmed: `sll $t4, $t2, 2` now renders as
    `sll $12, $10, 2`; `srl $s5, $t2, 1` as `srl $21, $10, 1`; `sra` as
    `sra $22, $10, 1`. This also resolves the long-standing issue #1
    from the original `teaching-mode.md` notes.

## How to re-run the audit

The single source of truth is `include/op.h` — an X-macro of every
mnemonic the assembler recognizes. `scanner.l` (line ~606) and `parser.y`
both include it. The 3rd arg of `OP(...)` distinguishes `ASM_DIR`,
`PSEUDO_OP`, and the real-instruction type categories.

```sh
# 291 real-instruction Y_*_OPs
grep -E '^OP\(' include/op.h \
  | awk -F'[,()]' '$4 !~ /ASM_DIR/ && $4 !~ /PSEUDO_OP/ {gsub(/ /,"",$3); print $3}' \
  | sort -u > /tmp/oph_real.txt

# 199 opcodes spim can actually execute
grep -oE 'Y_[A-Z0-9_]+_OP' src/run.c | sort -u > /tmp/run.txt

# 45 opcodes I currently narrate
grep -oE 'Y_[A-Z0-9_]+_OP' src/explain.c | sort -u > /tmp/explain.txt

# Gap 1: executable but unexplained
comm -23 /tmp/run.txt /tmp/explain.txt

# Gap 3: assembler-accepted but not executable (FYI only)
comm -23 /tmp/oph_real.txt /tmp/run.txt

# Pseudo-ops (49) — none of these are narrated as themselves
grep -E '^OP\(' include/op.h \
  | awk -F'[,()]' '$4 ~ /PSEUDO_OP/ {gsub(/"/,"",$2); print $2}'
```

## Gap 1 — executable but unexplained (154 opcodes)

These dispatch in `src/run.c` and reach `explain_before`/`explain_after`,
but fall through to the generic fallback. Suggested priority order:

### P1 — direct analogs of opcodes already covered (~12, easy wins)

Re-use the existing templates with minor tweaks.

- `Y_DIVU_OP` `Y_MULTU_OP` — unsigned mul/div; mirror `Y_DIV_OP`/`Y_MULT_OP`
- `Y_SLLV_OP` `Y_SRAV_OP` `Y_SRLV_OP` — variable shifts; mirror SLL/SRA/SRL
  but read shift count from `rs` instead of `SHAMT`
- `Y_MOVN_OP` `Y_MOVZ_OP` — conditional move on $rt nonzero/zero
- `Y_MTHI_OP` `Y_MTLO_OP` — write HI/LO from a GPR (inverse of MFHI/MFLO)
- `Y_CLO_OP` `Y_CLZ_OP` — count leading ones / zeros

### P2 — "likely" and branch-and-link branch variants (10)

`Y_BEQL_OP Y_BNEL_OP Y_BGEZL_OP Y_BGTZL_OP Y_BLEZL_OP Y_BLTZL_OP
Y_BGEZAL_OP Y_BGEZALL_OP Y_BLTZAL_OP Y_BLTZALL_OP`

Re-use existing BEQ/BNE/BGEZ/... templates plus a one-line note. The
"likely" variants annul the delay slot when not taken; the "AL" variants
also write `$ra` like `JAL`. Worth a sentence each, not separate templates.

### P3 — atomic + unaligned memory (6)

`Y_LL_OP Y_SC_OP` — load-linked / store-conditional; one-paragraph
explanation of the "did anything write to this address between LL and SC"
contract.
`Y_LWL_OP Y_LWR_OP Y_SWL_OP Y_SWR_OP` — unaligned word load/store halves.
Worth narrating since students rarely see these and the semantics are
surprising.

### P4 — trap instructions (12)

`Y_TEQ_OP Y_TEQI_OP Y_TGE_OP Y_TGEI_OP Y_TGEIU_OP Y_TGEU_OP
Y_TLT_OP Y_TLTI_OP Y_TLTIU_OP Y_TLTU_OP Y_TNE_OP Y_TNEI_OP`

Same shape as the branch family but trap-on-condition instead of branch.
One shared template parameterized by the condition would work.

### P5 — extended mul/div (4)

`Y_MADD_OP Y_MADDU_OP Y_MSUB_OP Y_MSUBU_OP` — multiply-accumulate into
HI/LO. Mirror MULT/MULTU with an "(adds to HI:LO instead of replacing)"
hint.

### P6 — system / coprocessor 0 / TLB (15)

`Y_BREAK_OP Y_SYNC_OP Y_PREF_OP Y_ERET_OP Y_RFE_OP Y_CACHE_OP
Y_TLBP_OP Y_TLBR_OP Y_TLBWI_OP Y_TLBWR_OP
Y_MFC0_OP Y_MTC0_OP Y_CFC0_OP Y_CTC0_OP`

Lower educational priority — students touch these only in OS-flavored
courses. A short generic blurb per category is fine; deep diffs of CP0
register state would be a project of its own.

### P7 — Coprocessor 2 family (13)

`Y_COP2_OP Y_BC2F_OP Y_BC2FL_OP Y_BC2T_OP Y_BC2TL_OP
Y_MFC2_OP Y_MTC2_OP Y_CFC2_OP Y_CTC2_OP
Y_LDC2_OP Y_LWC2_OP Y_SDC2_OP Y_SWC2_OP`

Vendor-defined coprocessor; in practice rarely covered in coursework. Low
priority. Generic blurb only.

### P8 — Floating point (~70 opcodes)

The original spec explicitly listed FP as a stretch goal. Categories:

- **Arithmetic single/double** (8): `Y_ADD_{S,D}_OP Y_SUB_{S,D}_OP
  Y_MUL_{S,D}_OP Y_DIV_{S,D}_OP`
- **Unary single/double** (8): `Y_ABS_{S,D}_OP Y_NEG_{S,D}_OP
  Y_SQRT_{S,D}_OP Y_MOV_{S,D}_OP`
- **Compare** (32): the entire `Y_C_*_{S,D}_OP` family — 16 condition
  codes × 2 precisions. A single condition-table-driven template.
- **FP branches** (4): `Y_BC1F_OP Y_BC1FL_OP Y_BC1T_OP Y_BC1TL_OP`
- **Conversions** (6 executable): `Y_CVT_{D,S,W}_{S,D,W}_OP`
- **Round-to-int** (8): `Y_CEIL_W_{S,D}_OP Y_FLOOR_W_{S,D}_OP
  Y_ROUND_W_{S,D}_OP Y_TRUNC_W_{S,D}_OP`
- **FP conditional move** (8): `Y_MOV{F,T,N,Z}_{S,D}_OP`
- **FP/CP1 transfer** (4): `Y_MFC1_OP Y_MTC1_OP Y_CFC1_OP Y_CTC1_OP`
- **FP load/store** (4): `Y_LWC1_OP Y_SWC1_OP Y_LDC1_OP Y_SDC1_OP`

A table-driven approach (Option B in the original design doc) would pay
off here — 70 case statements written individually is just churn.

## Gap 2 — pseudo-ops have zero narration (~~49 mnemonics~~ — partially fixed 2026-05-11)

### Status update 2026-05-11

Pseudo-op narration is now wired up in `src/explain.c`. A
`struct pseudo_info` table holds 40 of the 49 pseudo-ops with a short
pedagogical description for each (the 9 FP/coprocessor pseudo-ops —
`l.d`, `l.s`, `s.d`, `s.s`, `li.d`, `li.s`, `ld`-as-FP, `mfc1.d`,
`mtc1.d` — are not yet covered; they sit alongside the FP opcode gap in
Gap 1 P8).

When a student writes `blt $t0, $t1, label`, the explain output now
opens each emitted instruction with a header like:

```
  Pseudo-instruction `blt` (as written in source):
    Branch if Less Than (signed) — branch if $rs < $rt. Expands to
    `slt $at, $rs, $rt` + `bne $at, $0, label`.
  The line below is the first real instruction the assembler emitted.
```

…followed by the existing per-opcode template for the real `slt`. The
second emitted instruction (`bne $at, $0, label`) is annotated as:

```
  (continuation of the `blt` pseudo-op expansion above —
   same source line, next real instruction emitted)
```

Continuation tracking lives in two file-static variables
(`pending_pseudo_name`, `pending_pseudo_multi`) and triggers when the
current instruction has no SOURCE line of its own AND the prior
matched pseudo-op was flagged `may_be_multi`.

### Still open

- FP-flavor pseudo-ops (the 9 listed above) are unrecognized — they'll
  produce expansions with no pseudo-op header. Same low priority as
  the FP opcode gap below.
- The "expands to ..." text in each `struct pseudo_info` entry is a
  general description, not derived from the actual emitted code at
  this PC. If the assembler chose a different expansion for this
  particular operand shape (e.g., `li` with a small constant vs a
  large one), the header text may not exactly match what follows. The
  real instruction narration immediately below is authoritative; the
  pseudo-op header is general. Acceptable tradeoff.

### Original problem statement (kept for reference)

These never appear as themselves at execution time; the assembler expands
them. The student writes `la $a0, msg`, sees `lui` then `ori` narrated
separately, and gets no signal that those two lines came from one source
line. This is also what teaching-mode.md issue #2 is pointing at.

Full pseudo-op list from `op.h`:

```
abs   b     bal   beqz  bge   bgeu  bgt   bgtu
ble   bleu  blt   bltu  bnez  l.d   l.s   la
ld    li    li.d  li.s  mfc1.d move  mtc1.d mulo
mulou neg   negu  nop   not   rem   remu  rol
ror   s.d   s.s   sd    seq   sge   sgeu  sgt
sgtu  sle   sleu  sne   ulh   ulhu  ulw   ush
usw
```

### Proposed fix shape

In `explain_before`, when `SOURCE(inst)` is set and the source line's
first token is a pseudo-op mnemonic, prepend something like:

```
  As written in source:
    la $a0, message
  Pseudo-instruction; the assembler expanded it to this real
  instruction (and the next one is the continuation):
    lui $at, 0x1001
```

Then on the next `explain_before` call, if the previous instruction was
part of a multi-instruction pseudo-op expansion AND the current
instruction has no `source_line` of its own, emit:

```
  (continuation of the `la` expansion above)
```

Needs: a list of pseudo-mnemonics (extract from `op.h` at startup, or
hard-code), plus one-line state in `explain.c` tracking
"previous instruction was a pseudo-op head."

### Single-instruction pseudo-ops are easier

Many of the 49 expand to exactly one real instruction (`move` → `addu`,
`nop` → `sll $0,$0,0`, `b` → `beq $0,$0,target`, `not` → `nor`, `neg` →
`sub`). For those, no continuation tracking is needed — just rewrite the
"About to execute" header to show the source line when it matches a
pseudo-op mnemonic.

## Gap 3 — assembler-accepted but not executable (92, out of scope)

`op.h` declares them so source files containing them assemble cleanly,
but `run.c` has no dispatch case so they can never be the current
instruction at an explain hook. No action needed unless someone wires up
the corresponding execution support.

Buckets, FYI:

- MIPS32r2+ extensions: `EXT INS SEB SEH WSBH ROTR ROTRV SYNCI
  JR.HB JALR.HB DI EI EHB RDHWR RDPGPR WRPGPR DERET SDBBP SSNOP
  MFHC1 MFHC2 MTHC1 MTHC2`
- Paired-single FP (28): every `*_PS_OP`
- Long-integer FP (16): `CVT_L_* CVT_*_L CEIL_L_* FLOOR_L_*
  ROUND_L_* TRUNC_L_*`
- Indexed FP load/store (7): `LWXC1 SWXC1 LDXC1 SDXC1 LUXC1 SUXC1 PREFX`
- MIPS-3D NMADD/NMSUB family: `NMADD_{S,D,PS} NMSUB_{S,D,PS}
  MADD_{S,D,PS} MSUB_{S,D,PS}` (the MADD/MSUB variants here are FP — the
  integer `MADD`/`MSUB` are separate and *are* in Gap 1)
- Reciprocal/rsqrt FP: `RECIP_{S,D} RSQRT_{S,D}`
