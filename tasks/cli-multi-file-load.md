# Allow multiple `-f` files on the spim command line

## Goal

Let `spimulator -f a.asm -f b.asm args...` load both files into
one symbol table the same way the REPL's repeated `load`
commands do today.  Enables a curriculum-friendly library
workflow: `spimulator -f libio.asm -f libstr.asm -f prog.asm
args...`.

## Why

The underlying machinery already supports it.  The REPL `load`
command at `src/spim.c:851` calls `read_assembly_file`, which
**accumulates** rather than replacing — text, data, and symbols
from a second load merge with what's already there, with
forward references resolving across files.  Only the
command-line parser at `src/spim.c:486-510` artificially blocks
this by guarding with `if (assembly_file_loaded) continue;` and
then `break;`-ing out of the arg loop after the first file.

A new `examples/` direction (a teaching libc — see
`PLAN-libctype.md`, `PLAN-libstr.md`, `PLAN-libstdlib.md`)
wants a clean CLI shape that matches real linker UX (`gcc a.o
b.o c.o`) so demos can be invoked the same way with or without
libraries.

## State of the world

Relevant block, `src/spim.c:486-510`:

```c
} else if (((streq(argv[i], "-file") || streq(argv[i], "-f")) &&
            (i + 1 < argc))
           || (argv[i][0] != '-')) {
  if (assembly_file_loaded) continue;       /* blocks second -f */

  int program_i = (argv[i][0] == '-') ? (i + 1) : i;
  program_argc = argc - program_i;
  program_argv = &argv[program_i];

  initialize_world(load_exception_handler ? exception_file_name : nullptr,
                   !quiet);
  initialize_run_stack(program_argc, program_argv);
  assembly_file_attempted = true;
  assembly_file_loaded = read_assembly_file(argv[program_i]);

  break;                                    /* exits the arg loop */
}
```

Two structural problems for multi-`-f`:

1. `initialize_world` + `initialize_run_stack` run **inside** the
   per-file branch.  For multi-file, these should run ONCE
   before any file is read; a second call would reset state and
   wipe the first file's content.
2. `break;` ends arg processing after one file — must become
   `continue;` for explicit `-f`, and only `break` when we hit a
   non-flag positional arg (which becomes the start of program
   argv).

## The change

Rough shape; exact code on landing:

1. **Hoist initialization.**  Move `initialize_world` +
   `initialize_run_stack` to run once, before the arg loop's
   first `-f`.  Track with a `bool world_initialized = false;`
   so it fires lazily — i.e., only when we actually have a file
   to load, not when `-f` never appears.  This keeps the
   no-file REPL-only path (`spimulator` with no args)
   identical.
2. **Compute program_argv based on the bare-arg position.**  In
   the positional case (`spimulator main.asm args...`), the
   bare arg is both the file and `argv[0]` per current
   convention — keep that.  In the explicit-`-f` case, program
   argv starts at the first non-`-f` non-flag token after all
   `-f` files.
3. **Loop properly.**  For explicit `-f`: read the file,
   continue the loop.  For positional bare-arg: still read,
   then break out as today (a bare arg is a positional terminus
   — anything after is program argv).
4. **Initialize the run stack after all files are loaded.**
   `initialize_run_stack` should see the final program_argv
   — so call it once, after the arg loop completes, before
   `run_spim`.

## Backwards compatibility

| Invocation | Today | After change |
|---|---|---|
| `spimulator -f main.asm args...` | works | unchanged |
| `spimulator main.asm args...` | works | unchanged |
| `spimulator -f libio.asm -f main.asm args...` | only libio.asm loads (second `-f` dropped); `main.asm` silently ignored | **new**: both load, `args...` is program argv |
| `spimulator main.asm libio.asm args...` | `main.asm` loaded; `libio.asm` is `argv[1]` | unchanged (positional shortcut still single-file) |
| `spimulator` (REPL only) | works | unchanged |

The positional shortcut intentionally stays single-file —
positional-multi is ambiguous (where do files end and args
begin?), and the explicit-`-f` form is the right answer for
multiple files anyway.

## Tests

Add `tests/tt.multifile.s` and `tests/tt.multifile.helper.s`:

- `tt.multifile.helper.s` defines `helper_double:` that returns
  `$a0 * 2` in `$v0` (small, no `$ra` concerns).
- `tt.multifile.s` has `main:` that loads `$a0 = 21`, `jal
  helper_double`, then exits with `$v0` as the status.

Test driver runs both ways:

```sh
spimulator -f tt.multifile.helper.s -f tt.multifile.s
echo "exit=$?"         # expect 42
spimulator -f tt.multifile.s -f tt.multifile.helper.s
echo "exit=$?"         # expect 42 (load order shouldn't matter — forward refs)
```

Regression: existing `tt.argv.s` (which tests the positional
shortcut + argv handling) must still pass unchanged.

## Verification

1. `ninja -C builddir` clean.
2. `meson test -C builddir` → 22/22 green (now 23/23 with the
   new test).
3. Manual run of all five rows in the backwards-compat table.
4. Manual REPL session: `load "tt.multifile.helper.s"; load
   "tt.multifile.s"; run` — should match CLI behavior.

## Out of scope

- New file-discovery flags (no `-L` search path, no `.spim`
  config file).
- ELF object files, real linking, library archives — spim
  doesn't have any of this.
- Renaming `-f` or deprecating the positional shortcut.

## Status

Landed 2026-05-23.  23/23 meson tests green (was 22; new
`multifile` test added).

### What landed

`src/spim.c`:

- Two new locals: `bool world_initialized` and `int last_file_i`.
- Rewrote the `-f` / positional-bare-arg branch (around line
  486):
  - Lazy-init the simulator world the first time any file
    needs to load.
  - Removed the `assembly_file_loaded` guard for explicit `-f`
    so it can repeat; kept it as a positional-shortcut
    terminator (bare arg after a loaded file → starts program
    argv).
  - Explicit `-f` advances past the filename and `continue`s
    the loop; positional shortcut still `break`s after one file.
  - On `read_assembly_file` failure, `break` out (preserves the
    "exit 2 on fopen failure" behavior via the existing
    post-loop check).
- Added post-loop fixup: if files loaded via explicit `-f` but
  no trailing positional args set `program_argc/argv`, derive
  them from `last_file_i` so argv[0] is the last loaded file.

`tests/tt.multifile.s` + `tests/tt.multifile.helper.s`: two-file
regression covering forward references across files (the
helper defines `helper_double`, the main file calls it).  Test
driver runs both load orders; both must pass.

`tests/run-test.sh` + `meson.build`: new `multifile` test
registered.

### Verified backwards-compat matrix

All five rows from the plan + the new multi-`-f` rows:

| Invocation | Result |
|---|---|
| `spimulator -f main.asm args...` | unchanged |
| `spimulator main.asm args...` | unchanged |
| `spimulator -f libio.asm -f main.asm args...` | works, args reach program |
| `spimulator main.asm libio.asm args...` | unchanged (positional one-shot) |
| `spimulator -f /nonexistent.s` | exit 2, error printed (unchanged) |
| `spimulator -f tt.multifile.s` (forward ref unresolved) | exit 1, "undefined symbol: helper_double" (unchanged) |
| `spimulator` (no args) | drops into REPL (unchanged) |
