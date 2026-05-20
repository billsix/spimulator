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

These re-declare functions that `inst.h` already declares — and
parser.c already `#include`s inst.h.  ~50 lines of redundant
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

`src/mem.c:297-374` exposes `read_mem_byte`/`read_mem_half`/
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
`store_half`, `store_byte` (`inst.h:232-235`) which don't.
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

### C2. Audit which `inst.h`-declared functions are only used in inst.c

Survey claims `eval_imm_expr` is only used inside `inst.c`.
Verify, and if true, drop the declaration from `inst.h` and mark
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

## Tier D — C23 modernization (1 week, low risk)

The codebase opted into `gnu23` but uses almost no C23 features.
This is low-hanging fruit.

### D1. `NULL` → `nullptr` everywhere

~201 occurrences.  `nullptr` is a typed keyword in C23, catches
real bugs (passing `NULL` to a non-pointer variadic, etc.).
Mechanical sed.  One commit.

### D2. `#include <stdbool.h>` → delete (bool/true/false are keywords in C23)

Drop all `#include <stdbool.h>` lines.  `bool` / `true` / `false`
work without the header.  ~12 files.

### D3. `[[nodiscard]]` on error-returning functions

Functions whose return value carries the "did this fail" signal
should be `[[nodiscard]]`:

- `parse_file()` in `parser.h:46` (returns error count)
- `read_assembly_file()` in `spim-utils.h` (returns bool)
- `lookup_label()` in `sym-tbl.h:67` (returns NULL on miss)

Compiler will warn if any caller silently drops the result.
Catches latent bugs.

### D4. `[[maybe_unused]]` on parameters used only in macros

E.g. `addr` parameter of `bad_text_read()` in `mem.c:378` is
only consumed by the `RAISE_EXCEPTION(addr, ...)` macro, which
makes clang think the parameter is unused.  Marking it cleans
the warning honestly.

### D5. `static_assert(...)` for layout invariants

`inst.c` and `mem.c` quietly assume things like
`sizeof(mem_word) == 4` and `sizeof(instruction) == sizeof(uint32_t)`.
Add `static_assert` at file scope so a future port to a 64-bit-
int platform fails to compile loudly rather than miscomputing.
C23 lets you write `static_assert` without the underscore.

### D6. `constexpr int` for typed compile-time constants where it matters

The `#define` macros for register counts (`R_LENGTH`, etc.) are
type-less, so `R[R_LENGTH]` works but `sizeof(R) / R_LENGTH`
doesn't always do what you want.  `constexpr int R_LENGTH = 32;`
fixes this.  Pick the few constants that benefit; the others
can stay as `#define`.

### D7. Enable more warnings

Add to `meson.build`:
- `-Wunused-function` (would have flagged `parse_br_imm32` years ago)
- `-Wpedantic` (catches GCC-isms that would break on Clang/MSVC)
- `-Wshadow` (catches inner-scope variable shadowing)

Be ready for a flood of one-time noise; suppress per-file with
`[[maybe_unused]]` where the noise is legitimate.

### D8 (optional). `auto` for obvious type inference

```c
auto entry = map_string_to_name_val_val(...);  // instead of: name_val_val* entry = ...
```

Cosmetic; controversial.  Don't bother unless the team likes it.

**Total Tier D effort: 1 week.  Net diff: ~minor; mostly
modernisation.**

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

## Tier F — structural refactors (weeks each, medium-high risk)

These are bigger swings.  Each is independently justifiable; none
is urgent.

### F1. Bundle file-statics into context structs

Counts of `static` per file:
- `spim.c`: 76
- `parser.c`: 62
- `explain.c`: 62
- `scanner.c`: 35
- `inst.c`: 35
- `mem.c`: 21

That's a lot of implicit state.  For parser.c specifically,
gather the related statics (token lookahead, error flags,
labels-on-current-line, file name) into a `parser_state`
struct and pass it down explicitly.  Same for scanner.c,
explain.c, mem.c.

**Benefit**: testable — you can instantiate two parsers in one
process for parity testing.  Re-entrant.  Easier to reason about.

**Cost**: invasive.  Every internal function grows a `state*`
parameter.  ~2 weeks per subsystem.

**Recommendation**: do parser.c first if at all.  Stop after it
if the win doesn't feel proportional to the churn.

### F2. Define the memory model as an opaque type

Today `mem.c` exposes `extern mem_word *text_seg, *data_seg`,
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

### F4. Move the `*_DIR` / `*_OP` / `*_POP` taxonomy out of `op.h`

`op.h` currently mixes assembler directives (`.alias`, `.data`),
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

### G2. Code generation for op.h's X-macro

The "X-macro requires code gen" friction the Go-port survey
identified is real if you port to Go, but in C the X-macro IS
the code-gen step.  It's elegant and working.  Don't replace
with a Python script that emits the tables.

### G3. Replacing manual `xmalloc`/`free` with arena allocators or GC

For a 14k-LOC simulator that runs for seconds at a time, manual
memory is fine.  Bringing in Boehm GC or an arena framework
adds dependency for marginal benefit.

### G4. Bitfield struct refactor

`dump_ops.c` uses bitfields to overlay the MIPS instruction
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
- The X-macro pattern in op.h — see G2.

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
