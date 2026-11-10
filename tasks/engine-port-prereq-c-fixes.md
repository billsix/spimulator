# Before the Python port: fix the C engine's wrong-value bugs, pin the test oracle, drop `,` as an FP decimal separator

**Status:** in progress
**Priority:** 3
**Difficulty:** 5
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/10/repo-review.md §3 (F5), §6 (F7), §3.20 (F12)
**Part of:** `tasks/engine-ports.md`
**Next:** `tasks/engine-port-python.md` (its Plan step 1 is this task), then `tasks/engine-port-haskell.md` and `tasks/engine-port-rust.md`

## BLUF

The maintainer decided the C engine's bugs get fixed before the Python port, so the port is diffed
against correct output, and that `,` stops being accepted as a floating-point decimal separator.
This task does three things in C: it fixes the F5 wrong-value bugs, each with a regression test; it
closes the F7 test-gate gaps, so the oracle is complete; and it drops the `,` separator. Done means:
every F5 item has a failing-then-passing test, the F7 tests run in the meson regression suite,
`.word 1,2` assembles two words, and `make image` (sanitizers on) is green.

## Context (read first)

- `tasks/review/2026/11/10/repo-review.md` — §3 rows 3.4, 3.5, 3.7, 3.8, 3.10, 3.13 (F5), §6.1, §6.2,
  §6.4–§6.6 (F7), §3.20 (the separator; upstream `9b0892d`, `8141259`). The record is frozen; re-check
  each row against the tree before acting.
- `tasks/explain-mode-correctness-bugs.md` owns the explain-mode bugs (F6); not this task.
- The memory-safety bugs (F3) and the setjmp locals (F4) do not change output on correct programs,
  so they do not gate the port; they are left for their own task.

## Plan

1. **Separator (F12, §3.20):** remove `,` and `'` from the FP-literal scan in `src/scanner.c`
   (`scan_fp_check`), keeping only `.`. Test: `.word 1,2` assembles to two words; an FP literal
   with `.` still parses. Note the change in `Documentation/spim.1` if it documents the separator.
2. **F5, one commit per bug, test first:** `divu` overflow guard, forward `.word lab+N`, `c.cond`/`movf`
   decode, `movn.s`/`movz.s` encoding, `BRANCH_OFFSET` reach, signed-overflow UB in `signed_multiply`
   and in text-segment `sb`/`sh`. Any `opcodes.h` change is gated with `BUILD_TREE_SITTER=1` (`CLAUDE.md`).
3. **F7:** run `tt.alu.bare.s`/`tt.fpu.bare.s` under `-bare -noexception`; make the `bare` test pass
   `-bare`; make the AST-parity loop fail on a missing dump; pin `tt.core.s`'s full output as a golden;
   add one default-handler (no `-ef`) test; fix or delete `explain_parity`.
4. Gate: `make image` with the default flags (sanitizers on).

## Decisions

- Fix before porting, and drop `,` as a decimal separator (William Emerison Six <billsix@gmail.com>,
  2026-11-10; recorded in `tasks/engine-port-python.md`).
- Drop `'` as well; `.` is the only FP decimal separator (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- Go-ahead for this task, then the Python, Haskell and Rust ports in that order, on one branch with
  the agent committing; gpg signing off for the day (William Emerison Six <billsix@gmail.com>, 2026-11-10).

## Log (2026-11-10)

Written as the work went, for the maintainer to read afterwards.

**Fixed in C (Plan steps 1 and 2), each covered by `tests/tt.engine_fixes.s` (a self-checking
program: a wrong result prints `Failed engine-fixes case N`):**

1. **Decimal separators** (§3.20; case 1). `src/scanner.c` accepted `1,5` and `1'5` as the number
   1.5, so `.word 1,2` (no space) was read as one float instead of two words. Only `.` is a decimal
   point now.
2. **`divu` overflow guard** (§3.4; case 2). Unsigned division reused the signed division's guard
   against `0x80000000 / -1`, so `divu 0x80000000, 0xffffffff` left HI and LO unchanged. Unsigned
   division cannot overflow, so the guard is gone; the result is LO = 0, HI = 0x80000000.
3. **Undefined behavior in `mult` and in byte/half stores into code** (§3.13; case 3, plus the
   sanitizer gate). C's signed-integer overflow is undefined behavior, which the UBSan build turns
   into a crash. `signed_multiply` negated its 64-bit result with `LO += 1` on a signed int, which
   overflows for `mult 0x80000000, 1`; `sb`/`sh` into the text segment shifted `0xff << 24` as a
   signed int. Both now do the same arithmetic on unsigned ints, which wrap by definition.
4. **Forward data references** (§3.5; case 4). A `.word lab+4` written before `lab` is defined
   stored `lab` and lost the `+4`; a forward `.byte lab` or `.half lab` overwrote four bytes, the
   following data included. Each recorded use now keeps its constant and its width (1, 2 or 4
   bytes), and the resolution writes exactly that many bytes (`write_data_value`,
   `src/symbol-table.c`).
5. **Decoding an FP compare or FP conditional move** (§3.7; cases 5 and 6). The engine keeps each
   instruction in a struct whose fields share storage (the FP source register `fs` lives in the
   same slot as the integer `rd`). Decoding `c.cond.fmt` and `movf/movt/movn/movz.fmt` from a raw
   word wrote `fd` into that slot, overwriting `fs`, so `c.lt.s $f2, $f4` compared `$f0`. The
   decoder only runs when a program stores a word into its own code at run time, so the test does
   exactly that: it overwrites a placeholder `nop` with `sw`, then executes it. (A first version of
   the test put the word in `.text` with `.word`; that goes to the data segment, so it never ran
   the decoder. Caught by dumping the text segment with `-dump`.)
6. **Encoding `movn.s`/`movz.s`** (§3.8; case 7). These test an integer register, but the encoder
   wrote only the top three bits of its number (`rt >> 2`, the movf/movt condition-code field), so
   `movn.s $f6, $f2, $t1` encoded register 8 instead of 9, and the disassembler printed `2` instead
   of `$t1`. The whole field is encoded now, and the disassembler prints a register name for
   movn/movz and a condition code for movf/movt.
7. **Branches more than 32 KB away** (§3.10; case 8). A branch stores a 16-bit offset counted in
   words (±128 KB). The assembler cut the offset to 16 bits while it was still in bytes, and the
   decoder did the same, so a branch more than 32 KB away jumped to the wrong place (the old engine
   loops forever on case 8). Both keep the full offset now, and a branch beyond ±128 KB is an
   assembly error instead of a silent wrong jump. The test reaches its target across `.align 16`
   (64 KB of padding).

**Found while testing, not in the review, fixed:** a token that cannot start an expression inside a
`.byte`/`.half`/`.word` list (for example `.word 1.5`) made the assembler print `Expected expression`
forever: 64 MB of output in ten seconds. `parse_expr_list` reported the error without consuming
the token, then asked for the next expression again. It now skips to the end of the line after an
error. `tests/tt.parse_error_data_list.s` expects exactly one error and exit status 2. This is a
real loop the ports would otherwise have to copy.

**Test goldens updated on purpose:** `tests/golden.ast_parity.txt` and
`tests/golden.ast_parity_all.txt`. What these are: the assembler parses a program into a tree (the
AST, abstract syntax tree) and then turns the tree into machine code. When the old parser was
replaced (2026-08-03), its exact machine code for a set of sample programs was saved in these two
files, and the `ast_parity` tests check that today's assembler still produces exactly the same
bytes. Fix 6 changes the bytes for eight `movn`/`movz` instructions (the register is now encoded)
and how they print, so the saved files were regenerated with `tests/gen-parity-golden.sh` and the
diff was reviewed: those eight lines changed, nothing else.

**Checked:** all 27 regression tests pass on the fixed engine (meson, gcc build). Both new tests
fail on the engine from before the fixes, built from `HEAD` in a scratch directory.

**Noticed, not changed:**
- The `args_cmd` and `disasm` tests fail on a build without line editing
  (`-Dline_editing=disabled`), so the suite silently depends on libedit. The image builds with it.
- 87 clang-format differences already exist in `src/` and `include/` under the image's clang-format
  23.1.5. The fixes formatted only their own lines; a tree-wide reformat is a separate decision.
