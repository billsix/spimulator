# Use `switch` for the C engine's dispatch chains, and `else if` for its guard runs

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** spimulator `c-switch-and-else-if-chains`

## Outcome

The C engine (`src/`) follows the dispatch rules the Python port got in
`tasks/archive/2026/11/11/python-port-match-statements.md`, written in C's terms. The rules are now
in the shared C standard (the c-coding-standard.md reference doc from
github.com/billsix/runClaudeInContainer, "Dispatch").
- **Every `switch` has a `default`** (21 had none).
  - Where the cases cover every value, the `default` calls `fatal_error` with "internal error:
    <function>: ...", under a comment saying why it is never reached. That covers the 7 enum
    switches, the 4 decode-step renderers, `subst_field_regs`, `do_parse_pseudo`'s inner switch,
    and `run_spim`'s 4 byte-lane switches.
  - Where other values are legitimate, the `default` is a commented `break`: `explain_before`,
    `explain_after`, `lookup_classification`, `r_cond_type_inst`.
- **The core builds with `-Wswitch-enum`** (`meson.build`). A `default` turns off `-Wswitch`'s
  missing-member warning, and `-Wswitch-enum` warns even with one. Checked under gcc and clang by
  deleting a case in a scratch copy: both fail the build. The two `ast_kind` switches in `parser.c`
  that leave most kinds to their `default` opt out at the site with a pragma and the reason.
- **10 one-value chains are `switch` statements:** the 3 with 3+ tests, and 7 two-test chains that
  list several constants with `||`.
- **The 20 runs of early-exit `if`s are each one decision.** 3 became `switch` statements
  (`parse_factor`, `parse_address`, `scan_one_token`'s whitespace run). The other 17 became
  `if` / `else if` / `else` chains, with the code after the run in the final `else`. In `main`, the
  tail was already a chain, so the two joined into one.
- **Every `if` / `else if` chain ends in an `else`** (33 had none, counting `BRANCH_INST`'s 16
  expansions as 16).
  - 31 have a commented no-op naming the remaining outcome.
  - `store_instruction`'s `else` is fatal: `data_dir` and `text_dir` are always set as a pair.
  - **Left alone:** `run_spim`'s instruction-fetch chain (`src/run.c`). Every branch leaves, and
    the code after it is the rest of the dispatch loop, so nesting that one level deeper would read
    worse.
- **`tools/dispatch_candidates.py BUILDDIR`** (promoted from this task's scanner) checks new C. On
  this tree it lists only the leave-alone above. It is not wired into a gate.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. Convert one-value chains of 3+ tests, and two-test chains whose tests list several constants.
   A plain two-way test on one constant stays `if`/`else`. The same threshold was applied to the
   one-value guard runs.
2. The code after a converted guard run goes into the final `else`, as in the Python port.
3. Exhaustive enum switches get a `default` too, which errors and exits with a good message and a
   comment saying why it is never reached. `-Wswitch-enum` keeps the compile-time check. Where
   C23's `unreachable()` would also fit, the fatal message was used instead, so a broken invariant
   is diagnosed, not undefined.
4. Naming `include/tokens.h`'s anonymous token enum is out of scope:
   `tasks/name-the-token-enum.md`.
5. The shared C coding standard gets the rules: runClaudeInContainer and runCrushInContainer,
   branch `c-dispatch-rules` in each.
6. The scanner was promoted to `tools/`.

## Found along the way (both are bugs that predate this work; each has its own task)

- **`tasks/paired-single-compare-encoding.md`.** The 16 `c.cond.ps` rows of `include/opcodes.tsv`
  encode format field 0x13, where GNU `as` puts 0x16. The engine also can't execute them. Found
  because `r_cond_type_inst`'s "exhaustive" switch wasn't: those rows reach it with no case.
- **`tasks/parse-j-drops-cop2-and-jalr-hb.md`.** `cop2` and `jalr.hb` reach `parse_j` and assemble
  to nothing, with no error. Found while writing `parse_j`'s `default`.
- The oracle for both is GNU binutils for MIPS (`binutils-mips64-linux-gnu`), installed and added
  to both sandboxes' base lists. LLVM's `llvm-mc` doesn't support the paired-single compares.

## How

Scripts in `tasks/adhoc/c-switch-and-else-if-chains/`. The one-shot ones were removed when this task
was archived and are in its history.
- **Discovery:** `scan_conditionals.py` (now `tools/dispatch_candidates.py`) read clang's JSON AST,
  so macro-expanded tests counted. `switch_warnings.sh` recorded clang's `-Wswitch-default` /
  `-Wswitch-enum` list, which matched the scan's 21 switches without a default.
  `crossref_python.py` mapped each C site to the Python port's same-named function. The worklogs
  were in `data/`.
- **The scan's misses were understood, not ignored:** grep found 5 more `switch (` than the scan.
  Four are under `#ifdef SPIM_BIGENDIAN`, which isn't compiled; the fifth is in a comment.
- **Conversions:**
  - The switches and the defaults were written by hand, or by inline edits anchored on the exact
    old text.
  - 13 guard runs were rewritten by `guards_to_else_if.py`. It moves each comment between two
    guards into the branch it describes, and splices a tail that is itself one chain.
- **The `break` hazard was checked at each site.** In `parse_asm_code`'s `div` immediate path, the
  `break` that skips `free(imm)` now leaves the inner switch. It still skips the `free`, and the
  enclosing case ends in its own `break`.

## Mistakes made and fixed within the task

- **The first `guards_to_else_if.py` took any following `if` as a guard,** including ones with an
  `else` or a body that doesn't leave, and merged unrelated code in `parse_line`. The output was
  discarded and the files restored. The hand edits made in the same step were re-applied from saved
  scripts, and the fixed tool run once. Two more fixes followed before that final run: a
  block comment's second line wasn't recognized as a comment, and moved comments lost their
  relative indentation.
- **One comment landed in the wrong branch.** `decode_string`'s "Backslash sequence" comment went
  into the trailing-backslash branch; it was moved by hand.
- **The defaults script's first run failed** before writing anything: its line keys mixed the
  closing-brace and opening lines.
- **Formatter drift on master:** `clang-format` also rewrapped unrelated text in `instruction.c`
  and `parser.c`. Those hunks were restored each time. In `scanner.c`, the drift was in the
  `have_char:` block this task rewrote, so that block is formatter-clean now.

## Verification

- At each step: gcc and clang builds under `-Werror` with `-Wswitch-enum`, and the regression suite
  (36/36).
- **Against an unmodified master build:** `compare_engines.sh` ran both binaries on every `.s` and
  `.asm` in `tests/`, `examples/src/` and `pgu/src/`. It compared stdout, stderr and exit status for
  four modes (REPL `disasm`, `-print-ast-json`, `-listing`, `-explain=4`), plus a REPL session that
  covers every token kind the `print` and `args` commands dispatch on. After each of the three
  conversion steps: 447 identical, 6 skipped.
  - The skipped cases are where master's own output differs between two runs: programs that read
    the clock or run until the timeout. Each case runs the before binary twice to detect that.
- `make image` with the default flags passed: 285 meson tests OK (including the three port suites and the
  generated-table check), and the regression suite 36/36 under UBSan and under ASan.

## Follow-ups

- `tasks/c-dispatch-port-review.md` (proposed): whether the ports should follow any of this.
- `tasks/paired-single-compare-encoding.md`, `tasks/parse-j-drops-cop2-and-jalr-hb.md` (proposed).
- `tasks/name-the-token-enum.md` (proposed): a named token type, so switches on tokens can be
  checked.
