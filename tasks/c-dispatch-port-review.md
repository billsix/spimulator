# Review the C dispatch changes for anything the Python, Haskell and Rust ports should follow

**Status:** proposed — needs go-ahead
**Priority:** 6
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Follows:** `tasks/archive/2026/11/11/c-switch-and-else-if-chains.md` (branch
`c-switch-and-else-if-chains`)

## BLUF

The `c-switch-and-else-if-chains` branch rewrote the C engine's dispatch code. Every `switch` got a
`default`, every chain an `else`, guard runs became chains or switches, and the core now builds
with `-Wswitch-enum`. None of this changed behavior: the port suites pass unchanged. This task
checks each change against the three ports, to see whether a port should follow it for its own
sake or to keep reading side by side with C. Done means a per-port list of changes made (each a
commit, with the port suites green) and changes not needed (each with a reason).

## Context

- **The C engine is the standard** (`CLAUDE.md`, "port/"). Every port must match its stdout, stderr
  and exit status, so a behavior change would already have failed the port suites. This review is
  about structure and invariants, not output.
- **What already lines up:**
  - The Python port got `match` statements with `case _` and `if`/`elif`/`else` chains first
    (`tasks/archive/2026/11/11/python-port-match-statements.md`), and the C work copied its rules.
  - Rust's `match` is exhaustive by the compiler.
  - Haskell checks incomplete patterns under `-Wall -Werror`, and its guards end in `otherwise` by
    the Haskell standard (the haskell-coding-standard.md reference doc from
    github.com/billsix/runClaudeInContainer).

## What to check, per C change (commits on the branch, oldest first)

1. **The fatal defaults** (commit "c: every switch has a default; -Wswitch-enum keeps the
   exhaustiveness check"). Where C's cases cover every value (the `ast_kind` switches, the
   decode-step renderers, `subst_field_regs`, `do_parse_pseudo`'s inner switch, `run_spim`'s four
   byte-lane switches, `seg_name`, `listing_observer`), the `default` calls `fatal_error` with an
   "internal error: <function>: ..." message.
   - **Python:** each counterpart's `case _` should raise with a message naming the function and
     the value, not pass silently.
   - **Rust:** the counterpart should be an exhaustive `match` on an enum, or end in
     `unreachable!("…")` with the same message.
   - **Haskell:** an exhaustive `case`, or an `error` call naming the function.
2. **The legitimate defaults:** `explain_before`, `explain_after`, `lookup_classification`,
   `r_cond_type_inst`, and `parse_j`'s three switches. Check that each port's counterpart is also a
   documented no-op, and that it doesn't raise where C does nothing.
3. **The one-value chains that became switches**, and **the guard runs that became chains**
   (commits "one-value dispatch chains are switch statements" and "runs of early-exit ifs …").
   - The functions: `explain_bit_layout`, `explain_decoding_steps`, `inst_decode`, `parse_j`,
     `parse_asm_code`'s `RS_RT_INST` dispatch, the print and args REPL commands, `parse_factor`,
     `parse_address`, `parse_abs_addr`, `parse_imm32`, `parse_r3`, `parse_line`, `scan_one_token`,
     `scan_register`, `decode_string`, `check_keyword`, `fill_line_buf`, `ast_file_append`,
     `ast_pseudo_append`, `spim_completion`, and `main`'s load-file chain.
   - **Python:** mostly done already. Confirm with `tasks/adhoc/` worklogs or a grep.
   - **Rust and Haskell:** find the same runs of early returns and decide per site.
4. **The explicit `else` branches** (commit "every if/else-if chain ends in an else"), including the
   `BRANCH_INST` macro's not-taken case and `store_instruction`'s fatal `else`. Check each port's
   counterpart for an open-ended chain.
5. **`-Wswitch-enum`'s equivalent.** Python: a type checker's exhaustiveness check on `match` over
   an enum (`assert_never`), if `ty` supports it. Rust and Haskell have it already. Record what each
   port's gate checks.
6. **Not this task** (each port follows the fix when it lands):
   - `tasks/parse-j-drops-cop2-and-jalr-hb.md`: `cop2` and `jalr.hb` assemble to nothing.
   - `tasks/paired-single-compare-encoding.md`: the `c.cond.ps` encodings.

## How

- `tools/dispatch_candidates.py BUILDDIR` lists the C sites, which is the checklist's source. Each
  port's counterpart has the same function name, or a leading `_` in Python.
  The task's one-shot `crossref_python.py` mapped C functions to Python ones. It was removed when
  the task was archived; `git log --all -- tasks/adhoc/c-switch-and-else-if-chains/` finds it.
- Gate: `make image`; the port suites are `port-python`, `port-haskell`, `port-rust`.
