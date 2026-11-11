# Fixed the teaching mode's correctness bugs

**Created:** 2026-11-05 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §4.1.2, §4.1.4 F10 (added 2026-11-11)

## Summary

A 2026-11-05 audit found `-explain` misleading the learner. All nine bugs, the two F10 additions
and the side findings were fixed in C, each with a regression check that failed before its fix.
The levels were not redesigned; that is `tasks/explain-levels-redesign.md`. A fresh subagent did
the C work with git forbidden, the coordinator reviewed and committed it, and three more subagents
mirrored the port-visible part in Python, Haskell and Rust.

| # | Bug | Fix | Pinned by |
|---|---|---|---|
| 1 | Exceptions swallowed: `inst_to_string` cleared `exception_occurred` before `run_spim` checked it (also REPL `step`); the old peek save/restore missed `first_bad_exception` | side-effect-free `mem_peek_inst`/`mem_peek` (`src/memory.c`) | `explain_parity` (same stdout and status at L0–L4), `repl_step_exception`; the `explain` case now expects exit 0 |
| 2 | Branch targets 4 bytes off; in delayed mode the delay slot overwrote the branch's snapshot | `branch_target`/`jump_target` as `run.c` computes them; `explain_save_snapshot`/`explain_restore_snapshot` around the delay slot | `explain_branch_targets` (with and without `-delayed_branches`) |
| 3–5 | `syscall` narrated as "continuation of li"; "first" twice; pseudo-ops behind labels and symbol-relative loads/stores missed | the assembler records each instruction's expansion (`expansion_of/index/count/of_pseudo` on `mips_instruction`), and `explain_after` narrates from it ("first of N", "k of N", "Expanded:") | `explain_pseudo`, the golden |
| 6 | Pseudo-op descriptions contradicted the emitted code | table corrected (b, bgt/ble/bge/blt immediate forms, la, li, rem/remu) | `explain_pseudo`, the golden |
| 7 | Wrong categories and glosses | Comparison and Constant categories; sltu's `u` is an unsigned compare; nop has no category | the golden |
| 8 | ori's immediate called signed at L3/L4 | andi/ori/xori/lui zero-extended at every level | the golden |
| 9 | Program exit never narrated | `explain_program_exit` at levels 1–4 ("The program exited with status N", with the 128+code note) | `explain_parity` |
| F10 | `jr.hb`/`jalr.hb` unexplained; `explain_before`'s false "stores nothing" | both tokens in both switches; sc and swl/swr snapshotted (swc1/sdc1 print no memory values, and the comment says so) | `tests/tt.explain.s` |

Side findings:
- **`-Dline_editing=disabled`:** eight tests failed, not two. The no-libedit REPL never pushed a
  scanner source, printed the prompt without a newline on a pipe, and `.` read the rest of stdin.
  Both builds now share one read loop (`read_command_line`), and the eight pass.
- **`-show-expansion` / `-print-ast`:** they print `op=<mnemonic>`, and a pseudo-op's children carry
  its line (`show_expansion` checks both).
- **The four level descriptions** (man page, usage text, `CLAUDE.md`, `pgu/docs/source/examplesap.rst`)
  match the code.

`tests/tt.explain.expected` and `tests/golden.ast_parity_all.txt` were regenerated, and the whole
diff was read. To keep the address shift from the added `.hb` lines out of it, the new binary was
first diffed on the original `tt.explain.s`.

**Ports** (plan step 4): the port suites skip the new `explain_*` cases, since the ports have no
teaching mode. Python, Haskell and Rust followed everything visible outside `-explain`: a
side-effect-free text peek in `inst_to_string` (REPL `step` runs the handler; no spurious IBE),
`op=<mnemonic>` in the AST text dumps, and the children's line numbers. The `step_exception` REPL
session (`port/harness/repl_corpus/`) caught the `step` change before the ports had it.

Left as found: syscalls still have no source line in `disasm`/listings, since a fix would change
listing output in every engine and the narration no longer depends on it; `rem` with an immediate
skips the zero check (the table describes the register form).

Gates: gcc (line editing on) and clang (off) regression + examples 86/86 each for the C work; after
the ports, `meson test` 340/340 (gcc) and clang regression 51/51; `dispatch_check` 0 flagged. The
image gate ran at the end of the batch branch.
