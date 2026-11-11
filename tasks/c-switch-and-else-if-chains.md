# Use `switch` for the C engine's dispatch chains, and `else if` for its guard runs

**Status:** in progress (branch `c-switch-and-else-if-chains`)
**Priority:** 6
**Difficulty:** 4
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Found by:** cross-referencing `tasks/archive/2026/11/11/python-port-match-statements.md` against
`src/`

## BLUF

The Python port now writes its dispatch chains as `match` with a `case _`, and its runs of early-exit
`if`s as single `if`/`elif`/`else` chains. This task applies the same two rules to the C engine
(`src/`), in C's spelling:
- a chain that compares one value against constants becomes a `switch` with a `default`;
- a run of separate early-exit `if`s that make one decision becomes one `if` / `else if` / `else`
  chain;
- an existing `switch` with no `default` gets one, except where the compiler already checks it is
  exhaustive.

Done means each site in the worklogs below is converted or left with a recorded reason, behavior is
unchanged, and `make image` passes.

## Context

- **The rules being carried over.** The Python standard's "Prefer `match` + `case _`" and "Mutually
  exclusive branches are one `if`/`elif`/`else` chain" (the python-coding-standard.md reference doc
  from github.com/billsix/runClaudeInContainer). The cross-project "Prefer total dispatch" rule in
  that repo's `CLAUDE.md` covers all languages: write the default branch, and where the language
  checks exhaustiveness (`-Wswitch`), let it. The C standard
  (`github.com/billsix/runClaudeInContainer:tasks/reference/c-coding-standard.md`) says nothing about
  `switch`, `default` or `else if` chains today (open question 5).
- **Scope.** `src/` only, which is the modernization scope in `CLAUDE.md`. `examples/` and `pgu/` C is
  teaching code and is excluded. Code under `#ifdef SPIM_BIGENDIAN` is not compiled, so the scan
  doesn't see it, and it can't be tested here (`tasks/big-endian-conversion-hardening.md` is
  blocked). Leave it alone.
- **The ports are unaffected.** This changes no behavior, so the port suites must stay green without
  edits. The Python port's same-named functions already use `match`/`elif` (cross-reference below).
- **Read first:** `tasks/archive/2026/11/11/python-port-match-statements.md` (how the Python
  conversion went, and the converter bugs it hit), and the worklogs below.

## Findings (2026-11-11)

All scripts and their output are in `tasks/adhoc/c-switch-and-else-if-chains/`:
- `scan_conditionals.py BUILD_DIR` reads clang's JSON AST of every `src/*.c`, so a test written
  through a macro is seen as the code it expands to. It writes `data/if_chains.tsv`,
  `data/guard_runs.tsv`, `data/switches.tsv` and `data/summary.txt`.
- `switch_warnings.sh BUILD_DIR` records clang's `-Wswitch-default` and `-Wswitch-enum` diagnostics
  in `data/switch_warnings.txt`. That is the compiler's own list, and it matches the scan's 21
  switches with no `default` exactly.
- `crossref_python.py` adds, for each C chain or run, the Python port's function of the same name
  and its `match`/`elif` counts (`data/crossref_python.tsv`; 69 of the 100 rows have a
  counterpart).

`BUILD_DIR` is any clang meson build: `CC=clang meson setup <dir>`. The scan found all 53 compiled
switches. `grep` finds 58; the other 5 are four in `run.c` under `#ifdef SPIM_BIGENDIAN`, and one
in a comment.

### 1. `if` / `else if` chains on one value: `switch` candidates (23 chains)

`data/if_chains.tsv`, shape `switch`: every test compares one subject with an integer, character or
enumerator constant. Only 3 have three or more tests:
- `src/instruction.c:1181` `inst_decode`: 5 tests on the opcode field (`SPECIAL`/`SPECIAL2`,
  `REGIMM`, `COP0`, `COP1`, `COPz`). There is no final `else`; a switch's `default` would say
  "other opcodes: the top 6 bits are the whole key".
- `src/spim.c:1013` `parse_spim_command`: `TOK_REG` / `TOK_FP_REG` / `TOK_INT` / `TOK_ID`, already
  ending in `else`.
- `src/explain.c:1623` `explain_decoding_steps`: `op` against `0x00 || 0x1c || 0x1f`, `0x01`,
  `0x02 || 0x03`, then `else`. The `||` lists become stacked `case` labels.

The other 20 have two tests. Seven of them list constants with `||` (`parse_j`'s three chains, and
`parse_asm_code`'s at `src/parser.c:1754`, which tests 8 trap and divide opcodes), so a switch's
stacked labels would read better there too. Eight of the 23 have no final `else`. Which count as
worth converting is open question 1.

### 2. Runs of early-exit `if`s: one decision written as separate statements (20 runs)

`data/guard_runs.tsv`: two or more consecutive `if`s with no `else`, each of whose bodies always
leaves (`return`, `break`, `continue`, `goto`, or a call to a function that never returns).
- **9 test one value**, so each could be a `switch` instead of an `else if` chain. Most are the
  recursive-descent parser's `scanner_peek()` dispatches: `parse_factor` (`TOK_INT`, `'('`,
  `TOK_ID`), `parse_address` (`'('`, `TOK_INT`, `TOK_ID`), `parse_abs_addr`, `parse_imm32` ×2 and
  `parse_line` ×2. The other two are `scan_one_token`'s whitespace and comment run, and its
  newline/`;` run (`src/scanner.c:524`, `:561`). The Python port's `parse_factor`, `parse_address`
  and `_scan_one_token` became `match` statements.
- **11 are mixed**, so they become `if` / `else if` chains. The largest is
  `src/scanner.c:580` `scan_one_token`: 8 tests, among them `c == '-' && isdigit(...)`,
  `isdigit(c)` and several character tests. `parse_r3` and `spim_completion` are among the rest.

### 3. Mixed chains with no final `else` (29 of 56)

`data/if_chains.tsv`, shape `mixed`, `has_else` `no`: chains whose tests compare different things.
They can't become a `switch`, but under the total-dispatch rule an open-ended chain should end in an
`else`: one that raises, does a documented fallback, or is a commented no-op. That applies only
where the chain is one decision; independent checks stay as they are. Each needs a per-site look.
The two long mixed chains already end in `else`:
- `main`'s 35-test argument parser (`src/spim.c:426`);
- `read_assembly_command`'s 23 tests (`src/spim.c:1327`).

### 4. `switch` statements with no `default` (21 of 53)

`data/switches.tsv` (has_else `no`, with the subject's type) and `data/switch_warnings.txt`:
- **7 switch on a real enum type** (`ast_kind` ×5: `ast_free`, `print_node`, `json_kind_header`,
  `json_node`, `emit_one`; `asm_segment` in `seg_name`; `asm_event_kind` in `listing_observer`).
  Each lists every member. `-Wswitch`, part of `-Wall` and fatal under `-Werror`, already fails the
  build when a member is added and not handled. **Adding a `default` would turn that check off**,
  so leave these as they are, with a one-line comment saying why.
- **5 switch on an opcode stored as `int`/`int16_t`** (`explain_before`, `explain_after`,
  `lookup_classification` with 60 labels, `r_cond_type_inst` with 32, `do_parse_pseudo`). The
  labels are `TOK_*` enumerators, but `include/tokens.h` declares an anonymous `enum : int32_t`,
  so the compiler can't check anything. Give each a `default` that says what happens to other
  opcodes (open question 4 asks about naming the token type).
- **4 switch on a small `int`** (`render_r_decode_step`, `render_i_decode_step`,
  `render_j_decode_step` and `render_regimm_decode_step`'s `step`) and **1 on a `char`**
  (`subst_field_regs`). These get a `default` too.
- **4 in `run_spim` switch on `addr & 0x3`** (`src/run.c:529`, `:582`, `:930`, `:980`: the
  little-endian `LWL`/`LWR`/`SWL`/`SWR` byte lanes). They list cases 0–3, which is every value, so a
  `default` can't run. C23's `unreachable()` (`<stddef.h>`) says that.

The two `-Wswitch-enum` reports (`src/parser.c:241`, `:344`) are `ast_kind` switches that have a
`default` and deliberately leave 21–22 kinds to it. They need nothing.

### 5. String dispatch (1 chain)

`src/spim.c:1440` `print_reg_from_string`: 8 `streq` tests on one string (`"pc"`, `"hi"`, …), ending
in `else`. C can't `switch` on a string. A name-to-handler table would be a separate refactor. Leave
it.

## Hazards (C-specific; check at every site)

- **A `break` changes meaning inside a `switch`.** A guard run whose bodies `break` out of an
  enclosing loop can't become a `switch` without rewriting those exits (to `goto` or a flag). Use
  `else if` there instead. `continue` and `return` are unaffected.
- **A `switch` evaluates its subject once.** The `if` runs call `scanner_peek()` once per test.
  Checked: `scanner_peek()` (`src/scanner.c`) only fills the one-token lookahead and returns its
  type, so one call gives the same result. Check any other subject that is a call.
- **Case labels must be integer constant expressions.** `TOK_*`, characters and literals are; a
  `const int` variable is not, in C. Check each one.
- **Fall-through is checked.** `-Wimplicit-fallthrough` (level 5 under gcc, `meson.build`) is on, so
  a missed `break` fails the build. Stacked labels with no statements between them are fine.
- **Keep comments.** Put each test's comment on its `case` label. The Python conversion lost comments
  four different ways before its tool was fixed. Converting by hand, or with a tool and a check
  that every comment survives, avoids that.

## Plan

1. Write the decisions on the open questions into this doc and commit.
2. §4 switches: the defaults, `unreachable()` and the enum comments. This is the smallest diff and
   gives the most direct exhaustiveness gain. Gate it.
3. §1 `switch` conversions, as decided in question 1. Gate them.
4. §2 runs: parser and scanner dispatches to `switch`, mixed runs to `else if`, with the tail placed
   as decided in question 2. Gate them.
5. §3 triage: add an `else` where the chain is one decision, and record the leave-alones here.
6. Each step: `clang-format`, gcc and clang `-Werror` builds, then `make image`, which covers the
   285 meson tests, the port suites and the UBSan/ASan regression runs.
7. Done-record, archive, `git rm` the adhoc scripts (or promote `scan_conditionals.py` to `tools/`
   if question 6 says so).

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. **One-value chains:** convert the 3 chains of 3+ tests and the 7 two-test chains whose tests list
   several constants with `||` (§1). A plain two-way test on one constant stays `if`/`else`.
2. **Guard runs:** the code after a converted run goes into the chain's final `else`, as in the
   Python port.
3. **The 7 exhaustive enum switches get a `default` too.** It calls `fatal_error` with a message
   naming the function and the bad value, and carries a comment saying it is never reached and why
   (every member is listed, and `-Wswitch-enum` checks that). A `default` turns off `-Wswitch`'s
   missing-member warning for that switch, so the core is built with **`-Wswitch-enum`**, which
   warns even when a `default` exists. That keeps the compile-time check and adds the run-time
   one. The two switches that deliberately leave many `ast_kind` members to their `default`
   (`src/parser.c:241`, `:344`) opt out of `-Wswitch-enum` at the site, with the reason.
4. **The token enum's name** is not part of this task. It gets its own proposed task.
5. **The shared C coding standard gets the rules**, as its own commit in runClaudeInContainer and
   runCrushInContainer (three copies, kept byte-identical).
6. **The scanner is promoted to `tools/`**, so new C can be checked, like `tools/const_candidates.py`.
