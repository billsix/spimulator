# Compared full REPL transcripts across the engines

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §6.1, §10.2 F7

## Summary

- **`port/harness/repl_corpus/`** holds 26 sessions, one `.in` file each. Together they cover every
  command in `src/spim.c`'s command table: args, breakpoint (by label and by address, a rejected
  one, and one that fires), continue, delete, disasm, dump, dumpnative, exit, help, list, load,
  print (registers including `$f33`/`$99`, memory, labels), print_all_regs (hex, FPU, and twice in a
  row), print_symbols, quit, read, reinitialize, run, step (including into an exception), the `.`
  command, redo on an empty line, end of input, and a last line with no newline.
- **`port/harness/compare_repl.py`** feeds each session to the C engine and a port, in a scratch
  directory with `tests/` linked in and an empty environment. It compares stdout, stderr (minus the
  `Loaded:` line), exit status and the files `dump` writes, using `compare_engines.py`'s comparison
  and float tolerance. A hung C run is a FAIL, and so is a selection that matches nothing. It is
  registered as `<port>-repl` in each port suite.
- **Results:** all three ports passed 26/26 against the committed C engine. The divergence this task
  expected (`print_all_regs` after `print_all_regs hex`) had already been fixed in C by
  `tasks/archive/2026/11/11/c-undefined-behavior-fixes-then-ports.md`; the `print_all_regs_twice`
  session pins it. The `step_exception` session then caught the REPL `step` change in
  `tasks/archive/2026/11/11/explain-mode-correctness-bugs.md` before the ports had it, as intended.
- The one-shot REPL differs `tasks/adhoc/engine-port-haskell/repl.py` and
  `tasks/adhoc/engine-port-python/diff_repl.sh` were removed. Their sessions are in the corpus, and
  the scripts remain in git history.
- `port/README.md`, `tasks/reference/engine-ports-design.md` and `port/harness/corpus.txt`'s header
  name the new comparison.
