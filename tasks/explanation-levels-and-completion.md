# Plan: explanation levels + tab completion

Two related changes to teaching mode, designed together so the second
builds cleanly on the first. Both are CLI-side work; both are
intentionally compatible with a future GUI session where the user
clicks a button to see more detail for an instruction after the fact.

## Goals

1. Replace the binary `-explain` on/off with **three levels of detail**
   (L1 = minimum, L2 = + interactive, L3 = full / current behavior),
   plus L0 = off (no `-explain` flag).
2. Add **tab completion** at the `(spim)` prompt for two things:
   a. the simulator's own REPL commands (`load`, `breakpoint`,
      `print`, ...) when the cursor is at the start of the line, so a
      student who doesn't remember the exact name can press Tab to
      discover it; and
   b. the "Try it yourself" suggestions from the most recent
      instruction when typing arguments to a command, so a student can
      recall a suggested `print $a0` with one keystroke. Neither kind
      of completion enters up-arrow history.

Non-goals (deferred):

- Runtime REPL toggle (`explain 2` to change level mid-session). Needs
  `scanner.l` / `parser.y` changes — same blocker as the original
  `explain` / `noexplain` toggle. Add later.
- The struct-and-render refactor that the GUI will eventually want
  (see "GUI compatibility" below). Plan for it; don't ship it now.
- Symbol-table and register-name completion. The tab-completion
  infrastructure this plan introduces is the right hook for it, but
  this plan ships only the suggestions-list use case.

## Part 1 — Three explanation levels

### What each level shows

Headings below use the existing section names from `src/explain.c`.

| Section                           | L1 | L2 | L3 |
|-----------------------------------|----|----|----|
| `About to execute at 0xNNNN:`     | ✓  | ✓  | ✓  |
| Disassembly line (ABI names)      | ✓  | ✓  | ✓  |
| Pseudo-op header (when applicable)| ✓  | ✓  | ✓  |
| `What it does:` (semantic line)   | ✓  | ✓  | ✓  |
| Syscall semantic ($v0 → name)     | ✓  | ✓  | ✓  |
| After-step diff (changed regs/PC) | ✓  | ✓  | ✓  |
| `Inputs read:` block              |    | ✓  | ✓  |
| `Will write:` block               |    | ✓  | ✓  |
| `Try it yourself:` `print` hints  |    | ✓  | ✓  |
| Syscall-4 string-bytes dump       |    | ✓  | ✓  |
| Stack-frame intent hint           |    | ✓  | ✓  |
| Bit-layout ASCII diagram          |    |    | ✓  |
| Mnemonic-decoding annotation      |    |    | ✓  |

Rationale:

- **L1** covers "what does this instruction do, and what state changed."
  The pseudo-op header is here, not deferred to higher levels, because
  without it a single `la $a0, msg` source line producing two real
  instructions becomes baffling.
- **L2** adds the simulator-as-tool layer: explicit inputs/outputs,
  Try-it-yourself, syscall string dumps, stack-frame intent hints. A
  student at L2 is learning to drive the simulator, not just read MIPS.
- **L3** adds the machine-encoding layer: bit layout, field decode,
  mnemonic-decoding annotation. Patterson & Hennessy §2.5 territory.

### CLI flag

- `-explain=N` or `-explain N` or `-x N` where N ∈ {1,2,3}.
- `-explain` / `-x` alone = 3 (preserves current behavior — backward
  compatible with existing scripts and documentation).
- `-noexplain` / `-nx` = 0 (unchanged).
- Invalid N → error and usage-print, don't silently coerce.

### Implementation

**State:** Replace `bool explain_mode` in `src/explain.c` with
`int explain_level` (0–3). For minimal diff in unrelated code,
`include/explain.h` keeps a compatibility macro:

```c
extern int explain_level;
#define explain_mode (explain_level > 0)
```

Anywhere that currently reads `explain_mode` (the two hook call sites
in `src/run.c`, and conditional code inside `explain.c` itself) keeps
working unchanged.

**Section gating:** Each section in `explain.c` is wrapped in a
level check. The cleanest pattern:

```c
if (level >= 2) {
    write_output(message_out, "  Inputs read:\n");
    /* ... */
}
```

Section-by-section, the level threshold matches the table above.

**Threading the level parameter (GUI-readiness — see below):**
Don't read `explain_level` from inside each template function. Read
it once at the entry to `explain_before` / `explain_after` and pass
it down:

```c
void explain_before(instruction *inst, mem_addr pc) {
    if (explain_level == 0) return;
    int level = explain_level;
    /* ... */
    tpl_r3_arith(level, inst, ...);
}
```

Each `tpl_*` takes `int level` as its first parameter. Inside, section
gating becomes `if (level >= N)`. This is the contract the GUI work
will eventually use to render an old instruction at a new level.

**Argument parsing in `src/spim.c`:** add cases for `-explain=N` /
`-explain N` / `-x N` next to the existing `-explain` / `-noexplain`
handling around line 220 (search for `explain_mode = true`). Update
the usage string a few lines below.

### Files touched

- `include/explain.h` — declare `extern int explain_level`; add
  `#define explain_mode (explain_level > 0)` compatibility macro.
- `src/explain.c` — replace `bool explain_mode` with
  `int explain_level`; thread `level` through `explain_before` /
  `explain_after` and into each `tpl_*` function; gate each section
  by level per the table.
- `src/spim.c` — argument parsing, usage string.
- `tests/tt.explain.s` — unchanged; the existing smoke test runs at
  L3 implicitly. New capture files for regression: `tt.explain.L1.expected`,
  `tt.explain.L2.expected`, `tt.explain.L3.expected` once the format
  stabilizes (this lands with the golden-output follow-up from
  `teaching-mode-coverage.md`).

### Verification

After build, re-run smoke test at each level:

```sh
./builddir/spimulator -ef src/exceptions.s -explain=1 -f tests/tt.explain.s | wc -l   # should be much smaller
./builddir/spimulator -ef src/exceptions.s -explain=2 -f tests/tt.explain.s | wc -l
./builddir/spimulator -ef src/exceptions.s -explain=3 -f tests/tt.explain.s | wc -l   # ~3901 (current)
./builddir/spimulator -ef src/exceptions.s -explain   -f tests/tt.explain.s | wc -l   # equals -explain=3
```

Spot-check L1 output by eye: each instruction should produce only the
"About to execute" header, disassembly, "What it does", and after-diff.

### Backward compatibility

- `-explain` with no argument keeps behaving as today (level 3).
- `-noexplain` keeps disabling narration.
- Any script or doc that says `-explain` continues to work.
- The `explain_mode` macro means no other source file needs editing.

## Part 2 — Tab completion at the (spim) prompt

### Behavior

Pressing Tab does context-aware completion based on where the cursor
sits in the line:

- **Cursor at column 0** (no command typed yet) — offer the simulator's
  REPL commands: `exit`, `quit`, `read`, `load`, `run`, `step`,
  `continue`, `print`, `print_symbols`, `print_all_regs`,
  `reinitialize`, `breakpoint`, `delete`, `list`, `dump`, `dumpnative`,
  `help`. Typing `pr<TAB>` resolves the ambiguous prefix, typing nothing
  and pressing Tab twice lists all of them.
- **Cursor after the first word** — offer the most recent instruction's
  `Try it yourself:` suggestions (e.g. `print $a0`, `print 4($sp)`).

Pressing up-arrow still walks only the student's *typed* command
history — neither completion source enters history.

### Design

Dispatch on the `start` argument passed by libedit (the column where the
text being completed begins):

```c
#ifdef HAVE_LIBEDIT
static char **spim_completion(const char *text, int start, int end) {
    (void)end;
    rl_attempted_completion_over = 1;          /* no filename fallback */
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }
    return rl_completion_matches(text, suggestion_generator);
}
#endif
```

Wired up once at the top of `top_level()`:

```c
#ifdef HAVE_LIBEDIT
    rl_attempted_completion_function = spim_completion;
#endif
```

#### Command completion

A static list of the 17 REPL command names mirrors the table in
`read_assembly_command()` at `src/spim.c:833`:

```c
#ifdef HAVE_LIBEDIT
static const char *spim_commands[] = {
    "breakpoint", "continue", "delete", "dump", "dumpnative",
    "exit", "help", "list", "load", "print", "print_all_regs",
    "print_symbols", "quit", "read", "reinitialize", "run", "step",
    NULL
};

static char *command_generator(const char *text, int state) {
    static size_t idx;
    static size_t text_len;
    if (state == 0) { idx = 0; text_len = strlen(text); }
    while (spim_commands[idx] != NULL) {
        const char *s = spim_commands[idx++];
        if (strncmp(s, text, text_len) == 0)
            return strdup(s);   /* libedit frees */
    }
    return NULL;
}
#endif
```

This list is sorted alphabetically so the Tab-Tab listing reads cleanly.
It deliberately omits the single-character forms `?` and `.` — Tab
completion of one character into another single character has no
discoverability value and Tab on `.` would compete with filename
patterns.

Drift risk: if a new REPL command is added to `read_assembly_command()`
in the future, `spim_commands[]` has to be updated by hand. That's
acceptable — new REPL commands are rare and the list is short. Adding a
build-time check that the two lists match would be over-engineering for
a 17-entry table.

#### Suggestion completion

A small per-instruction suggestions list owned by `explain.c`:

```c
/* In explain.c */
#define MAX_SUGGESTIONS 16
static const char *suggestions[MAX_SUGGESTIONS];
static size_t n_suggestions;

static void clear_suggestions(void) { n_suggestions = 0; }
static void add_suggestion(const char *cmd) {
    if (n_suggestions < MAX_SUGGESTIONS)
        suggestions[n_suggestions++] = cmd;   /* string-literal lifetime */
}

const char **explain_get_suggestions(size_t *n_out) {
    *n_out = n_suggestions;
    return suggestions;
}
```

`clear_suggestions()` is called at the top of `explain_before`. Each
template that emits a `Try it yourself:` line calls `add_suggestion()`
with the bare command (no trailing comment).

The suggestion generator, paired with the dispatcher above:

```c
#ifdef HAVE_LIBEDIT
static char *suggestion_generator(const char *text, int state) {
    static size_t idx;
    if (state == 0) idx = 0;
    size_t n;
    const char **list = explain_get_suggestions(&n);
    size_t text_len = strlen(text);
    while (idx < n) {
        const char *s = list[idx++];
        if (strncmp(s, text, text_len) == 0)
            return strdup(s);   /* libedit frees */
    }
    return NULL;
}
#endif
```

When the most recent suggestion is a multi-word command (e.g. the full
string `print $a0`), the student types `print ` first — `start` is then
past the space, so `text` is just `$a0`, and `strncmp("print $a0", "$a0", 3)`
fails. To make this case work, the suggestion generator could strip a
matching leading-command-and-space from the candidate before the
prefix check. That refinement is straightforward but easy to defer until
we have evidence students reach for it.

### Separating displayed lines from command form

Each `Try it yourself:` line in the current code is a single
`write_output` with the command and a trailing comment in one string:

```
    print $a0           # see $a0 before/after
```

Refactor those call sites into a small helper that takes the command
and the comment separately:

```c
static void try_it(int level, const char *cmd, const char *why) {
    add_suggestion(cmd);
    if (level >= 2) {
        write_output(message_out, "    %-20s # %s\n", cmd, why);
    }
}
```

The level guard inside `try_it` means template code stays clean —
`try_it(level, "print $a0", "see $a0 before/after")` regardless of
whether we're displaying or not. At L1 the suggestion is not added
either (because `Try it yourself` doesn't fire at L1), which is the
right behavior — the student at L1 isn't being shown the simulator
as a tool yet.

Actually, reconsider: should suggestions populate even when not
displayed? Arguments either way:

- **Yes, always populate:** even at L1 the student could hit Tab and
  discover what they could inspect. Discoverability beyond what's
  written.
- **No, only when displayed:** at L1 we deliberately hid the tool
  layer; Tab silently revealing it undermines the level distinction.

Recommendation: **no, only when displayed (L2+).** Levels are about
what we choose to show the student. Tab-completion on hidden
suggestions is invisible-magic and works against the leveled approach.

### Memory ownership

`add_suggestion()` stores `const char *` pointers. The strings have
to outlive the next `(spim) ` prompt. Three options:

1. **String literals only.** Every `try_it()` call site passes a
   literal. The `static const` storage in the read-only data segment
   outlives anything. Simplest, but rules out dynamic suggestions
   (e.g., `print foobar` where `foobar` is the symbol from the
   current `la`).
2. **strdup + free on clear.** `add_suggestion` strdups; `clear_suggestions`
   frees. Handles dynamic strings.
3. **Per-instruction arena.** Allocate from a small bump pool
   `clear_suggestions` resets. Avoids the malloc/free traffic.

Recommendation: **(2)**. The traffic is one malloc per suggestion per
instruction — negligible. Lets us emit suggestions like
`print msg` (where `msg` is a symbol name parsed from the source) or
`print 4($sp)` (where the offset is the actual offset from the
current instruction) without lifetime gymnastics.

### Files touched

- `include/explain.h` — declare
  `const char **explain_get_suggestions(size_t *n);`
- `src/explain.c` — suggestions list, `add_suggestion` /
  `clear_suggestions` / `explain_get_suggestions`. Each
  `Try it yourself:` call site refactored through `try_it()`.
- `src/spim.c` — `spim_commands[]` table, `command_generator`,
  `suggestion_generator`, `spim_completion` dispatcher, wire to
  `rl_attempted_completion_function` in `top_level()`. All gated on
  `HAVE_LIBEDIT`.

### Verification

```sh
./builddir/spimulator -ef src/exceptions.s -explain -f helloworld.s
```

At a fresh `(spim)` prompt:

- Press Tab on an empty line → list of REPL commands appears.
- Type `pr` then Tab → completes to `print` (or shows the three
  `print*` variants if Tab-Tab is pressed).
- Type `breakp` then Tab → completes to `breakpoint`.

After stepping to an instruction whose template emitted any
`Try it yourself:` suggestions:

- Type `print ` (with a trailing space) then Tab → libedit offers the
  suggestion arguments for that instruction's relevant registers.

Up-arrow should walk only previously typed commands. Neither command
names nor suggestions appear in the up-arrow history.

## Order of work

1. **Levels first** (Part 1). Smaller change. Section-gating is
   independent of completion. Smoke-test at each level before
   moving on.
2. **Tab completion second** (Part 2). Cleanly layers on top — it
   needs `try_it()` to exist as the call site, which Part 1 doesn't
   introduce but makes natural to add. The `add_suggestion` storage
   work is contained.

Both parts together: maybe a day's work for someone with the code
loaded.

## GUI compatibility

The user has a GUI session planned where the user clicks a button to
re-render an instruction at a higher level after the fact. This plan
sets up two things the GUI will need:

1. **`level` passed as a parameter** (not read from a global inside
   templates). The GUI can call `explain_render(saved_inst, level=3)`
   on an instruction it previously rendered at level=1.
2. **Sections are explicitly delineated** (each `if (level >= N)`
   block corresponds to a discrete output section). When the GUI work
   refactors to a struct-and-render model, the section boundaries are
   already the natural struct fields.

What the GUI work will need to add on top of this plan:

- A per-instruction structured representation (each section as a
  string field on a struct), so the GUI can hold a step's full
  rendered content and re-emit at a different level without
  re-executing.
- A snapshot-persistence change: today `explain_after` frees the
  pre-step register snapshot once the diff is printed. The GUI needs
  it kept around per step so the diff can be re-rendered later.

Neither is needed for the CLI work. The CLI doesn't lose anything by
keeping the current snapshot-frees-after-diff lifecycle.

## Risks / open questions

- **Level 1 may be too sparse.** Without inputs/outputs, the after-step
  diff is the only signal for what changed. Probably fine — that's the
  Patterson & Hennessy chapter-2.3 model — but worth verifying with a
  student before declaring it the recommended starting point.
- **Tab on multi-word suggestions.** Stored suggestions are full
  commands like `print $a0`, but once the student has typed `print `,
  the `text` argument libedit passes the generator is just `$a0` — a
  naive `strncmp("print $a0", "$a0", 3)` fails to match. The
  suggestion generator should strip a matching leading-word-and-space
  from each candidate before the prefix check, or maintain
  suggestions as just the operand portion when the command-prefix is
  fixed (always `print` today). Either works; the strip-prefix form
  is cheaper to implement and degrades gracefully if a future
  template emits a suggestion that doesn't start with `print`.
- **Suggestion list at startup / between programs.** Before the first
  step the list is empty; Tab does nothing. After `reinitialize` the
  list should probably also be cleared. Easy: clear in
  `initialize_world` or hook into the existing reinit path.
- **Symbol-table completion later.** The `spim_completion` dispatcher
  is the right place to also offer label names from
  `find_symbol_address`'s table and register names from
  `int_reg_names[]`. Either as a third generator (selected when
  `start > 0` and the first word is `print`) or by mixing them into
  `suggestion_generator`'s result list. Out of scope for this plan;
  noted so the design here doesn't lock it out.
