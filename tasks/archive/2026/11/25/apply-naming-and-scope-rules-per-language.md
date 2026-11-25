# Apply the naming and scope rules, per language (Python, Rust, C, Haskell, bash, JavaScript)

**Done:** 2026-11-25 (William Emerison Six <billsix@gmail.com>, the same day: "make a task in all of my repos for my projects in /mnt/sda1 to apply them, per language", then "make a task for all of them, but only start work on hanoi and spimulator"). Written by the fleet task in github.com/billsix/towersofhanoi (`tasks/archive/2026/11/25/apply-naming-and-scope-rules-per-language-across-repositories.md`).

## What this did

Applied the five naming and scope rules to the code in this repository that the maintainer wrote: inline a binding used once; two uses do not earn a name; declare at the point of use; nest a helper with one caller; shadow deliberately. Each language's rules are a section of its shared standard (github.com/billsix/runClaudeInContainer `tasks/reference/`): "Locals and helpers" in `c-coding-standard.md`, `rust-coding-standard.md`, `haskell-coding-standard.md` and `javascript-coding-standard.md`, "Names and scope in bash" in `shell-and-gate-scripts.md`, and "Inline a value used once…" and "What earns an extraction" in `python-coding-standard.md`. Behaviour is unchanged: every engine still gives the output the C engine gives.

The survey was `survey_names_and_scope.py` (same directory as the standards), run as `--exclude 'elpa/|^builddir/|^examples/|^pgu/'`: 816 lines of leads, functions named exactly twice and locals named exactly twice inside their function. Its counts are textual, so each lead was read or, in the Rust port, tried against the compiler.

## Scope (from `CLAUDE.md`)

- **In:** `src/` and `include/` (the simulator core); `tools/` except the vendored tools; the shell scripts; `tree-sitter/grammar.js`; the three engine ports under `port/`.
- **Out, teaching C:** `examples/` and `pgu/`. `CLAUDE.md` ("Modernization scope") opts the teaching C out of declare-at-use and helper extraction, because each demo is written for the clearest C-to-assembly mapping.
- **Out, vendored:** `tools/check_local_annotations.py`, `tools/c_dispatch_check.py` and `tools/c_designated_init_check.py` (byte-identical copies of shared tools); `pgu/upstreamSource/` and the `pgu/` Python (the book's own sources).
- **Out, generated:** the opcode tables in every language and `tree-sitter/scripts/keyword-lists.js`.

## Decision

**Rule 4 is not applied in `port/`** (William Emerison Six <billsix@gmail.com>, 2026-11-25: "keep the ports mirroring the C engine"). The ports are written "side by side with the C one" (`port/README.md`): a port function carries its C original's name (`parse_b2`, `parse_i2a`, `fill_line_buffer`, `maybe_snapshot_line`), so a reader can hold one against the other. C cannot nest, so a one-caller helper in a port stays a top-level function, or a method in Rust, like its C original. For the same reason the ports keep the parser's token reads (`parse_register`, `advance`, `value_integer`) as named statements, as the C engine does.

## C core (`src/`, `include/`)

- **Rule 4 changed nothing.** The 80 one-caller leads are the parser's per-directive and per-instruction-form functions, the scanner's per-token functions, the `emit_*` and `render_*` families, and callbacks (comparators, libedit generators, `atexit` handlers). Each is the arm of a dispatch or a function passed by address, which the standard keeps. All but eight are `static`. `__asan_default_options` is a name the sanitizer runtime looks up. The other seven have no caller at all; see "Found on the way".
- **Rule 1, five inlines:** `t` in `is_directive` and `length` in the `.ascii` directive (`src/parser.c`); `value` in the memory dump (`src/display-utils.c`); `mnemonic` in the modifier preamble and `bit_position` in `write_bits` (`src/explain.c`).
- **Left alone, by group:** the parser's `parse_register()` and `parse_expression()` results (each call consumes tokens, so the calls stay statements in source order); values saved before a later statement changes their source (`end = line_position` in the scanner, `x = data_top` before `expand_data`, `old_bare`, `saved_pseudo`, `errors_before`, `start_data_directive`); the encoding fields in `explain.c` (`source_register = (encoding >> 21) & 0x1f` and its siblings: the name is the field's name); the partial products `ac`, `ad`, `bd`, `cb` in the 64-bit multiply; `n` in the completion generator (read in a loop condition); `symbol` in `lcomm_directive` (the inlined form, a call followed by `->flag = true` on the next line, read worse). About half of the 130 "local" leads were assignments to file-scope variables, which the survey's pattern cannot tell from a declaration.

## Python tools (`tools/`)

`nest_one_caller.py` nested 22 one-caller helpers in `add_port_lines.py`, `check_asm_canonical.py`, `check_example_commands.py`, `check_float_format.py`, `const_candidates.py` and `gen_opcodes.py`. It skipped the helpers with a doctest, the ones another file names, and `read_table` (its caller rebinds a name it reads). Five of the six files were `ruff format` clean before and are again; `check_asm_canonical.py` was not formatted before and was left that way. Three comment and docstring lines that the deeper indent pushed past the limit were reworded to fit.

## The engine ports (rules 1 to 3 only)

- **Rust (`port/rust/src/`), 21 inlines in 10 files.** `inline_single_use_let.py` (below) took each one-line, non-`mut` `let` whose only use is on the next line of code with nothing but plain names evaluated before it, substituted it, and kept the change only if `cargo check --all-targets` still passed. Examples: `status` in `main` (`std::process::exit(spimulator::cli::main())`), `encoding` before `store_word` and `memory_write_word`, `program_counter` before `record_label` and `memory_write_word`, `line` before `parse_error_on_line`, `node` before `ast_append`, the two `text` strings in `cformat.rs`, `clamped`, `digits` and `exponent_i32` in `arith.rs`.
  - **Kept because the inlined form does not compile** (a borrow needs the binding): `id` before `self.symbols.label(id)` (four places), `id` from `record_label`, `breakpoint` from `self.breakpoints.remove(index)`, `count` and `end` in `syscall.rs`.
  - **Kept by rule:** token reads (20 candidates, per the Decision); `unsafe` blocks; `let mut` bindings; bindings whose value is read before a mutation (`old_top`, `old_bare`, `old_size`, `line_start`); a `match` scrutinee and an index start, where the substituted text would need parentheses the compiler reports as unused; anything longer than one line, or whose use is not on the next line (about 120 leads, where the name labels a multi-line `if`/`match` value such as `slt`, `load_high`, `ast_kind`).
- **Python (`port/python/spimulator_py/`), 5 inlines:** `high` in `instruction.py` (the immediate-fits test) and in `symbol_table.py`; `kind` before its `match` in `instruction.py`; `text` in the listing line of `spim.py`; `bucket` in `symbol_table.py`. `list_single_use_python.py` (below) listed 74 leads. Left: values bound outside a per-instruction `step` closure in `run.py` (14 leads; bound once, read on every execution, which the Python standard names as a reason to keep a binding); token reads; the opcode tables and tuples named for what they hold (`trap_opcodes`, `register_opcodes`); strings named before the f-string that prints them, where the inlined form would nest a `join` or a conditional inside the f-string.
- **Haskell (`port/haskell/src/`), 4 inlines:** `start` in `formatMemory` (`DisplayUtils.hs`), `aligned` in `SpimUtils.hs`, `value` in the float scanner (`Scanner.hs`), `bare` in `Main.hs`. 335 of the leads are monadic binds (`c :: Char <- peekChar`), which the standard keeps as statements. Of the 49 pure single-use bindings, the rest stay: values shared across a lambda or a loop (`value` in `SymbolTable.hs`), `where` bindings that name a `case` result later lines format (`body`, `symbolic`, `withSource`, `expo`), tables (`branches`, `loadStores`, `table`), local actions (`closeListing`, `readLine`, `checkInterrupt`), and `nd` and `tf` in the branch parser, which mirror the C engine's `nullify_delay` and `true_false`.

## bash and JavaScript

- **bash:** one inline, `root` in `port/python/lint.sh` (it fed one `cd`). No script has a one-caller function. The other leads were the two `meson setup` command lines in `entrypoint/buildDebug.sh`, which are not assignments.
- **JavaScript:** `tree-sitter/grammar.js` had no leads: its one helper, `kw`, is called 26 times.

## Second pass: the Python the first pass read only for nesting (2026-11-25)

The maintainer asked the same day whether every language was fully done ("if not, do it"). Two things were not: `tools/` and `port/harness/` had been read for one-caller helpers only, and the Python port's leads had been judged by hand, five of them applied.

- **`inline_single_use_python.py`** (below) inlined 15 single-use annotated bindings: `body` in `tools/gen_opcodes.py`; `worst` in `port/harness/compare_all_programs.py` and `compare_engines.py`; `module` in `port/python/run_doctests.py`; and in the port `cflag` (`console.py`), `computed` (`instruction.py`), `kind`, `bits` and `current` (`parser.py`), `signed`, `quotient` and three `slt` (`pseudo_op.py`), `cause` (`traps.py`).
- **The line it draws, which is also why the Rust port's other leads stay:** a binding is inlined only when its value fits on one line of source and nothing with an effect runs before it in the statement that reads it. A value that takes several lines (a table, a conditional, a long computation) keeps its name. A first run without that limit inlined 43 bindings and was discarded: three-line conditionals landed inside call arguments, and `a`, `b` and `accumulator` of the multiply-add became one seven-line expression. The shared standard asks for a comment naming an inlined value; a script cannot write that comment, and without it those inlines lost information.
- **Also kept:** a value read inside an f-string (the substituted expression is hard to read there); a value whose reader is inside a nested statement, a lambda, a comprehension, a conditional branch or a later `and`/`or` operand (it would run later, repeatedly, only sometimes, or under a different exception handler); `subprocess.run` results; token reads and `record_label` (the Decision).
- **The Rust port needed nothing more:** its codemod already applied the same one-line limit. Its remaining leads are multi-line values (`slt`, `load_high`, `ast_kind`, `name`, the `text` strings in `listing.rs`) or bindings read several lines later, where the name is what the reader follows across those lines.

**Gate for the second pass:** `make image`, default flags, exit 0; 385 tests OK and 0 failed, then 54 OK under UBSan and 54 under ASan. Before it, in the sandbox: `tools/gen_opcodes.py --check` exit 0, `port/python/lint.sh` exit 0, `compare_engines.py` 35 of 35 for the Python port.

## Found on the way

- **Seven functions in the core have a definition and a header declaration and no caller:** `assembler_fire_align`, `assembler_fire_segment_change`, `ast_make_error`, `extern_directive`, `lcomm_directive`, `initialize_stack` and `print_undefined_symbols`. `git grep -w` finds each only in its `src/*.c` file and its header (searched `src/`, `include/`, `tests/`, `examples/`, `port/harness/`). They are non-`static`, so `-Wunused-function` does not report them. This task did not remove them: whether each is dead or kept for a planned use is the maintainer's call.
- **The survey tool missed Haskell's two-line bindings** (`let x :: T` then `x = e`): the signature line counted as a use. Fixed in the shared tool (github.com/billsix/runClaudeInContainer `tasks/reference/survey_names_and_scope.py`); the 49 pure Haskell leads above come from the fixed version.

## Verification (2026-11-25)

- **`make image`, default flags, nested, after the first half** (C core, `tools/`, bash): exit 0; 385 tests OK and 0 failed in the full suite, then 54 OK under UBSan and 54 OK under ASan.
- **`make image` again with the port changes in:** exit 0; 385 tests OK and 0 failed in the full suite (the port suites included, each comparing a port with the C engine), then 54 OK under UBSan and 54 OK under ASan.
- **Before the gate, in the sandbox:** a scratch clang build of the core under `-Werror`, with `meson test --suite regression --suite examples` 98 OK; `tools/gen_opcodes.py --check` exit 0; `tools/check_asm_canonical.py` over every `examples/**/*.asm` printing the same 47 lines as the version before the change; `tools/check_float_format.py` "identical to glibc on 158029 values"; `port/python/lint.sh` clean with 38 doctests; `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings` and `cargo test` clean for the Rust port; the Haskell port built under `-Wall -Werror` and `ormolu --mode check` clean; `port/harness/compare_engines.py` against the scratch C engine 35 of 35 for Rust and for Python. For Haskell it gave 34 of 35 in the sandbox: `core_builtin` stops with "Cannot parse embedded exception handler", and a build of the unmodified sources made the same way fails identically, so it is a property of that scratch build, not of this change (cause not looked into; the image gate is the check that counts).

## How

Four scripts, removed with this archive and kept in this repository's history under `tasks/adhoc/apply-naming-and-scope-rules-per-language/`:

- `inline_single_use_let.py`: the Rust codemod described above. It applies one candidate at a time and puts back any that does not compile. An inline can expose one more candidate, so it was run until it reported none (two passes changed something, the third nothing).
- `list_single_use_python.py`: read-only; lists annotated assignments stored once and loaded once, by the statement that follows.
- `inline_single_use_python.py`: the Python inliner of the second pass, working from the `ast` module's positions; it repeats until a pass changes nothing.
- The C, Haskell and bash edits, the `tools/` nesting (`nest_one_caller.py`) and the first five Python-port inlines were made by hand or with the shared tool.
