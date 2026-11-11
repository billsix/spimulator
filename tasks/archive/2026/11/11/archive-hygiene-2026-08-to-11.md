# Rewrote 13 archived tasks into done-records; repointed dangling paths; removed one-shot scripts

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Source:** tasks/review/2026/11/11/repo-review.md §10.1, §10.2, §10.3, §10.4, §10.6 F13

## Summary

- **Done-record rewrites** (13 docs, 1,598 → about 810 lines): `fix-inherited-idiosyncrasies`,
  `integer-conversion-hardening` (2026/10/16); `calc-incremental-versions`,
  `examples-separate-io-from-algorithm`, `separate-io-from-algorithm-remaining-demos`,
  `incremental-example-versions` (2026/10/30); `examples-makefile-debug-build`,
  `asm-simplification-sweep`, `asm-simplification-sweep-step-1-pilot-and-style-doc` (2026/10/31);
  `trim-claude-md` (2026/09/14); `libstr`, `calc-language` (2026/09/02);
  `fix-examples-listing-std-flag` (2026/10/28). Items a plan listed but nobody did are now recorded
  as not done, with the reason: the five superseded items in `fix-inherited-idiosyncrasies`, the
  `long addr` field in `integer-conversion-hardening`, the shared `calc_eval.c` that was never made,
  `libstr`'s deferred `strlen` consolidation, and the step-1 pilot's image gate, which was never
  recorded as run. `examples-makefile-debug-build` now gives the `DEBUG_FLAGS` that shipped
  (`examples/src/Makefile`). A fresh subagent did the rewrites with git forbidden, and the
  coordinator checked the two SHAs it cites (`fb24ee2`, `8f0f38a`).
- **Dangling and relative paths** were repointed to their `tasks/archive/...` locations
  (`engine-port-language-investigation`, `engine-port-prereq-c-fixes`, `engine-ports`,
  `parser-leak-cleanup`, plus the ones inside the rewritten docs).
- **Dated notes** say where each follow-up went: `engine-ports` (Haskell registered),
  `engine-port-python` (install script and full-tree comparison done afterwards),
  `engine-port-haskell` (the Decisions supersede its planned answers), `paired-single-compare-encoding`
  (the image gate passed, 293/293), `python-port-match-statements` (the adhoc directory's real name),
  `add-dockerignore` (MELPA step removed the same day, `b6e476e`), `c23-modernization-pass3`
  (`expand_k_data` fixed; the string macros done in `aebc787`), `wc-add-golden-test-coverage` (stale
  Status).
- **One-shot scripts:** six were removed with `git rm`: `engine-port-python/{add_port_lines.py,diff_engines.py}`,
  `engine-port-haskell/differential.py` and `engine-port-rust/{diff_more.py,run-demos.sh,run-regression.sh}`.
  The two REPL ones (`tasks/adhoc/engine-port-python/diff_repl.sh`,
  `tasks/adhoc/engine-port-haskell/repl.py`) stay until `tasks/archive/2026/11/11/repl-transcript-corpus.md` replaces them.
- **Path check:** every backticked path the rewritten docs cite exists, or is described as deleted,
  external or a build output. Archived records that cite the removed one-shot scripts keep those
  paths as history.
