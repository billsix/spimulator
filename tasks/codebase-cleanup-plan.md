# Codebase cleanup + modernization plan

Audit of `/spimulator/` (~14k LOC C, `c_std=gnu23`) after the
Phase-5 parser migration + four-tier post-Phase-5 naming cleanup
landed.  Organised by effort / risk / payoff so individual items
can be done independently.

The plan covers refactorings, renamings, restructurings, and
modernizations to newer C standard features.  Each item is
sized and triaged.  Items at the top are pure wins; items at the
bottom are bigger swings with more risk.

## Verification gate (applies to every tier)

After any change:
1. `ninja -C builddir` clean.
2. Dockerfile smoke set passes — bare / core / le / argv /
   args-cmd / read_int_eof.
3. /examples curriculum spot-check produces identical output.

Cosmetic cleanups have no business causing regressions.  Back
out anything that fails the gate.

---

## Tier A — quick wins (hours total, zero risk)

These are dead-code deletions and one-line fixes.  Pre-authorized
as a group; do them all in one commit.

### A1. Delete `parse_br_imm32` in `src/parser.c:344`

Unused since some earlier refactor; flagged by every build as
`-Wunused-function`.  Function and its 2-line comment go.  ~6
lines deleted.

### A2. Delete the `parser_error_occurred` ghost in `include/spim.h:237`

Declared `extern bool parser_error_occurred` but never defined
or read anywhere.  The active symbol is `parse_error_occurred`
(no `r`).  Pure cruft, deleting it removes a future foot-gun
for someone who reaches for the wrong one.

### A3. Delete `// write_startup_message ();` in `src/spim.c:376`

Commented-out function call.  Either we want it or we don't;
the comment-out is a decision frozen in amber.

### A4. Add include guards to `include/spim-syscall.h` and `include/version.h`

Both currently lack `#ifndef`/`#define`/`#endif`.  `spim-syscall.h`
is a pure `#define` block which is double-include-safe today, but
guarding it removes a class of future bug and matches the
convention every other header follows.

### A5. Fix `README.md` build-system claim

`README.md:7,31-32,50-51` says spim is built with CMake and tells
the reader to `apt install cmake` / `brew install cmake`.  The
actual build is meson (per `meson.build`, `Dockerfile`).  Replace
all four mentions.

### A6. Delete `README.rtf`

11KB of RTF.  Same content as `README` in worse format.  Nobody
reads RTF from a source tree.  The plain-text `README` and
modern `README.md` cover the same ground; pick one as canonical
(suggest `README.md`) and either delete `README` or mark it as
the legacy SPIM upstream documentation.

### A7. Wrap the one bare `realloc` in `src/spim.c:208` (label_names_cache)

Every other realloc in the codebase either uses `xmalloc`-style
fatal-on-NULL or explicitly NULL-checks; this one neither
checks nor wraps.  Either use the existing convention or
introduce `xrealloc` once and use it everywhere.

### A8. Drop the 46 duplicate `extern` declarations at `src/parser.c:101-150`

These re-declare functions that `instruction.h` already declares — and
parser.c already `#include`s instruction.h.  ~50 lines of redundant
forward-decl noise that becomes a maintenance hazard if any
signature ever changes (the duplicates would silently diverge).
Mechanical delete + rebuild to confirm.

### A9. Update `ChangeLog` Phase-5 entry to reflect post-rename names

The Phase-5 ChangeLog entry still talks about `Y_*` tokens and
`yyerror` because that's what existed when the entry was
written.  The post-cleanup tiers renamed those to `TOK_*` and
`parse_error`.  Either add a second entry for the
cleanup tiers or amend the original to mention the follow-on
work.  Cosmetic but the ChangeLog should be greppable.

### A10. Delete `bldInstall/` from the working tree and add it to `.gitignore`

Untracked directory cluttering `git status`.  Already-built artefact.

**Total Tier A effort: half a day.  Net diff: ~−150 lines.**

---

## Tier B — naming consistency (one day, low risk)

### B1. Settle `read_mem_*` vs `set_mem_*` verb asymmetry

`src/memory.c:297-374` exposes `read_mem_byte`/`read_mem_half`/
`read_mem_word`/`read_mem_inst` as getters, but the setters
are `set_mem_byte`/`set_mem_half`/`set_mem_word`.  Two choices:

- **Option A**: `read_mem_*` / `write_mem_*` (read/write pair).
- **Option B**: `mem_read_*` / `mem_write_*` (noun-first; matches
  scanner_init / parser_init from Tier 2a of post-Phase-5).

Recommend B — the noun-first convention is what we just committed
to for the parser/scanner.  ~6 setter renames, ~30 call-site
updates.

### B2. Pick one of `str_copy` / `strdup` and use it everywhere

`spim-utils.c:507` defines `str_copy` (5 callers); `strdup` is
called 10+ times directly (e.g. `spim.c:129,154,977,981`).  They
appear to do the same thing.  Either:

- Delete `str_copy`, use `strdup` (POSIX, present everywhere
  spim runs).
- Keep `str_copy`, sed all `strdup` callers to use it.

Recommend the first — `strdup` is standard and shorter.

### B3. Decide whether `*_inst` is a suffix convention or noise

`r_type_inst`, `i_type_inst`, `j_type_inst` end in `_inst`.  But
they're emitting instructions, and so do `store_word`,
`store_half`, `store_byte` (`instruction.h:232-235`) which don't.
Either:

- All instruction-emit functions get `_inst` suffix
  (`store_word_inst`, etc.).
- None do (`r_type`, `i_type`, `j_type`).

I'd go with the second — the suffix doesn't earn its keep.
"_inst" is implied by the context.

**Total Tier B effort: 1 day.  Net diff: ~50 lines + lots of
mechanical renames.**

---

## Tier C — header + module hygiene (2-3 days, low-medium risk)

### C1. Split `include/spim.h` (currently 12 nested `#ifdef` blocks)

That one header bundles types, sizing constants, function
declarations, exported globals, and platform shims.  Split into:

- `include/types.h` — `mem_addr`, `int32`, `uint32`,
  `intptr_union`, `reg_word`, etc.
- `include/limits.h` — `TEXT_SIZE`, `DATA_SIZE`, `K_TEXT_SIZE`,
  `STACK_TOP`, etc. (sizing macros).
- `include/spim.h` — function declarations + exported variables
  only.

Reduces inclusion blast radius: a TU that only needs `mem_addr`
shouldn't pull in stdio + termios shims.

### C2. Audit which `instruction.h`-declared functions are only used in instruction.c

Survey claims `eval_imm_expr` is only used inside `instruction.c`.
Verify, and if true, drop the declaration from `instruction.h` and mark
the definition `static`.  Repeat for any other internal-only
function that leaked into the public header.

### C3. Compress the 32-line BSD-license boilerplate per file

Every `.c` and `.h` has the same 32-line block.  ~33 files × 32
lines = 1056 lines of license boilerplate.  Replace with:

```c
/* SPIM S20 MIPS simulator.
   SPDX-License-Identifier: BSD-3-Clause
   See LICENSE for full text. */
```

(plus a top-level `LICENSE` file with the full text).  Net diff:
~−1000 lines.  Zero behavior change.  Modern convention.

### C4. Drop the dead `wsock32`/`ws2_32` link in `meson.build:63-66`

Survey claims the codebase has no socket code.  Verify with
`grep -rE 'socket\(|bind\(|listen\(|accept\(' src/`.  If empty,
drop the Windows-only socket library link — it's pure deadweight
on Windows builds.

### C5. Audit + delete stale `#ifdef _AIX` / `#ifdef RS` / `#ifdef NEED_TERMIOS` blocks

`spim.c:52,62,70` have ancient AIX/RS/NEED_TERMIOS conditionals.
Unless someone is actually building this on AIX in 2026, delete.

**Total Tier C effort: 2-3 days.  Net diff: ~−1100 lines.**

---

## Tier D — C23 modernization (DONE)

Landed May 2026 across twelve numbered commits on the
`c23modernization` branch.  Final-state writeup:
[`archive/2026/05/22/c23-modernization.md`](archive/2026/05/22/c23-modernization.md).

What this delivered (Tier D items in the original plan, plus
several that emerged during execution):

- D1. `NULL` → `nullptr` — already done in earlier portability work.
- D2. `<stdbool.h>` removal — already done in earlier portability work.
- D3. `[[nodiscard]]` — applied to 41 heap-allocators and status-return
  functions across `ast.h`, `instruction.h`, `symbol-table.h`, `spim-utils.h`,
  `string-stream.h`, `run.h`.  Wider sweep than the original three
  the plan named.
- D4. `[[maybe_unused]]` on macro-only parameters — not needed; no
  unused-parameter warnings remained after the sweep.
- D5. `static_assert` — already in place at `include/memory.h:19`.
- D6. `constexpr` — applied to ~85 constants across spim.h, memory.h,
  instruction.h, registers.h.  Includes register-array lengths, memory-segment
  addresses, immediate bounds, register-number selectors, and the
  CP0/FCSR bitmask families.
- D7. Warning flags — `-Wpedantic`, `-Wshadow`, `-Wunused-function`
  already on.  This work added `-Wimplicit-fallthrough=5`.
- D8. `auto` — skipped (deferred as readability-harm risk).

Additional C23 features adopted that weren't in the original list:

- `[[noreturn]]` for the four `_Noreturn` functions.
- `enum E : T` underlying types on every named enum (uint8_t for
  most; int32_t for tokens.h's TOK_* family).
- Two `#define` clusters promoted to enums (`op_type`,
  `mips_exc_code`).
- `static inline` for `streq`, `sign_ex`; `typeof`-based macros for
  `MIN`/`MAX`/`ROUND_*` (kills double-evaluation).
- `<stdbit.h>` for CLO/CLZ.
- `<stdckdint.h>` for `add`/`addi`/`sub` overflow detection (closes
  a signed-overflow UB hole at `-O2`).
- `#embed` for the default exception handler (binary is now
  self-contained — no on-disk lookup needed).
- `= {}` empty initializers.

---

## Tier E — testing infrastructure (1 week, medium payoff)

### E1. Wire `tests/tt.*.s` into `meson test`

Currently the regression tests live as shell commands in the
Dockerfile.  Add a `test()` declaration for each one in
`meson.build` so `meson test -C builddir` runs them locally
without needing the container.  Also enables `--repeat` for
flake hunting and `--list` for inventory.

### E2. Add golden expected-output files

`tt.explain.s` emits ~2961 lines of teaching narration.  Today
the test just exits successfully.  Pinning a golden file
(`tt.explain.expected`) and diffing on every run would catch
silent regressions in the explanation system that nothing else
does.

Repeat for `tt.le.s`, `tt.core.s`, `tt.dir.s` — anything whose
output is stable.

### E3. Add tests for exception paths

Survey flags zero coverage of:
- Divide-by-zero behavior.
- Break instruction.
- Stack overflow.
- Kernel-mode transitions.

Each is a ~30-line test.  High pedagogical value: students hit
these.

### E4. Add a parity-style sanity test

The Phase 5 parity harness died with bison.  But the spirit
("two runs must agree") still has value — run each test twice
with different `-explain=N` levels and confirm the underlying
register state matches.  Lets us catch teaching-mode bugs that
change behavior.

**Total Tier E effort: 1 week.  Test coverage measurably better.**

---

## Tier F — structural refactors (CONSIDERED, NOT PURSUED 2026-05-20)

After Tiers A-E landed, evaluated F1/F2/F3 against the
post-cleanup state of the codebase and decided not to pursue.
The rationale:

- **F1 (context structs)** was motivated by "testable: you can
  instantiate two parsers in one process for parity testing."
  The parity-testing use case died with bison in Phase 5;
  there's only one parser now and spim runs one file at a
  time.  Threading a `parser_state*` through every internal
  function in parser.c adds a parameter to ~30 function
  signatures and ~200 internal call sites for zero behavioural
  or testing benefit.

- **F2 (opaque memory)** was the same shape — ~150 call sites
  changed to add a `memory*` parameter that the
  single-instance simulator never uses.

- **F3 (split run.c)** would force dispatch through a function-
  pointer table or chained sub-switches.  The current monolith
  fits the domain (one CPU's instruction decoder); splitting
  would obscure rather than clarify.

- **F4** the plan already recommended skipping.

Tier F items remain plausible if the codebase grows in ways
that change the calculus (e.g. embedding spim as a library, or
running multiple parsers concurrently for some teaching
visualisation).  Until then, the cost-benefit doesn't work.

Original Tier F text preserved below in case the calculus changes.

### Original F1-F4 (preserved)

### F1. Bundle file-statics into context structs

Counts of `static` per file:
- `spim.c`: 76
- `parser.c`: 62
- `explain.c`: 62
- `scanner.c`: 35
- `instruction.c`: 35
- `memory.c`: 21

That's a lot of implicit state.  For parser.c specifically,
gather the related statics (token lookahead, error flags,
labels-on-current-line, file name) into a `parser_state`
struct and pass it down explicitly.  Same for scanner.c,
explain.c, memory.c.

**Benefit**: testable — you can instantiate two parsers in one
process for parity testing.  Re-entrant.  Easier to reason about.

**Cost**: invasive.  Every internal function grows a `state*`
parameter.  ~2 weeks per subsystem.

**Recommendation**: do parser.c first if at all.  Stop after it
if the win doesn't feel proportional to the churn.

### F2. Define the memory model as an opaque type

Today `memory.c` exposes `extern mem_word *text_seg, *data_seg`,
etc. as globals.  Wrap in a `struct mips_memory`.  Pass to
read/write functions.  Same benefits as F1.

### F3. Split `run.c`'s 260-case switch by instruction category

`run.c` is 1637 lines, most of it in one switch.  Split into
`run_integer.c`, `run_fp.c`, `run_branch.c`, `run_memory.c`,
`run_syscall.c`.  Each gets its own dispatch.

**Benefit**: smaller TUs, faster incremental builds, easier
navigation.

**Cost**: have to thread the dispatch (function-pointer table or
sub-switches).  ~1 week.  Risk of subtle dispatch ordering
issues.

### F4. Move the `*_DIR` / `*_OP` / `*_POP` taxonomy out of `opcodes.h`

`opcodes.h` currently mixes assembler directives (`.alias`, `.data`),
real opcodes (`add`, `ori`), and pseudo-ops (`la`, `li`) into
one giant X-macro list.  Split into three smaller files
(`directives.h`, `opcodes.h`, `pseudo_ops.h`), each with its own
X-macro pass, with `tokens.h` enumerating tokens from all three.

**Benefit**: easier to audit "what opcodes do we support" vs
"what directives" without scrolling through 540 lines.

**Cost**: 2-3 days.  Risk of forgetting to add a new entry in
all three places where it's currently one place.

**Recommendation**: skip unless you find yourself needing it.
The current single-file X-macro is a feature.

---

## Tier G — things I'd explicitly NOT do

These came up in the audit but I'd advise against:

### G1. Replacing `setjmp`/`longjmp` with anything

The SIGINT-escape path in `spim.c` is the only `longjmp` user.
Three landing pads.  Works.  Alternatives (panic/recover-style
unwinding via flags) end up uglier.  Don't.

### G2. Code generation for opcodes.h's X-macro

The "X-macro requires code gen" friction the Go-port survey
identified is real if you port to Go, but in C the X-macro IS
the code-gen step.  It's elegant and working.  Don't replace
with a Python script that emits the tables.

### G3. Replacing manual `xmalloc`/`free` with arena allocators or GC

For a 14k-LOC simulator that runs for seconds at a time, manual
memory is fine.  Bringing in Boehm GC or an arena framework
adds dependency for marginal benefit.

### G4. Bitfield struct refactor

`dump-opcodes.c` uses bitfields to overlay the MIPS instruction
encoding.  This is exactly what bitfields are for.  Leave it.

### G5. Removing the static globals entirely

Some of the 335 statics ARE candidates for context structs (F1),
but a chunk of them (e.g. `bare_machine`, `delayed_branches`,
`accept_pseudo_insts`) are global modes set once at startup and
read everywhere.  Threading those through a context struct is
all cost, no benefit.

### G6. Adopting `[[deprecated]]` aggressively

There's nothing to deprecate — the codebase doesn't have a
released API surface anyone external depends on.  Save the
attribute for when it earns its keep.

---

## Suggested ordering

If you want to do everything: A → B → D → C → E → F → (skip G).

If you want to do the highest-value subset: **A + D + E**.
That's ~2 weeks of work, retires the visible cruft, and gives
you a real test infrastructure.  The structural refactors in
F can wait until they hurt.

If you want a single afternoon of polish: **just A**.  Half a
day, ~150 lines deleted, zero risk.

## What this plan deliberately doesn't touch

- The parser, scanner, or pseudo-op expander — they were just
  cleaned and renamed in Phase 5 + four tiers.  Stable.
- The explain.c teaching-mode subsystem — works, tested,
  audience-tuned.  Refactoring it is high risk for a tool
  whose value IS the teaching narration.
- Cross-platform support — Windows / AIX cruft removal is
  Tier C5 but the rest of the platform conditional logic
  should stay.
- The X-macro pattern in opcodes.h — see G2.

## First concrete step

Tier A is pre-authorized.  Just do it:

1. Delete `parse_br_imm32` (parser.c:344).
2. Delete `parser_error_occurred` (spim.h:237).
3. Delete `// write_startup_message ();` (spim.c:376).
4. Add include guards to spim-syscall.h and version.h.
5. Fix `README.md` CMake → meson (4 mentions).
6. Delete `README.rtf` (or move to docs/legacy/).
7. Wrap or NULL-check the realloc at `spim.c:208`.
8. Delete the 46 duplicate extern decls at `parser.c:101-150`.
9. Update ChangeLog Phase-5 entry footnote about TOK_* renaming.
10. Delete `bldInstall/` from working tree; add to `.gitignore`.

Build clean, run the verification gate, commit as a single
"Tier A cleanup" commit.  Then decide whether to authorize Tier B.
