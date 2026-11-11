# Repository review — 2026-11-11 (the past days' work and the three engine ports)

**What this is.** A deep review, using the method in the repo-review-conventions.md reference doc
from github.com/billsix/runClaudeInContainer. Its focus, set by the maintainer, is the work done
since 2026-11-09:
- the engine ports to Python, Haskell and Rust, and their harness;
- the opcode-table generator and its outputs;
- the C23 `const` pass;
- the Haskell annotate-every-binding work and its checker;
- the Python `match`/`elif` conversion;
- the C `switch`/`else if` dispatch work, `-Wswitch-enum` and the vendored `tools/c_dispatch_check.py`;
- the `jalr.hb`/`jr.hb`/`cop2` fix and the `c.cond.ps` removal, in all four engines.

The three ports in `port/` get particular attention: correctness against the C engine, test
coverage, docs, and code quality against each language's shared coding standard.

**Requested by** William Emerison Six <billsix@gmail.com>, 2026-11-11.

**Snapshot warning.** This record is frozen as of commit `d098b12` (master, merge of
`dispatch-follow-ups`). Line numbers rot; re-check a finding against the tree before acting on it.

**Baseline:** `tasks/review/2026/11/10/repo-review.md` (simulator core + full git history). Its
carry-over is below.

**Labels.**
- **VERIFIED:** checked by reading code or running a command.
- **MAIN-VERIFIED:** the main thread re-ran the evidence shown.
- **LEAD:** plausible, not confirmed.
- **Severity:** HIGH means a wrong value, a crash on valid input, or a gate that silently doesn't
  test what it claims; MEDIUM is latent or misleading; LOW is cosmetic or a convention issue.

## Coverage

| Dimension | What was read | By |
|---|---|---|
| 1. Docs ↔ docs | `CLAUDE.md`, `README.md`, `port/README.md`, `src/README`, `tasks/README.md`, 8 of 12 reference docs in full and 4 curriculum reference docs partly | reader (docs) |
| 2. Docs ↔ code | Every backticked path and SHA in those docs, by script; gate claims, `make` targets, meson tests; `Documentation/spim.1` against the flag parser; `README.md` claims | reader (docs) + main thread |
| 3. Product content | User-facing docs only: `README.md`, `port/README.md`, `Documentation/spim.1`, and the ports' `-explain` refusal. The `examples/` curriculum and the `pgu/` book were **not read** this time; they are outside the focus | main thread |
| 4. Code quality | C: the whole diff `20ad101..HEAD` of `src/`, `include/`, `meson.build` and `tests/run-test.sh`, plus every changed function in full. Python: most modules in full, the rest partly (listed in §4.2). Haskell: Run, Syscall, Memory, Output and Assemble in full, the rest partly. Rust: 13 of 19 modules in full, the rest partly. Each port reader ran ~25–40 differential programs against the C engine | 4 readers; the main thread re-ran every HIGH |
| 5. Generators and tools | Every tracked file in `tools/`, `port/harness/*`, `port/meson.build`, the test sections of `meson.build` and `examples/src/meson.build`, and the Dockerfile's check stage, all read in full. Generated outputs sampled. `gen_opcodes.py --check` run, plus a mutation copy | reader (tools) |
| 6. Tests | `tests/run-test.sh`, `examples/tests/run-demo.sh`, `port/*/tests/*` and the harness, in full. The coverage gaps are ranked | reader (tools) + port readers |
| 7. Proofs | N/A (none) | — |
| 8. Build | `Dockerfile`, `Makefile` and `meson.build` in full, checked against the container-template checklist; the port launchers | main thread + reader (tools) |
| 9. Open-task backlog | All 19 open tasks: 9 in full, 10 by their header plus a grep | reader (docs) |
| 10. Archive hygiene | `tasks/archive/2026/08`–`2026/11` in full (53 docs). Every cited path checked with `ls`, and every SHA with `cat-file` and reachability from master. The older months were covered by the baseline | reader (archive) |

## 1. Docs ↔ docs

1.1 **MEDIUM — `tasks/README.md:111-175`, the "Ordering & dependencies (reviewed 2026-07-07)"
section.** `CLAUDE.md` sends readers here, but the section is mostly about archived work:
parser-leak-cleanup, libstr, c23-modernization-pass2, the four engine-port tasks, and others.
- Lines 168-175 say to log work in `ChangeLog`, which was last touched in May 2026.
- The status-marker table at :101-105 doesn't match the statuses in use (`proposed`, `blocked`) and
  says nothing about Priority/Difficulty.

VERIFIED.

1.2 **MEDIUM — `tasks/reference/engine-port-language-choice.md` keeps superseded present-tense
claims.**
- :136 still plans a `tools/diff_engines.py`; `port/harness/compare_engines.py` now exists.
- :218-221 and :256-257 say the ports must reproduce glibc `qsort`/`bsearch`, which the generated
  decode column now settles.
- :96 lists `,` as an FP decimal separator, which the prerequisite fixes dropped.
- :99 cites `opcodes.h`.
- :120 says "25 tests".
- :201 and :291 say float formatting is "checked by" `check-format.sh`, `check-strtod.sh` and
  `cargo test`. No gate runs any of them (§5.2).
- Its two double counts disagree (158,029 vs 58,028).

VERIFIED.

1.3 **LOW — numbers and paths that disagree.**
- `tasks/reference/engine-ports-design.md:51` names `entrypoint/0N-install-ports.sh`; the file is
  `05-install-ports.sh`.
- `engine-ports-design.md:82` says 23 harness cases; `corpus.txt` has 24.
- `tasks/reference/opcode-table-generator.md:53` and `:145` cite
  `tasks/adhoc/opcodes-x-macro-redesign/check_decode.c`, which was `git rm`'d at archive.

VERIFIED.

1.4 **LOW — stale or non-repo-root pointers in reference docs.**
- `calc-incremental-versions.md:4` and `incremental-example-decomposition.md:120` point at the
  archived `tasks/calc-incremental-versions.md`.
- `inherited-idiosyncrasies.md:7` uses `tasks/archive/.../`.
- Bare `*.md` names: `mips-asm-teaching-style.md:27`, and `ast-column-tracking.md:6`,
  `multi-file-load.md:39,62` and `mini-c-compiler.md:236` in open tasks.
- `port/harness/corpus.txt:14` points at the archived `tasks/engine-ports.md`.

VERIFIED.

1.5 **LOW — convention issues.**
- Bare "Bill" stamps in `string-equality-audit`, `mini-c-compiler`, `container-cross-env`,
  `multi-file-load`, `timing-model` and `pgu-build-matrix`, and in `entrypoint/format.sh`.
- History narration in the reference docs: `opcode-table-generator` "History", `parser-ast-emit`
  "History", and `inherited-idiosyncrasies`, which is all FIXED items and reads as a done-record.
- `CLAUDE.md:160`: "rollout is COMPLETE".

VERIFIED.

## 2. Docs ↔ code

2.1 **HIGH — the documented way to change an opcode can't work.** `README.md:69-72`, `CLAUDE.md`
(Conventions, "make generate") and `opcode-table-generator.md:174` all say: edit
`include/opcodes.tsv`, then run `make generate`.
- `generate:` depends on `image` (`Makefile:110`).
- The image build runs `python3 tools/gen_opcodes.py --check` (`Dockerfile:113`) on the edited
  `.tsv` against the old committed tables.
- So the image build fails before the generator runs. `format:` and `shell:` also depend on
  `image`, so they fail the same way.
- MAIN-VERIFIED by reading: the `image` recipe (`Makefile:47-56`) passes no way to skip the check.
  The 2026-11-11 table changes worked around it by running `python3 tools/gen_opcodes.py` directly,
  without noticing.
- Fix: let `generate` run the generator without first rebuilding the checked image (a small
  generator stage or `--target`, or run on the existing image), and document the host fallback.

2.2 **MEDIUM — `Documentation/spim.1` (baseline F9, still open).**
- It documents a `-execute` flag that `src/spim.c` doesn't accept.
- It omits `-exception_file`, `-trap_file`, `-pseudo`, `-dump`, `-full_dump` and `-assemble`.
- Its SYNTAX names the program `spim`, and it installs as `spim.1` (`meson.build:128`) for a binary
  called `spimulator`, so `man spimulator` finds nothing.
- It is unchanged since before the baseline (`git log -- Documentation/spim.1`).

MAIN-VERIFIED (a grep of each flag in both files).

2.3 **MEDIUM — gate wording overstates what runs.**
- `CLAUDE.md` (the `-Werror` and conversion bullets), `tasks/reference/simulator-integer-types.md:225`
  and `meson.build:102` say the core is "gated" under gcc and clang. Every build in the image is
  `CC=clang` (`Dockerfile:119`, `:153`, `:160`), so gcc is a manual check.
- `CLAUDE.md` says new C "is checked with" `tools/c_dispatch_check.py` (which "must exit 0") and
  `tools/const_candidates.py`. No gate runs either one, and the dispatch check exits 1 today (§4.1).

VERIFIED.

2.4 **LOW — `README.md`.**
- :4 says "Works on Windows". Nothing in `meson.build` or the docs supports a Windows build.
- :80 dates the copyright from commit `e10b97408f6d…`, which doesn't resolve in this repository
  (`git cat-file` fails). The baseline §1.1 identifies the upstream baseline as `6cb6a08`.
- :37 uses a placeholder `01-helloworld.asm`; the repo ships `helloworld.s`.

MAIN-VERIFIED.

2.5 **LOW — `port/README.md:29-31`** leaves out the `<port>-all-programs` comparison
(`compare_all_programs.py`) and `haskell-annotations` from what `port/meson.build` registers.
VERIFIED.

2.6 **LOW — `tasks/reference/mips-fpu-and-float-demos.md:112-121`** says exit2's status isn't
propagated. `syscall.c` now sets `spim_return_value` from `$a0`, and the `return_value` test exists.
LEAD.

## 3. Product content (user-facing docs)

3.1 The ports' refusal of `-explain` is clear and consistent. All three print
"spimulator-<port>: the teaching mode (-explain) is not supported by this port" and exit 2, as
`port/README.md` says. MAIN-VERIFIED.

3.2 The rest is covered by §2.2 (the man page) and §2.4 (the README). The `examples/` curriculum and
the `pgu/` book were outside this review's focus and were not read.

## 4. Code quality

### 4.1 The C engine (changes since the baseline)

No HIGH. Every new "never reached" claim was checked against its callers and the table row types,
and each holds:
- the four LWL/LWR/SWL/SWR byte lanes;
- `subst_field_regs` and the four decode-step renderers;
- `store_instruction`, `r_cond_type_inst`'s 32 compares, and `parse_j`'s defaults against the
  RD_RS/J_TYPE rows;
- the set-compare pseudo-ops;
- the AST, segment and event switches.

Every converted switch kept its `break` targets.

- **4.1.1 MEDIUM — the dispatch check fails: `src/parser.c:943` (`parse_j`).**
  `python3 tools/c_dispatch_check.py BUILDDIR src` exits 1 and flags
  `guard-run parse_j 3 ifs on op | scanner_peek()`.
  - The cause is the `cop2` early return added in `625b30b`, two minutes after the checker was
    vendored in `2452136`.
  - Nothing caught it because no gate runs the checker (§2.3).
  - Two readers found it independently. MAIN-VERIFIED (rc=1, the output above).
- **4.1.2 MEDIUM — the teaching mode doesn't know `jr.hb`/`jalr.hb`.** `src/explain.c`
  `lookup_classification` and `render_dispatch` have no case for `TOK_JR_HB_OPCODE` or
  `TOK_JALR_HB_OPCODE`. Under `-explain=2`, `jr.hb $t0` prints "(no detailed explanation for this
  opcode yet)", while `jr` gets a category and a full explanation. MAIN-VERIFIED.
- **4.1.3 MEDIUM — the decode column fixes the wrong answer for three duplicate encodings
  (baseline §3.9 / F8).** In `include/opcodes.tsv`, 0x46000008/09/0f decode to `swxc1`/`sdxc1`/`prefx`.
  - The real COP1 instructions at those encodings are `round.l.s`/`trunc.l.s`/`floor.w.s`.
  - A `floor.w.s` word stored into text executes as "Unknown instruction type", a fatal error.
  - The behavior predates this work, but the `*` column now writes the choice down on purpose.
  - The COP1X (0x4c) decode still never ORs in funct.

  VERIFIED.
- **4.1.4 LOW — `src/explain.c` `explain_before`'s new `default` comment is false.** It says "Any
  other opcode stores nothing", but swl, swr, sc, swc1 and sdc1 store. That is baseline §3.26,
  still open. VERIFIED.
- **4.1.5 LOW — two pragma'd switches keep a silent fallback.** `data_int_finalize` and
  `emit_dir_seg` (`src/parser.c`) opt out of `-Wswitch-enum` but keep a fallback `default` that
  builds a word/data node. The repo's stated convention (`meson.build`) is a fatal default where
  the cases cover every reachable value. VERIFIED.
- **4.1.6 LEAD — `src/symbol-table.c` `resolve_a_label_sub`.** `byte_offset / BYTES_PER_WORD`
  truncates toward zero where the old `>> 2` rounded down. Results differ only for a negative byte
  offset that isn't a multiple of 4 (`b label+2`).
- **4.1.7 LEAD (C bugs the port readers found).**
  - `print_all_regs` after `print_all_regs hex` leaves three stale lines: `ss_clear` rewinds the
    `open_memstream` without truncating it. The Haskell port's output is the correct one.
  - `-skt 0` loops forever on "Invalid address (0x80000180)", in C and in the ports.
  - `ld $31` and `mfc1.d`/`mtc1.d` with register 31 write `gpr[32]` (undefined behavior). The fix
    belongs in C first, by rejecting the odd or last register for the pair forms.

### 4.2 The ports — crashes on valid programs (cross-port)

Each of these is a valid program on which C exits cleanly or with its own fatal message, while a
port crashes. The main thread re-ran each one on all four engines.

| # | Input | C | Python | Haskell | Rust |
|---|---|---|---|---|---|
| 4.2.1 HIGH | `li $a0,0x7ffff000; li $v0,9; syscall; lw $a0,0x10000000` (a ~2 GB sbrk) | 255, "realloc failed in expand_data" | 1, `ValueError: negative count` (`memory.py:106-107`) | 1, `Ix{Int}.index` out of range (`Memory.hs:113-129`); other sizes pass silently | 101, panic (`memory.rs:241-291`), release too |
| 4.2.2 HIGH | close fd 1 (syscall 16), then print_string | 0 | 1, `OSError: [Errno 9]` (`console.py:43-55`) | 1 | 0 |
| 4.2.3 HIGH | 400 chained `j` under `-delayed_branches` | 0 | 1, `RecursionError` (`run.py:220-226`) | 0 | 0 |
| 4.2.4 MEDIUM | `ld $31, 0($t0)` (C: undefined behavior, see §4.1.7) | 0 | `IndexError` (`parser.py:790-822`) | — | — |

The fix for 4.2.1 in each port is to treat a negative new size as C does: `fatal_error` with C's
message ("realloc failed in expand_data", "… expand_k_data", and "Out of memory at request for N
bytes." for the stack).

### 4.3 Python port

- **4.3.1 MEDIUM — `-assemble` output differs** when the data segment doesn't end on a word
  boundary (`spim.py:420-425`). C's separator test leaves a trailing `", "`, which is a C bug worth
  fixing in C first. VERIFIED.
- **4.3.2 MEDIUM — `.align N` for N ≥ 32** differs (`data.py:67-72`). C's `1u << 40` is undefined
  behavior, so range-check `.align` in C (0..31) and mirror it. This is baseline §3.14. VERIFIED.
- **4.3.3 MEDIUM — three `match` statements are guards only** (`case _ if scanner.peek() == TOK_INT:`).
  They are `parser.py:287` (`_parse_factor`), `parser.py:397` (`_parse_address`) and `scanner.py:471`.
  The Python standard calls this an if/elif chain in disguise. The cause is that `TOK_*` are bare
  names, which `match` would treat as captures; match on `opcodes.TOK_INT` instead. VERIFIED.
- **4.3.4 LOW — history-narrating comments.** `run.py:957` and `instruction.py:622` say
  "nothing to do, as before". VERIFIED.
- **4.3.5 LOW — open-ended dispatch the match work missed.**
  - `_emit_one` (`parser.py:1313`) has no `case _`.
  - if/elif chains with no `else` at `ast_nodes.py:126`, `symbol_table.py:249` and `spim.py:829`.
  - Catch-all branches that stand for one specific member: `parser.py:941`, `parser.py:1011`,
    `run.py:403-415` and `run.py:822-833`.

  VERIFIED.
- **4.3.6 LOW — behavior picked by string slicing.** `_executes(*names: str)` and
  `run.py:684-1072` choose behavior by slicing mnemonic text, and REPL `Command` values are typed
  `int` (the standard's "options are enums"). VERIFIED.
- **4.3.7 LOW — dead code.** `options.force_break` is only ever written, and `Kind.ERROR` is never
  constructed. Ten `noqa` comments name rules that aren't selected (`ruff --select RUF100`).
  VERIFIED.
- **4.3.8 LOW — layering.** Function-local imports break import cycles, and `assert` is used as a
  type guard, which `python -O` strips (`parser.py:955,1395-1399`, `scanner.py`). VERIFIED.
- **4.3.9 LEAD.** `-stext -5` loops; a batch Ctrl-C prints a traceback; a raise from the signal
  handler can half-update state.

### 4.4 Haskell port

- **4.4.1 HIGH — HI/LO pile up unevaluated thunks** (`State.hs:46` `set = writeIORef`, with
  `Run.hs:252-262`). A `madd`/`msub` loop that never reads HI/LO keeps growing a thunk chain.
  - MAIN-VERIFIED: 3,000,000 `madd` iterations peaked at **488 MB RSS** in Haskell (sampled, so a
    lower bound) and **4 MB** in C. The reader saw 910 MB with its probe.
  - A longer loop exhausts memory or the stack.
  - Fix: a strict `set` (`writeIORef r $! v`), which covers every long-lived IORef. The Haskell
    standard's "strict where state accumulates" rule names exactly this.
- **4.4.2 MEDIUM — sbrk is quadratic** (`Memory.hs:104-109` `growBytes`). Every call copies the
  whole segment one boxed element at a time: 20,000 `sbrk(16)` calls take 0.04 s in C and 8.9 s in
  Haskell. VERIFIED.
- **4.4.3 LOW — `tools/BindingTypes.hs:172-186` is stricter than the standard.** A list
  comprehension is also an `HsDo` node, so its generators are reported as do-binds, which the
  standard exempts. VERIFIED with a probe module. The checker has no vacuity hole: 23/23 modules
  produce `.hie` files. But an empty `hie` directory would pass with 0 findings, so a count guard
  would close that.
- **4.4.4 LOW — polymorphic local signatures used at one type**
  (`Run.hs:259,261,290,620`, `Memory.hs:82,376,533`). The standard prefers concrete types for these.
  VERIFIED.
- **4.4.5 LOW — partial functions.** `!!` on `argv`, `regNames` and hex digits, which is guarded
  but partial in form; `last`/`init` in `CFormat.hs:64`. `DisplayUtils.hs:147-150` `partialLine`
  doesn't terminate for an unaligned address (LEAD).
- **4.4.6 LOW — the FP-compare lists sit in the hot path.** `Run.hs:513-520` checks them with
  `elem` mid-`case`. Also some duplication (`expandData`/`expandKData`), and a magic `0xfe800000`.
  VERIFIED.
- **4.4.7 LEAD.**
  - `Main.hs:470` reduces non-ASCII REPL input mod 256 on a tty.
  - `toInt32` hard-codes x86's 0x80000000.
  - `jump` evaluates its target before the delay slot (an edge across a 256 MB boundary, or `eret`
    when the slot writes EPC).

### 4.5 Rust port

- **4.5.1 MEDIUM — `+ 1` on an unbounded FP register number.** `parser.rs:804,823`, and `li.d`,
  `mfc1.d` and `mtc1.d`. `$f2147483647` makes `+ 1` overflow, which panics in a debug build. Fix:
  `wrapping_add`. VERIFIED.
- **4.5.2 MEDIUM — `steps -= 1` in `spim_utils.rs:227`.** `step -2147483648` makes a debug build
  panic. Fix: `wrapping_sub`. VERIFIED.
- **4.5.3 LOW — unannotated bindings, against the house rule.** `cli.rs:560,889`, `machine.rs:303`,
  `scanner.rs:551`, `parser.rs:183`, `instruction.rs:608,722,835,888,906,920`,
  `spim_utils.rs:32,245`. VERIFIED.
- **4.5.4 LOW — `_ =>` covering a named case.** In `run.rs:598` the `_` is really TLTU/TLTIU; name
  them and make `_` fatal, as C does. The lane arms in `run.rs:627-659` and
  `memory.rs:404,454,470` are total over `& 3`, which is acceptable. VERIFIED.
- **4.5.5 LOW — dead public functions.** `Machine::report` (`parser.rs:1757`) and `data_bot`
  (`data.rs:218`); rustc doesn't warn because they are public. VERIFIED.
- **4.5.6 LOW — divergences where C itself is undefined.**
  - `print $f4294967294` (`cli.rs:539-551`).
  - A text-segment address passed to read/write/print_string.

  The `unsafe` blocks in `console.rs` each carry a correct `// SAFETY:`. VERIFIED.

### 4.6 What the three port readers found correct

The recent changes match C case for case in every port: `parse_j`, the `.hb` run cases,
`r_cond_type_inst`, `store_instruction`'s fatal branch, and Python's `assert_never` sites.

Each reader's differential programs also matched C byte for byte in these areas: FP printing
(special and subnormal values), exceptions and their exit status, the read syscalls,
delayed branches and loads, lwl/lwr/swl/swr, text-segment byte and halfword accesses, the data
directives, `-dump`/`-assemble`/`-print-ast*`/`-show-expansion`, CLI flag errors, and most REPL
commands. The exceptions are the ones listed above.

## 5. Generators and tools

5.1 **HIGH — the harness's float tolerance passes real value errors**
(`port/harness/compare_engines.py:118-140`, `last_digit_unit`/`close`, also used by
`compare_all_programs.py`). C prints `%g`, which drops trailing zeros, so "one unit in the last
printed digit" can be a large tolerance.
- MAIN-VERIFIED with `compare_text("stdout", want, got)`:
  - `0.5`→`0.6` gives WARN;
  - `1e+10`→`2e+10` gives WARN;
  - `3.14159`→`3.1416` gives WARN;
  - `0.5`→`0.7` gives FAIL.
- A WARN passes the gate, so a 20% or 2× arithmetic error in a port passes.
- Fix: tolerate only when both strings parse to doubles within a few ULP (or 1e-15 relative).

5.2 **HIGH — nothing gates the ports' float formatters.**
- `port/haskell/tests/check-format.sh` and `check-strtod.sh` aren't registered in meson; only
  `port/haskell/README.md` mentions them.
- Nothing runs `cargo test`, so `port/rust/tests/cformat_vs_c.rs` and the `cformat.rs` doctests never
  run.
- The Python `cformat.py` has no oracle test.

Combined with §5.1, port float output is effectively ungated, although
`tasks/reference/engine-port-language-choice.md` says these checks exist (§1.2). VERIFIED.

5.3 **MEDIUM — no gate runs the ports' tier-(a) tools.** No ruff, `ty`, `check_local_annotations`,
clippy, `cargo fmt`, `cargo test` or `hlint` in any `meson.build`, `Makefile`, `Dockerfile` or
entrypoint script.
- The Rust release build doesn't deny warnings, and has overflow checks off, so the debug-only
  panics in §4.5.1–4.5.2 are invisible.
- Haskell does build with `-Wall -Werror` (`port/haskell/build.sh:17`).
- Run by hand today, ruff, `ty`, the annotation checker, clippy and `cargo test` all pass.

MAIN-VERIFIED (grep). Fix: a `port-lint` meson test per port (or a `make lint-ports`), and
`cargo test --offline` in the gate.

5.4 **MEDIUM — harness holes.**
- If both engines time out, the case passes: `compare_engines.py:103-104` and
  `compare_all_programs.py:100-105` return the same placeholder for each side.
  `--reference hang --engine hang --timeout 0.2 core le` prints "2 passed", exit 0.
- A case selection that matches nothing runs zero cases and exits 0.

VERIFIED.

5.5 **MEDIUM — the image gate silently skips every pgu program.** `compare_all_programs.py:53`
globs `pgu/**/*.s` (62 files, 7 modes each), but the Dockerfile copies no `pgu/`, so inside
`make image` those cases don't exist. Its docstring says pgu is covered. MAIN-VERIFIED (the
Dockerfile's COPY list).

5.6 **MEDIUM — the Rust launcher never rebuilds** (`port/rust/spimulator-rust:13`). It builds only
when the binary is missing; the Haskell launcher checks source timestamps. In a dev shell, an edit
to `port/rust/src` or a regenerated `opcodes_table.rs` leaves the port suites testing the old
binary. The image is safe because `.dockerignore` drops `target/`. VERIFIED.

5.7 **LOW — `tools/gen_opcodes.py` has small gaps.** `--check` catches every output (a mutation
test was reported stale, and `--check` writes nothing), and token numbers agree across C, Python
and Rust: 365 rows. The gaps:
- duplicate `value`s in `opcode-types.tsv` aren't rejected;
- names aren't escaped in emitted string literals;
- `FIRST_KEYWORD_TOKEN = 264` and the structural tokens in `tokens_h` must be changed together by
  hand;
- the decode table holds a dead `(-1, remu)` row.

VERIFIED.

5.8 **LOW — `tools/const_candidates.py`.**
- 43 of 73 lines print `:?:` as the line number: clang omits a repeated `loc.line`.
- A FILE argument resolves against the repo root, so a wrong path checks nothing and exits 0.
- A pointer passed through a cast is mis-classified.

VERIFIED.

5.9 **LOW — `tools/c_dispatch_check.py` miscounts a `','` character constant.** Constants are
counted by splitting `detail` on ",", so a `','` constant counts as two; that could produce a false
`switch-chain`. The vendored copy is byte-identical to the canonical, so the fix belongs in
github.com/billsix/runClaudeInContainer `tasks/reference/c_dispatch_check.py`. VERIFIED by
reading.

5.10 **LOW — `tools/check_asm_canonical.py` passes vacuously:** it exits 0 when no block is found,
ignores spim's exit status, and is in no gate. `cformat_vs_c.rs:96-99` reports "skipped" and passes
on any compile failure. `tests/run-test.sh:352` pipes spim into `grep -v`, which loses its exit
status. VERIFIED.

## 6. Tests

6.1 **HIGH — most REPL commands have no test on any engine.** The commands are listed at
`src/spim.c:75-80`. The tests only use load, read, disasm, args, run, quit, exit, print,
print_all_regs, `.` and a bare breakpoint.
- Never exercised: step, continue, delete, list, dump, dumpnative, help, print_symbols,
  reinitialize, and a breakpoint that actually fires.
- `corpus.txt:13-14` excludes the REPL on purpose, and the REPL tests that exist only grep for a
  line or two.
- So a port's REPL can diverge without failing anything. Promoting the one-shot REPL differs in
  `tasks/adhoc/engine-port-*` into a harness REPL corpus that compares the full transcript would
  close it.

VERIFIED.

6.2 **MEDIUM — exit status is ignored in most driver tests.** The sentinel tests
(`tests/run-test.sh:39-113`) and `stderr_split` don't check rc. `examples/tests/run-demo.sh:270-276`
checks status only for exit-demo and atexit-demo. A port that prints the right output and then
crashes passes about 45 tests, although `port/README.md` requires matching exit status. VERIFIED.

6.3 **MEDIUM — the crashes in §4.2 had no test.** Each is a cheap `corpus.txt` case: large sbrk,
closing fd 1, a long delayed-jump chain. So are `.align` ≥ 32, out-of-range `$fN`, `-mapped_io`
(`tt.io.s` and `timer.s` are never run) and `read_string` with real input (LEAD that nothing else
covers the last two).

6.4 **LEAD — circular goldens.** `tests/golden.core.*` and `tt.explain.expected` were captured from
the C engine, so a C bug is pinned and the ports must copy it. That is consistent with "the C engine
is the standard", but it makes C fixes (§4.1.7) a four-engine change.

## 7. Proofs

N/A.

## 8. Build, container, CI

8.1 **MEDIUM — the base-install `RUN` masks a failure** (`Dockerfile:60-68`). The line is
`01-install-base.sh && echo … ; if [ "$USE_EMACS" = "1" ]; then …; fi ;`, so the `RUN`'s status is
the `if`'s. MAIN-VERIFIED: `sh -c 'false && echo x ; if [ 0 = 1 ]; then :; fi'` exits 0. A failed
base install surfaces later, at an unrelated step. Fix: chain with `&&`, or accumulate the status,
per the convention.

8.2 **LOW — Dockerfile comments cite moved task docs.**
`Dockerfile:175` cites `examples/tasks/PLAN-asm-listings-makefile.md` (now
`tasks/archive/2026/05/24/`), and `:180` cites `tasks/examples-makefile-debug-build.md` (now
`tasks/archive/2026/10/31/`). MAIN-VERIFIED.

8.3 **LOW — all `ARG`s are declared at the top** (`Dockerfile:3-26`). Toggling `BUILD_TREE_SITTER`
(used at :195) or `RUN_SANITIZERS` (:150) re-runs every slow layer below. The template's rule is
to declare each `ARG` just before its first use. MAIN-VERIFIED.

8.4 Otherwise the Makefile conforms to the template:
- `.DEFAULT_GOAL := help`;
- one `SHELL_RUN_FLAGS` shared by `shell`/`shell-exec`;
- `PODMAN_RUN_FLAGS` threaded into every `run`;
- `image-export`/`image-import`;
- feature flags default 1 in the Makefile and 0 in the Dockerfile.

There is no `type-check` target for the Python port (§5.3).

## 9. Open-task backlog

9.1 **MEDIUM — `tasks/codebase-cleanup-plan.md` is mostly done.**
- Done: A1–A4, A6, A10, B2, C4 and C5.
- F4 concerns the retired `opcodes.h`, and G2 is marked superseded.
- Its verification gate leaves out the ports and `make image`.
- It has no Status line and no BLUF.

Trim it to what's open, or archive it. VERIFIED (a grep per item).

9.2 **MEDIUM — `tasks/minimal-nested-image.md`'s premise is overturned.** It builds on the
`NESTED_PODMAN`-keyed idiom, which the fleet replaced with the opt-in `MINIMAL_IMAGE` on
2026-09-27, and it cites `opcodes.h`. VERIFIED.

9.3 **MEDIUM — five tasks change C behavior without a ports step**, which `CLAUDE.md` requires
("a C behavior change must be followed in every port"). Their Difficulty ratings are probably low.
- `ast-column-tracking`: `-print-ast` output, which the port suites compare.
- `explain-mode-correctness-bugs`: the REPL `step` fix and `-show-expansion`.
- `explain-levels-redesign`: a new `-encoding` flag and a REPL `explain` command.
- `timing-model`.
- `software-alu`.

VERIFIED.

9.4 **LOW.**
- `tasks/engine-ports-teaching-mode.md` is marked `blocked` on two in-repo tasks; the convention
  is Priority plus `Depends on`.
- `tasks/examples-c-debug-build-via-meson.md:3` says "ready to implement" but has three open
  questions.
- `string-equality-audit` is done and waits on a maintainer answer.
- Missing BLUF, Context or Status lines in eight tasks, listed under 1.5.

VERIFIED.

**The backlog as the docs read today** (Priority, then Difficulty):

| slug | status | P | D | verdict |
|---|---|---|---|---|
| explain-mode-correctness-bugs | proposed | 2 | 4 | valid; add a ports step; add §4.1.2 (.hb) and §4.1.4 |
| examples-c-debug-build-via-meson | proposed | 4 | 3 | blocked by its own open questions |
| minimal-nested-image | proposed | 4 | 3 | premise overturned (§9.2) |
| codebase-cleanup-plan | — | 4 | 7 | mostly done (§9.1) |
| explain-levels-redesign | proposed | 4 | 7 | needs a ports step |
| immediate-range-error-line-number | proposed | 5 | 2 | valid; already plans the ports |
| container-cross-env | — | 5 | 4 | depends on debug-build question 2 |
| multi-file-load | re-scoped | 5 | 5 | only the de-dup sweep remains |
| pgu-build-matrix | — | 5 | 5 | waits on debug-build question 3 |
| paired-single-instructions-unsupported | proposed (on hold) | 6 | 2 | valid |
| ast-column-tracking | — | 6 | 5 | needs a ports step |
| examples-build-matrix | — | 6 | 5 | likely retired by debug-build decision 3 |
| timing-model | proposed | 6 | 6 | needs a ports plan |
| software-alu | — | 6 | 8 | needs a ports plan |
| mini-c-compiler | proposed | 6 | 9 | valid |
| scanner-xmalloc-int-narrowing | proposed | 7 | 2 | still open; a two-line fix |
| string-equality-audit | waiting | 7 | 2 | close or re-ask |

**Blocked:** `engine-ports-teaching-mode` (P8, D7), which should be `Depends on` (§9.4);
`big-endian-conversion-hardening` (P9, D4), a real external gate.

## 10. Archive hygiene

10.1 **HIGH (per the convention) — 13 archived docs were never rewritten into done-records.** Each
keeps a plan-shaped BLUF, a Plan, Open questions or future tense under a "done" status:
- `tasks/archive/2026/10/16/fix-inherited-idiosyncrasies.md` (five unchecked plan items under a
  SUPERSEDED status; MAIN-VERIFIED) and `2026/10/16/integer-conversion-hardening.md`;
- `2026/10/30/calc-incremental-versions.md`, `examples-separate-io-from-algorithm.md`,
  `separate-io-from-algorithm-remaining-demos.md` and `incremental-example-versions.md`;
- `2026/10/31/examples-makefile-debug-build.md`, whose "settled" `DEBUG_FLAGS` also contradicts
  `examples/src/Makefile:64-68`;
- `2026/10/31/asm-simplification-sweep.md` and `asm-simplification-sweep-step-1-pilot-and-style-doc.md`;
- `2026/09/14/trim-claude-md.md`, `2026/09/02/libstr.md` and `calc-language.md`;
- `2026/10/28/fix-examples-listing-std-flag.md`.

VERIFIED.

10.2 **MEDIUM — one-shot ad-hoc scripts of archived tasks are still tracked.**
- `tasks/adhoc/engine-port-haskell/` (2 files), `engine-port-python/` (3) and `engine-port-rust/`
  (3). `engine-port-python/add_port_lines.py` is a stale duplicate of the promoted
  `tools/add_port_lines.py`. MAIN-VERIFIED (`git ls-files tasks/adhoc`).
- `tasks/adhoc/engine-ports-float-format/` and `integer-conversion-hardening/` are cited as live,
  runnable checks by `engine-port-language-choice.md`, `engine-ports-design.md` and
  `simulator-integer-types.md`. They are kept, but not promoted to `tools/`; the maintainer should
  decide which.

10.3 **MEDIUM — dangling task paths, each to a doc since archived.**
- `2026/10/16/fix-inherited-idiosyncrasies.md:4`;
- `2026/10/30/incremental-example-versions.md:22,95`;
- `2026/10/30/calc-incremental-versions.md:93`;
- `2026/11/10/engine-port-language-investigation.md:20`;
- `2026/11/10/engine-port-prereq-c-fixes.md:85`;
- `2026/11/10/engine-ports.md:67`.

VERIFIED.

10.4 **MEDIUM — stale or contradicted claims.**
- `2026/11/10/engine-ports.md:43` ("not registered yet").
- `engine-port-python.md:165-171` (install script and corpus "missing"; both exist).
- `engine-port-haskell.md:54-75` (planned answers the Decisions section reverses, not marked
  superseded).
- `2026/11/11/paired-single-compare-encoding.md:42` (future-tense image gate, with no result; the
  gate later passed, 293/293).

VERIFIED.

10.5 **MEDIUM — an open decision is never tracked.** `2026/11/05/upgrade-to-fedora-45.md:31-35`
leaves open whether `.align 0` should disable auto-alignment. No open task tracks it. The variable
is still write-only (`src/instruction.c:71`; baseline §3.25). VERIFIED.

10.6 **LOW.**
- Relative links: `2026/08/03/parser-leak-cleanup.md:8`, `2026/09/02/calc-language.md` and
  `libstr.md`.
- `2026/11/11/python-port-match-statements.md:85-87` names the wrong adhoc slug.
- `2026/11/08/add-dockerignore.md:14-15` has no superseded note for the MELPA step.
- `2026/11/10/c23-modernization-pass3.md:43,49-50` has deferrals with no carry-over note.
- `2026/10/30/wc-add-golden-test-coverage.md:6-7` has a stale Status.

VERIFIED.

10.7 Every commit SHA cited in `2026/08`–`2026/11` resolves and is reachable from master. The
superseded decisions of the past days carry notes: the X-macro → TSV, `dispatch_candidates.py` →
`c_dispatch_check.py`, the token enum declined, and the paired-single compares removed. VERIFIED.

## Carried over from the previous review

The baseline is `tasks/review/2026/11/10/repo-review.md`. `review_status.py --carried-over` matched
0 follow-ups: the baseline's bullets put the tier word after the ID, a format the tool may not
expect. So each item below was re-checked against the tree by the C reader.
- The §3 bugs: **11 fixed** (3.2, 3.3, 3.4, 3.5, 3.7, 3.8, 3.10, 3.13, 3.16, 3.20, 3.24),
  **3 partly** (3.9, 3.15, 3.27) and **13 still open**.
- The follow-ups: F5 done, F7 mostly, and F3, F4, F10 and F12 partly. F6, F8, F9 and F11 are open.

| Item | Status now | Evidence |
|---|---|---|
| §3.1 print_reg_from_string overflow | still open | `src/spim.c` `print_reg_from_string`: `s1 - s < 100` then `*s1 = '\0'` |
| §3.2 setjmp locals | fixed | `volatile` locals in `top_level` |
| §3.3 expand_k_data | fixed | byte offsets |
| §3.4 divu guard | fixed | guard is `RT != 0` only |
| §3.5 forward `.word lab+N` | fixed | `write_data_value(offset, width)` |
| §3.6 explain peeks | still open | `sb $zero,1($t0)`: exit 0 at `-explain=0`, 132 at `-explain=2` |
| §3.7 c.cond/movf decode | fixed | `mk_co_r_inst` |
| §3.8 movn.s/movz.s | fixed | `REGS(RT,16)` |
| §3.9 duplicate encodings | partly | still 6 duplicates, now deterministic but wrong for 3 (§4.1.3) |
| §3.10 BRANCH_OFFSET | fixed | new macro; range error |
| §3.11 / §3.12 delay-slot exceptions | still open | `JUMP_INST` unchanged |
| §3.13 signed UB | fixed | unsigned arithmetic |
| §3.14 `.align` shift | still open | `data.c`, `instruction.c` unbounded; now a port divergence (§4.3.2) |
| §3.15 REPL redo | partly | blank lines after `print $ra` print `Reg 32`, `Reg 33` |
| §3.16 `atoi(NULL)` | fixed | `flag_value_missing` |
| §3.17 `addrs[10000]` | still open | `spim-utils.c` |
| §3.18 read_int overflow | still open | `v*10 + digit` |
| §3.19 read_string bounds | still open | unchanged |
| §3.20 `,` FP separator | fixed | `.` only |
| §3.21 find_symbol_address inserts | still open | still calls `lookup_label` |
| §3.22 FPU round/convert | still open | `val + 0.5` |
| §3.23 fopen/read unchecked | still open | unchanged |
| §3.24 `%08x` with long | fixed | cast |
| §3.25 `enable_text_auto_alignment` | still open | write-only (§10.5) |
| §3.26 explain store snapshots | still open | sw/sh/sb only (§4.1.4) |
| §3.27 run_error before setjmp / sigsetjmp | partly | `top_level_env_ready`; no `sigsetjmp` |
| [F1] pass-3 worklist | done | `tasks/archive/2026/11/10/c23-modernization-pass3.md` |
| [F2] port lens into the investigation | done | `tasks/archive/2026/11/10/engine-port-language-investigation.md` |
| [F3] memory-safety bugs | partly | 3.3 and 3.16 fixed; 3.1, 3.14, 3.15, 3.17, 3.19, 3.23 open |
| [F4] setjmp locals, sigsetjmp | partly | volatile done; no sigsetjmp |
| [F5] wrong-value engine bugs | done | all six fixed |
| [F6] explain peeks | still open | §3.6, §3.26 |
| [F7] test wiring | mostly | added: alu_bare, fpu_bare, core_full, core_default_handler; explain_parity dropped; the `bare` test still lacks `-bare` (`tests/run-test.sh:40`) |
| [F8] duplicate encodings | still open | §4.1.3 |
| [F9] man page, usage, src/README | still open | §2.2; `CLAUDE.md`'s Fedora version is fixed |
| [F10] dead code | partly | `dump-opcodes.c` and `qsort` gone; §4.9's names remain |
| [F11] orphan tests | still open | `read.s` and `allinsts` not wired |
| [F12] assembler quirks | partly | `,` decided; §3.21 and §3.25 open |

## Suggested follow-ups

- **Required:**
  - [F1] Make the documented opcode-change workflow work. `make generate` must not need an image
    that `--check` fails to build; document the host fallback (§2.1).
  - [F2] Make the harness's float comparison numeric (a few ULP), and gate the ports' float
    formatters: register `check-format.sh`/`check-strtod.sh`, run `cargo test --offline`, and add a
    Python formatter oracle (§5.1, §5.2, §1.2).
  - [F3] Fix the cross-port crashes on valid programs, each with a `corpus.txt` case:
    large sbrk in all three ports (C's fatal message);
    closing fd 1 in Python and Haskell;
    Python's delay-slot recursion;
    Rust's `+ 1` and `steps -= 1` overflows, plus a debug-build or `overflow-checks` gate

    (§4.2, §4.5.1, §4.5.2, §6.3).
  - [F4] Make the Haskell register writes strict, so the HI/LO thunk leak goes away. Also make `sbrk`
    non-quadratic (§4.4.1, §4.4.2).
  - [F5] Make `parse_j` pass `tools/c_dispatch_check.py` again, and wire the checker into meson so it
    stays green (§4.1.1, §2.3).

- **Recommended:**
  - [F6] Add a lint gate per port: ruff, `ty`, `check_local_annotations` and the doctests for Python;
    clippy, `cargo fmt` and `cargo test` for Rust; `hlint` (advisory) for Haskell (§5.3).
  - [F7] Build a REPL transcript corpus for the harness, promoted from the one-shot REPL differs, so
    every REPL command is compared on every engine (§6.1, §10.2).
  - [F8] Check the exit status in every sentinel and demo test (§6.2).
  - [F9] Close the harness holes: a C-side timeout fails, a selection of zero cases fails, `pgu/` is
    copied into the image (or a minimum count is asserted), and the Rust launcher always runs
    `cargo build` (§5.4, §5.5, §5.6).
  - [F10] Teach the teaching mode `jr.hb`/`jalr.hb`, and fix `explain_before`'s comment (or snapshot
    the other stores) (§4.1.2, §4.1.4, §3.26).
  - [F11] Resolve the duplicate encodings: give the xc1/prefx rows their COP1X encodings, OR funct in
    the COP1X decode, and drop the stars, with `BUILD_TREE_SITTER=1` (§4.1.3; baseline F8).
  - [F12] Fix the C bugs the ports exposed, then follow in every port:
    `ss_clear` truncation in `print_all_regs`;
    `-skt 0` looping;
    the `ld`/`sd`/`mfc1.d` register-31 pair forms;
    `.align` range;
    the `-assemble` trailing separator;
    out-of-range `$fN`

    (§4.1.7, §4.3.1, §4.3.2, §4.5.6).
  - [F13] Do the done-record rewrites for the 13 archived docs, repoint the dangling archive paths,
    add the missing superseded and "since done" notes, and `git rm` the one-shot `engine-port-*`
    ad-hoc scripts (§10.1, §10.2, §10.3, §10.4).
  - [F14] Fix the Dockerfile's masked base-install status, and move each `ARG` to just before its
    first use (§8.1, §8.3).
  - [F15] Rework the backlog:
    trim or archive `codebase-cleanup-plan`;
    rewrite or retire `minimal-nested-image`;
    add ports steps (and re-rate) to the five behavior-changing tasks;
    turn `engine-ports-teaching-mode` into `Depends on`;
    rewrite `tasks/README.md`'s ordering section from the table above, and drop its ChangeLog step

    (§9, §1.1).
  - [F16] File a task for the `.align 0` / `enable_text_auto_alignment` decision (§10.5; baseline
    §3.25).

- **Optional:**
  - [F17] Rewrite `Documentation/spim.1` from the flag parser, and install it as `spimulator.1`
    (§2.2; baseline F9).
  - [F18] Fix the docs:
    the gate wording (clang-gated; the checkers are manual until F5/F6);
    `engine-port-language-choice.md`'s stale claims;
    `engine-ports-design.md`'s numbers and paths;
    the deleted `check_decode.c` citation;
    the README's Windows claim, copyright SHA and example path;
    `port/README.md`'s missing tests;
    the stale Dockerfile comment paths

    (§1.2, §1.3, §1.4, §2.3, §2.4, §2.5, §8.2).
  - [F19] Python cleanups:
    the guard-only `match`es;
    the "as before" comments;
    the open-ended chains `_emit_one` and the three `else`-less chains;
    enum members over mnemonic slicing;
    the dead `force_break`/`Kind.ERROR`/`noqa`s;
    `assert` used as a type guard

    (§4.3.3–§4.3.8).
  - [F20] Haskell cleanups: concrete local types, the partial-function forms, the FP-compare hot path,
    the `BindingTypes` list-comprehension exemption, and a non-empty `hie` guard (§4.4.3–§4.4.6).
  - [F21] Rust cleanups: the unannotated bindings, naming the TLTU/TLTIU arm, and the dead public
    functions (§4.5.3–§4.5.5).
  - [F22] Tool fixes:
    `c_dispatch_check.py`'s `','` count, in the canonical copy;
    `const_candidates.py`'s `:?:` lines and path resolution;
    `check_asm_canonical.py`'s vacuous pass;
    `cformat_vs_c.rs`'s skip-on-compile-failure;
    `gen_opcodes.py`'s duplicate-value check and literal escaping;
    `run-test.sh:352`'s pipe

    (§5.7–§5.10).
  - [F23] Convention cleanups:
    the bare "Bill" stamps and non-root-relative links;
    the history sections in the reference docs;
    the missing BLUF/Context/Status lines

    (§1.4, §1.5, §9.4, §10.6).

## Open questions for the maintainer

1. For the C undefined-behavior cases the ports expose (`ld $31`, `.align` ≥ 32, out-of-range
   `$fN`, the `-assemble` trailing separator), should C be fixed first and the ports then follow, or
   should the ports copy C's current behavior? Recommendation: fix C first (F12), as the baseline's
   question 2 settled for the earlier engine bugs.
2. Should the float tolerance in the harness stay at all? Recommendation: keep a numeric tolerance
   of a few ULP for genuine formatting differences, and drop the printed-digit rule (F2).
3. Should `tasks/adhoc/engine-ports-float-format/` and `tasks/adhoc/integer-conversion-hardening/`
   be promoted to `tools/`, now that reference docs cite them as live checks? Recommendation:
   promote the float-format checks, as part of F2, and `git rm` the integer-conversion scan, since
   the `-Wconversion` gate replaced it (§10.2).

---

*Note, 2026-11-11 (same day): the maintainer, William Emerison Six <billsix@gmail.com>, answered
the three open questions, each as recommended. (1) C is fixed first for the undefined-behavior
cases, and the ports then follow (F12). (2) The harness compares floats numerically, within a few
ULP, and the printed-digit rule goes (F2). (3) `tasks/adhoc/engine-ports-float-format/` is promoted
to `tools/` as part of F2, and `tasks/adhoc/integer-conversion-hardening/` is removed. No finding
was changed.*

*Note, 2026-11-11 (same day): the "Suggested follow-ups" list was reformatted, with each tier as a
`- **Tier:**` bullet, its follow-ups indented under it, and no nested sub-bullets, so
`review_status.py` reads one follow-up per ID. The reviewer's own formatting error; no finding or
follow-up text was changed.*
