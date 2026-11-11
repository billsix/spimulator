# Before the ports: fixed the C engine's wrong-value bugs, closed the test gaps, dropped `,` and `'` as decimal separators

**Created:** 2026-11-10 · **Completed:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Part of:** `tasks/archive/2026/11/10/engine-ports.md` · **Source:** tasks/review/2026/11/10/repo-review.md §3 (F5), §6 (F7), §3.20 (F12)

## Summary

The maintainer decided that the C engine's bugs are fixed before any port, so every port is
compared with correct output, and that `.` is the only decimal point. This task fixed the review's
seven wrong-value bugs and one endless parse-error loop it found along the way, each with a test
that fails on the engine from before. It also closed the test gaps that would have let a port
differ from C unnoticed. Commits on branch `engine-ports`: `43c433a` (engine fixes), `5b3a7d6`
(test gaps). The gate `make image` (sanitizers on) passed at `0ada1bc`: 64 meson tests, and 29
regression tests each under UBSan and ASan.

**Fixed in C, each covered by `tests/tt.engine_fixes.s` (a self-checking
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

**Test gaps closed (review F7):**
- **`fpu_bare` (new):** upstream's FPU torture test `tt.fpu.bare.s` had never run. It runs under
  `-bare -noexception`, as upstream ran it, and passes.
- **`tt.alu.bare.s` still does not run:** 19 of its lines do not assemble in the fork (immediates
  out of range, and MIPS-I coprocessor-3 instructions MIPS32 removed). Whether to edit the test or
  loosen the assembler is the maintainer's call: `tasks/alu-bare-test-assembler-strictness.md`
  (proposed).
- **The `bare` test is left as it is.** Review §6.2 says it never passes `-bare`. Upstream ran
  `tt.bare.s` the same way (`-delayed_branches -delayed_loads -noexception`;
  `git show 6cb6a08:spim/Makefile`, line 135): it tests delayed branches and loads, not bare mode.
  Bare mode is covered by `fpu_bare` now.
- **The parity test cannot skip a program any more.** `ast_parity_all` used to skip any program
  that wrote no dump, which is how the three bare-machine programs fell out silently. The program
  list now lives in one file, `tests/parity-programs.txt`, with each program's flags. Both the test
  and `tests/gen-parity-golden.sh` read it through `tests/parity-capture.sh`, so they cannot list
  different programs, and a listed program that writes no dump fails the test (checked by adding
  `tt.parse_error.s` to a copy of the list). The golden gained `tt.bare.s` and `tt.fpu.bare.s`;
  nothing in it changed.
- **`core_full` (new):** `tt.core.s`'s whole stdout and stderr are pinned
  (`tests/golden.core.stdout`, `tests/golden.core.stderr`), along with its exit status. Before, only
  the last line was checked, so about 25 expected exception messages and the float prints were never
  compared. The output is deterministic: two runs, a different working directory and a different
  environment gave identical output. Only the `Loaded: <exception file>` line differs, so the test
  leaves it out. The exit status is 4, and that is correct: the program returns from `main` with
  `$v0 = 4` (left by its last `print_str` syscall), and main's return value is the exit status.
- **`core_default_handler` (new):** the same program on the built-in exception handler (compiled
  into the binary with `#embed`; what users get without `-exception_file`). No test took that path
  before. Its output is the same as `core_full`'s.
- **`explain_parity` deleted.** It compared `-dump` output, and `-dump` never runs the program, so
  the test could not fail. A real one (the program's output must not change with `-explain`) belongs
  with `tasks/explain-mode-correctness-bugs.md`, which now says so. `-explain=1` on `tt.core.s` is
  too verbose for that (about 45 million stderr lines in a minute), so it needs a small program.

**Also noticed:** assembling the shipped exception handler prints `Target of jump differs in
high-order 4 bits from instruction pc 0x80000258` on every run (it is in `golden.core.stderr`). A
kernel-text `j` to a user-text address is outside the 256 MB region a `j` can reach. This happens
with both the file and the built-in handler. Not changed; the goldens pin it as today's behavior.
