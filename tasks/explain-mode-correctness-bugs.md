# Fix the teaching mode's correctness bugs (it hides exceptions and misstates what ran)

**Status:** proposed — needs go-ahead
**Priority:** 2
**Difficulty:** 4
**Created:** 2026-11-05 (William Emerison Six <billsix@gmail.com>)
**Next:** `tasks/explain-levels-redesign.md` (fix these first, so the redesign's goldens start from correct output)

## BLUF

`-explain` misleads the learner in several ways. The worst is that at any level it **swallows exceptions and
can change the program's exit status**. Fix the bugs below without redesigning the levels, and add a test for
each so it can't come back. Done means every bug has a regression test that failed before the fix, `meson test`
passes with line editing both enabled and disabled, and `tests/tt.explain.expected` is regenerated (it
currently locks in bug 3).

## Context (read first)

- Found by an audit on 2026-11-05: built `spimulator` out of tree and ran sample programs at L0–L4. The
  audit's transcripts were in a session scratch directory, which no longer exists; each bug below includes
  its reproduction.
- Code: `src/explain.c` (`explain_print_step_header`, `explain_after`, `lookup_classification`, the branch
  `tpl_*` templates, `skip_source_prefix`); `src/run.c` `run_spim`; `src/instruction.c` `inst_to_string`.
- The target behaviour for wording and levels is `tasks/reference/teaching-mode-learner-model.md`. This task
  fixes only what is *wrong*, not what is merely weak.

## Bugs

1. **Exceptions are swallowed (verified 2026-11-05).**
   - **Cause:** `explain_print_step_header()` calls `inst_to_string()`, which sets
     `exception_occurred = 0` (in `src/instruction.c`, at the top of `inst_to_string`). That runs before
     `run_spim` checks `if (exception_occurred) handle_exception()`.
   - **Repro:** a three-line program `main: li $t0, 0x10010001 / lw $t1, 0($t0) / jr $ra`.
     - Without `-explain`, it prints "Exception occurred at PC=… Unaligned address … Exception 4 … occurred
       and ignored".
     - With `-explain=1`, it prints nothing about the exception. L2 even says the load happened.
   - The exit status can differ too: `get-char-from-user-1` exited 133 at L0 and 132 at L1.
   - REPL `step` without explain hits the same problem through `print_inst()`: `step` shows no exception
     where `run` does.
   - **Fix direction:** make the disassembly-to-string path not touch the simulator's exception state
     (save/restore, or a side-effect-free reader).
2. **Branch targets are 4 bytes off** in the default non-delayed mode. The branch templates use
   `snap_PC + 4 + BRANCH_OFFSET`. Example: `b beginningOfLoop` says it "would transfer to 0x00400040" while
   the PC line shows `→ 0x0040003c`. Compute the target the way `run.c` does, for both delayed and
   non-delayed mode.
3. **Every `syscall` is narrated as "continuation of the `li` pseudo-op".** The parser emits `syscall` with
   no source line, and `explain_after` treats any source-less instruction after a multi-instruction
   pseudo-op as its continuation. This happened on 11 of 11 syscalls in `print1through10`.
4. **The first/continuation labels are wrong the other way too.** The second half of a real two-instruction
   expansion carries the source line, so `bgt` says "This was the first real instruction" twice. A `li`/`la`
   that expands to one instruction still says "first".
5. **Pseudo-ops behind a label are missed.** `skip_source_prefix()` skips only the `NN:` prefix, so
   `main: li $v0, 4` gets no pseudo-op header. Symbol-relative `lw $t1, foobar` / `sw $t3, buf` (which expand
   to `lui $at` + `lw`/`sw`) are never recognized as expansions.
6. **Fixed pseudo-op descriptions contradict what was emitted.**
   - `b` claims "Encoded as `beq $0,$0`" but `bgez $r0` was emitted.
   - `bgt $t0,10` claims "slt…+bne" but `slti $at,$t0,11` + `beq` was emitted.
   - `la` claims "lui+ori" but only `lui` was emitted.

   Short-term fix: correct the table. The redesign replaces it with the real expansion.
7. **Wrong categories and modifier glosses** in `lookup_classification()`:
   - `slt`/`sltu`/`slti`/`sltiu` are filed as "Conditional branch".
   - The `u` in `sltu` is glossed as "no trap on overflow"; it means unsigned compare.
   - `lui` is filed as "Data transfer … memory".
   - `nop` and `li`→`ori` are filed as "Logical: bitwise operations and shifts".
   - `addiu` says "unsigned" next to "sign-extended immediate".
8. **Levels contradict each other.** For `ori`, L2 says "zero-extended", L3 shows the immediate "(signed)",
   and L4 says "a 16-bit signed immediate". `ori`'s immediate is zero-extended.
9. **The program's exit is never narrated.** The exit `syscall` ends the run before `explain_after` runs.
   Examples: `helloworld.s` exits with status 4 (`$v0` left at 4, passed through by `__start`) and nothing
   says so. Narrate "program exited with status N" at every level.

## Side findings (same audit; fix here, since they are small)

- With `-Dline_editing=disabled`, piped REPL input breaks: the `args_cmd` and `disasm` tests fail in
  `meson test`. All 25 pass with line editing enabled. CI or the image should test both settings.
- `-show-expansion` prints raw opcode numbers (`op=544`) instead of mnemonics, and child line numbers off by
  one.
- `Documentation/spim.1`, the usage text, and `CLAUDE.md` each describe the levels differently from the
  code. Only `pgu/docs/source/examplesap.rst` matches, and its "A bug on purpose" section shows an exception
  at L1 that bug 1 hides. Make all four describe the current behaviour (the redesign will change them again).

## Plan

1. For each bug, add a failing regression test first: a small `.s` plus expected output, or a property check
   in `tests/`. Bug 1 has the cheapest strong test: run the same program at L0–L4 and assert stdout without
   narration and the exit status are identical.
2. Fix the bugs one at a time, in priority order: 1, 2, 9, 3–5, 6–8, then the side findings.
3. Regenerate `tests/tt.explain.expected` and read the diff by hand (it should differ only where a bug was).
4. Gates: `meson test` with `line_editing` enabled and disabled; a nested `make image` (the image runs the
   suite plus the UBSan and ASan gates).

## Open questions

None. These are bugs; the wording and level design are decided in the redesign task.
