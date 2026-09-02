# MIPS FPU in spimulator — how to write a floating-point example demo

**Reference document.** Distilled while building the first FPU example, `rpn`
(`examples/src/algorithms/rpn/`, 2026-09-02, William Emerison Six
<billsix@gmail.com>). Read this before writing another floating-point demo
(e.g. `tasks/calc-language.md`), so the parser quirks and the C↔asm matching
technique don't have to be rediscovered.

## The core problem a paired float demo must solve

The examples harness (`examples/tests/run-demo.sh`) runs BOTH a **native-compiled
C** binary and the **asm under spim**, and diffs each stdout against ONE pinned
`.expected` golden. The C is freestanding (`-nostdlib`, own `_start` via
`crt0.h`, reaches the kernel through `os.h` inline-asm syscalls) — so there is
**no `printf` and no "print double" Linux syscall**. Therefore, to make the two
sides byte-identical, **both sides must format the double with the identical
algorithm**, using only IEEE-754 operations that are deterministic across the
host FPU and spim's software FPU. Do NOT print via spim's `syscall 3`
(print_double, `%.18g`) for output the golden checks: the native C side cannot
reproduce that string.

## The verified technique (used by `rpn`)

- **Synthesize FP constants from integers** — `li $t,10; mtc1 $t,$f; cvt.d.w
  $f,$f` gives `10.0`; `mtc1 $zero,$f; cvt.d.w` gives `0.0`. Avoids `.double`
  data entirely (see the parser quirk below). In C the constants are plain
  literals; `1.0/10.0 == 0.1` in IEEE, so a literal `0.1` and an asm
  `1.0/10.0` agree bit-for-bit if you ever need a tenth.
- **Parse a number identically on both sides**: accumulate an integer *mantissa*
  as a double (`mantissa = mantissa*10 + digit`, via `cvt.d.w`/`mul.d`/`add.d`),
  count fractional digits, then divide by `10.0` once per fractional digit
  (`div.d`). Same sequence of IEEE ops in C and asm ⇒ identical value.
- **Print a double with a fixed deterministic printer** (both sides): handle
  `nan` (the only value with `value != value`) and `inf` (`value != 0 &&
  value + value == value`) by name; take the sign; print the integer part; if a
  fraction remains, print `.` and a fixed number of **truncated** fractional
  digits. Truncation, not rounding, is what makes C's `(int)x` cast and asm's
  `trunc.w.d` agree — do NOT use `cvt.w.d` for this (it honors the rounding mode,
  default round-to-nearest, and will disagree with the C cast).
- **Read stdin byte-at-a-time with `syscall 12`** (`read_char`): returns the
  byte in `$v0`, or `-1` at EOF — exactly matching C's `read_char()` in `io.h`.
- **Verify by battery**: run many expressions through both C and asm and `diff`
  each pair before generating the golden; then generate the golden FROM the C
  reference (the oracle, never hand-typed) and confirm the asm reproduces it.

## Simulator parser idiosyncrasies found (2026-09-02)

These are real; work around them (and note them for `tasks/fix-inherited-idiosyncrasies.md`
/ `tasks/ast-column-tracking.md`, which this corroborates):

1. **A labeled `.double`/`.float` directive immediately followed on a later line
   by ANOTHER labeled statement fails to parse** — `spim: (parser) Expected
   label on line N`. A *single* labeled `.double`, or a comma-list under one
   label (`c: .double 3.5, 2.0`), parses fine; two consecutive labeled `.double`
   lines do not (reproduced with blank line and `.align` between — still fails).
   The `-bare` FPU torture test uses consecutive `.double`s and passes, so the
   bug is in the non-bare/pseudo path. **Workaround:** synthesize FP constants
   from integers (above) so a float demo needs no `.double` data at all.
2. **A data label whose name is an instruction mnemonic breaks parsing** — e.g.
   `neg:`, `abs:`, `mov:` collide with `neg`/`abs`/`mov` and give `Expected
   register`. Prefix such labels (`op_add`, `lblneg`, …).

## FPU facts confirmed in this simulator

- Opcodes present and working (non-bare, no explicit delay slots needed):
  `cvt.d.w`, `cvt.s.d`, `cvt.w.d`, `trunc.w.d`, `add.d`/`sub.d`/`mul.d`/`div.d`,
  `neg.d`, `mov.d`, `ldc1`/`sdc1`/`lwc1`, `mtc1`/`mfc1`, and the FP compares
  `c.eq.d`/`c.lt.d` with `bc1t`/`bc1f`. `l.d $f, label` (pseudo) loads a double
  from a symbolic address; `ldc1 $f, label` works too.
- FP syscalls in `src/syscall.c`: 2 print_float, 3 print_double (`%.18g`),
  6 read_float, 7 read_double. Integer/char: 1 print_int, 4 print_string,
  11 print_char, 12 read_char (`-1` at EOF).
- Doubles on the `$sp` stack: `addi $sp,-8; sdc1 $fX,0($sp)` to push,
  `ldc1 $fX,0($sp); addi $sp,8` to pop. `rpn` uses this as its operand stack;
  save/restore the original `$sp` (it kept it in `$s1`).

## Open observation — asm error-exit status is not propagated

In batch mode, the examples' error-exit convention (`li $a0,N; li $v0,17;
syscall`, syscall 17 = exit2, as `binary-search.asm` uses) does NOT set the
shell exit status: the simulated program's non-zero code comes back as `0`. The
native C side exits with the real code. So an error-path golden test that pins
`.expected-status` would see C=1 / asm=0 and fail. `rpn`'s golden pins the
SUCCESS path only (both exit 0). Whether spim should propagate the exit2 code in
batch mode is worth a look (relates to the `exit-demo`/`atexit-demo` tests,
which do pin status — check how they differ). Not yet filed as its own task.
