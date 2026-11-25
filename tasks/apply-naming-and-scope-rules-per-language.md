# Apply the naming and scope rules, per language (Python, Rust, C, Haskell, bash, JavaScript)

**Status:** in progress — the C core, the repository tools, bash and the grammar are done (2026-11-25); the three engine ports under `port/` remain, and one question about them is open
**Priority:** 7 (a readability sweep with no deadline)
**Difficulty:** 6 (what remains is reading about 590 leads in the ports, most of which stay)
**Created:** 2026-11-25 (William Emerison Six <billsix@gmail.com>: "make a task in all of my repos for my projects in /mnt/sda1 to apply them, per language", and the same day "make a task for all of them, but only start work on hanoi and spimulator").
**Source:** github.com/billsix/towersofhanoi `tasks/archive/2026/11/25/apply-naming-and-scope-rules-per-language-across-repositories.md` (the fleet task that wrote this one).

## BLUF

Five naming and scope rules are written for every language in the shared coding standards. This task applies them to the code in this repository that the maintainer wrote. The simulator core (`src/`, `include/`), `tools/`, the shell scripts and the tree-sitter grammar are done and gated. What is left is the engine ports (`port/python`, `port/rust`, `port/haskell`): rules 1 to 3 on their locals, and a decision on rule 4 there (open question 1). Done means the ports are surveyed and either changed or recorded as left alone, with `make image` green.

## Context

**The five rules:** inline a binding used once (a comment above the expression names it); a binding used twice does not automatically earn a name; declare a name at the point of use; nest a helper with one caller inside that caller, where the language has a nested function; shadow deliberately when nesting, where the language allows it. Each language's standard says how the rule reads there and which bindings must stay (github.com/billsix/runClaudeInContainer `tasks/reference/`):

| Language | Where here | Section | What differs from Python |
|---|---|---|---|
| Python | `port/python/`, `tools/` | `python-coding-standard.md`, "Inline a value used once…" and "What earns an extraction" | the baseline; `nest_one_caller.py` nests mechanically |
| Rust | `port/rust/` | `rust-coding-standard.md`, "Locals and helpers" | nested `fn`, or a closure when the helper captures a local |
| C | `src/`, `include/` | `c-coding-standard.md`, "Locals and helpers" | a one-caller helper is `static`, directly above its caller; no shadowing |
| Haskell | `port/haskell/` | `haskell-coding-standard.md`, "Locals and helpers" | a one-caller helper goes in its caller's `where`; no shadowing |
| bash | `entrypoint/`, `port/`, `tests/`, `examples/tests/` | `shell-and-gate-scripts.md`, "Names and scope in bash" | rule 4 does not apply |
| JavaScript | `tree-sitter/grammar.js` | `javascript-coding-standard.md`, "(b) Judgment calls — locals and helpers" | as Python |

**The survey tool:** `python3 ~/.claude/reference/survey_names_and_scope.py --exclude 'elpa/|^builddir/|^examples/|^pgu/'` (Crush: `~/.config/crush/reference/`). It lists functions named exactly twice and locals named exactly twice inside their function; the counts are textual, so every line is a lead to read. On 2026-11-25 it printed 816 lines here.

## Scope (settled from `CLAUDE.md`, 2026-11-25)

- **In:** `src/` and `include/` (the simulator core, which follows the shared C standard); `tools/` except the vendored tools; the shell scripts; `tree-sitter/grammar.js`; the three ports.
- **Out, teaching C:** `examples/` and `pgu/`. `CLAUDE.md` ("Modernization scope") opts the teaching C out of declare-at-use, helper extraction and the like, because each demo is written for the clearest C-to-assembly mapping.
- **Out, vendored:** `tools/check_local_annotations.py`, `tools/c_dispatch_check.py` and `tools/c_designated_init_check.py` are byte-identical copies of shared tools; `pgu/upstreamSource/` and the `pgu/` Python are the book's own sources.
- **Out, generated:** the opcode tables in every language and `tree-sitter/scripts/keyword-lists.js` (`tools/gen_opcodes.py` writes them).

## Done (2026-11-25)

**C core.**
- Rule 4 changed nothing. The 80 one-caller leads are the parser's per-directive and per-instruction-form functions, the scanner's per-token functions, the `emit_*` and `render_*` families, and callbacks (`qsort`-style comparators, libedit generators, `atexit` handlers). Each is the arm of a dispatch or a function passed by address, which the standard keeps. All but eight are already `static`; `__asan_default_options` is a name the sanitizer runtime looks up, and the other seven turned out to have **no caller at all**: `assembler_fire_align`, `assembler_fire_segment_change`, `ast_make_error`, `extern_directive`, `lcomm_directive`, `initialize_stack` and `print_undefined_symbols` are each defined in one `src/*.c` file and declared in its header, and `git grep -w` finds no other use in `src/`, `include/`, `tests/`, `examples/` or `port/harness/` (the survey counted the declaration as the "one call"). They are non-`static`, so `-Wunused-function` does not report them. This task did not remove them: whether each is dead or kept for a planned use is the maintainer's call (see "Found on the way").
- Rule 1, five inlines: `t` in `is_directive` and `length` in the `.ascii` directive (`src/parser.c`); `value` in the memory dump (`src/display-utils.c`); `mnemonic` in the modifier preamble and `bit_position` in `write_bits` (`src/explain.c`).
- Left alone, by group: the parser's `parse_register()` and `parse_expression()` results (each call consumes tokens, so the calls stay statements in source order); values saved before a later statement changes their source (`end = line_position` in the scanner, `x = data_top` before `expand_data`, `old_bare`, `saved_pseudo`, `errors_before`, `start_data_directive`); the encoding fields in `explain.c` (`source_register = (encoding >> 21) & 0x1f` and its siblings: the name is the field's name); the partial products `ac`, `ad`, `bd`, `cb` in the 64-bit multiply; `n` in the completion generator (read in a loop condition); `symbol` in `lcomm_directive` (the inlined form, a call followed by `->flag = true` on the next line, read worse). About half of the 130 "local" leads were assignments to file-scope variables, which the survey's pattern cannot tell from a declaration.

**Python tools.** `nest_one_caller.py` nested 22 one-caller helpers in `tools/add_port_lines.py`, `check_asm_canonical.py`, `check_example_commands.py`, `check_float_format.py`, `const_candidates.py` and `gen_opcodes.py`. It skipped the helpers with a doctest, the ones another file names, and `read_table` (its caller rebinds a name it reads). Five of the six files were `ruff format` clean before and are again; `check_asm_canonical.py` was not formatted before and was left that way. Three comment and docstring lines that the deeper indent pushed past the limit were reworded to fit.

**bash.** One inline: `root` in `port/python/lint.sh` (it fed one `cd`). No script has a one-caller function. The other leads were the two `meson setup` command lines in `entrypoint/buildDebug.sh`, which are not assignments.

**JavaScript.** `tree-sitter/grammar.js` had no leads: its one helper, `kw`, is called 26 times.

**Verification.**
- A scratch build (`CC=clang meson setup`, `--buildtype=debug`): the core compiled under `-Werror`; `meson test --suite regression --suite examples` gave 98 OK, 0 failed. That run includes `generated_opcode_tables`, `dispatch_check`, `designated_init_check` and `documented-commands` (which runs `tools/check_example_commands.py`).
- `python3 tools/gen_opcodes.py --check` exit 0 (the generator still writes the committed tables byte for byte).
- `tools/check_asm_canonical.py` over every `examples/**/*.asm`: 47 lines of output, identical to the output of the version before the change, exit 0.
- `tools/check_float_format.py`: "identical to glibc on 158029 values". `tools/const_candidates.py` ran to completion on the scratch build.
- `port/python/lint.sh`, run from another directory: ruff, ty, the annotation checker and 38 doctests all passed; `shellcheck` is quiet on it and its mode is still 100755.
- **`make image`** (the repository's gate, default flags, nested): see "Gate" below.

## Remaining: the engine ports

The survey's leads there: Rust 55 one-caller functions and about 190 single-use locals; Haskell 5 and about 340; Python is surveyed with `nest_one_caller.py --dry-run $(git ls-files 'port/python/*.py')`.

- [ ] Answer open question 1, then apply or record rule 4 in each port.
- [ ] Rules 1 to 3, port by port. A sample of the leads on 2026-11-25 suggests most stay: in Haskell nearly all are monadic binds (`c :: Char <- peekChar`), which the standard keeps as statements; in Rust most are parser calls that consume tokens in order, values read before a mutation, or bindings a later borrow needs. Read them all the same; expect a few dozen inlines, not hundreds.
- [ ] Gate: `make image` (it builds and tests every port against the C engine).
- [ ] Done-record and archive.

## Found on the way

Seven functions in the core have a definition and a header declaration and no caller (listed under "C core" above). Removing them, or the dead declarations, is a separate change with its own decision; nothing here depends on it.

## Gate

(filled in when the `make image` run of 2026-11-25 finishes)

## Open questions

1. **Rule 4 in the ports.** The ports are written "side by side with the C one" (`port/README.md`): the Rust port's one-caller functions carry the C engine's names (`parse_b2`, `parse_i2a`, `fill_line_buffer`, `maybe_snapshot_line`), so a reader can hold a port function against its C original. C cannot nest, so nesting them in a port would break that correspondence. (a) Leave rule 4 unapplied in `port/`, keeping one port function per C function, which is the agent's recommendation; or (b) nest one-caller helpers in the ports as each language's standard says.
