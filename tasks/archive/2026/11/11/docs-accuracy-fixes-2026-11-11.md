# Fixed the doc claims the 2026-11-11 review found false

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §1.2, §1.3, §1.4, §2.3, §2.4, §2.5, §2.6 F18

## Summary

Each claim below was corrected to match the tree; no code changed.

- **Gate wording** (`CLAUDE.md`, `tasks/reference/simulator-integer-types.md`, the `meson.build`
  comment): the image build gates `-Werror` and the conversion flags under clang only; gcc is checked
  by hand. `tools/const_candidates.py` is a manual check; `tools/c_dispatch_check.py` runs as the
  meson test `dispatch_check`.
- **`tasks/reference/engine-port-language-choice.md`**: names `port/harness/compare_engines.py` and
  `compare_all_programs.py` instead of a planned `tools/diff_engines.py`; the duplicate-encoding
  notes describe the decode column, not glibc's `qsort`/`bsearch`; `,` is no longer listed as an FP
  separator; `opcodes.h` became "the generated opcode tables"; the stale "25 tests" count was dropped.
  The 158,029 and 58,028 double counts were both correct (two different value sets); the Rust line now
  says so.
- **`tasks/reference/engine-ports-design.md`**: cites `entrypoint/05-install-ports.sh` and its full
  package list. (The case count had already been updated by an earlier task in this batch.)
- **`tasks/reference/opcode-table-generator.md`**: the deleted `check_decode.c` is described as living
  in git history before the archive of `tasks/archive/2026/11/11/opcodes-x-macro-redesign.md`.
- **Stale pointers**: `calc-incremental-versions.md`, `incremental-example-decomposition.md`,
  `inherited-idiosyncrasies.md` and `mips-asm-teaching-style.md` point at their archived tasks;
  `port/harness/corpus.txt` points at `tasks/engine-ports-teaching-mode.md`.
- **`README.md`**: the Windows claim was dropped; the copyright cites the first commit `e2d4f30`
  (the maintainer's decision, option (b)); the run examples use the root `helloworld.s`, which was
  run and prints "Hello World".
- **`port/README.md`** already listed every comparison (fixed earlier in this batch).
- **`tasks/reference/mips-fpu-and-float-demos.md`**: the "exit2 status is not propagated"
  observation was false. A three-line exit2 program with `$a0 = 3` exits with status 3, so the
  section now says asm and C give the same status.
