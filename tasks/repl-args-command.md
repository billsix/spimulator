# Task: `args` REPL command (gdb-style `set args`)

## Goal

Let a student at the `(spim)` prompt change the arguments their
program will receive on the next `run`, without quitting and
restarting spim.

Target session:

```
$ spimulator
(spim) read "fact.asm"
(spim) args 5
(spim) run
120
(spim) args 10
(spim) run
3628800
(spim) args 13
(spim) run
1932053504
```

Same shape as gdb's `set args` (and `run arg1 arg2 ...` shortcut),
but adapted to spim's command vocabulary.

## Why this is worth doing

Now that command-line argv works (see
[`argv-command-line-handling.md`](archive/2026/05/23/argv-command-line-handling.md)),
students iterating on numeric inputs for `factorial`, `gcd`,
`fizzbuzz`, etc. have to keep `quit`-ing spim and re-invoking it
with a different command line.  Each restart loses breakpoints,
register watches, and any explain-mode context.

`(spim) args N` keeps the same session.  Pedagogically this is
exactly the gdb workflow students will eventually use elsewhere,
which is good — they're learning a transferable habit.

## State of the world today

`/src/spim.c:740`:

```c
case RUN_CMD: {
  static mem_addr addr;
  ...
  addr = (redo ? addr : ((mem_addr)get_opt_int()));
  if (addr == 0) addr = starting_address();
  initialize_run_stack(program_argc, program_argv);    // <— uses current
  ...
}
```

The `run` command's optional argument is a *start address*, not
program args.  `program_argc` and `program_argv` are global file-
scope variables set ONLY by the command-line parser at startup.
There is no REPL command that touches them — they're effectively
frozen the moment spim launches.

The REPL's full command list lives in two places that must stay
in sync (per the existing comment at `spim.c:110`):

1. `spim_commands[]` — the tab-completion table.
2. `read_assembly_command()` — the `str_prefix` dispatch that
   maps typed strings to `*_CMD` enum values.

The switch in `top_level()` handles each enum value.

## Proposed implementation

### 1. New enum value

In the existing `enum`/`#define` block that names the `_CMD`
values (search for `RUN_CMD` to find it), add `ARGS_CMD`.

### 2. Tab completion

Insert `"args"` into `spim_commands[]` (`spim.c:113`) in
alphabetical position (between `"add"`-ish — actually after
`"args"` ought to sort right after the start; alphabetically
`args` precedes `breakpoint`).

### 3. Command dispatch in `read_assembly_command()`

Search for the lines that look like:

```c
else if (str_prefix((char*)yylval.p, "run", 2))
  return RUN_CMD;
```

Add (note: minimum match should be unambiguous — `a`-prefix
otherwise hits `add`-ish commands; pick at least 2 chars):

```c
else if (str_prefix((char*)yylval.p, "args", 2))
  return ARGS_CMD;
```

### 4. New case in `top_level()`'s switch

```c
case ARGS_CMD: {
  /* Read every token until newline.  Each becomes one argv entry
     starting at argv[1] — argv[0] stays as the loaded program's
     path so demos that print argv[0] still work. */
  static char**       new_argv = NULL;   /* owned malloc'd vector */
  static int          new_argv_cap = 0;
  static int          new_argv_len = 0;

  /* Free any previous args we own. */
  if (new_argv != NULL) {
    for (int i = 0; i < new_argv_len; i++) free(new_argv[i]);
    free(new_argv);
    new_argv = NULL; new_argv_cap = 0; new_argv_len = 0;
  }

  /* Read tokens off the REPL line. */
  int t;
  while ((t = read_token()) != Y_NL && t != 0) {
    char* s = (t == Y_STR || t == Y_ID) ? strdup((char*)yylval.p)
                                        : strdup_token_text(t);
    /* Grow the vector. */
    if (new_argv_len + 1 >= new_argv_cap) {
      new_argv_cap = new_argv_cap == 0 ? 4 : new_argv_cap * 2;
      new_argv = realloc(new_argv, new_argv_cap * sizeof(char*));
    }
    new_argv[new_argv_len++] = s;
  }

  /* Splice into program_argc/argv.  Keep argv[0] = whatever the
     last command-line gave us so `print argv[0]` still works. */
  static char**  spliced = NULL;
  free(spliced);
  spliced = malloc((new_argv_len + 1) * sizeof(char*));
  spliced[0] = program_argv ? program_argv[0] : (char*)"<repl>";
  for (int i = 0; i < new_argv_len; i++) spliced[1 + i] = new_argv[i];
  program_argc = 1 + new_argv_len;
  program_argv = spliced;

  prev_cmd = ARGS_CMD;
  return (0);
}
```

(The `strdup_token_text` helper is a small new function that
returns a printable rendering of a non-string scanner token — for
integers it'd be the decimal text, etc.  If the scanner already
preserves the source text, this could be even simpler.)

### 5. Help text

`spim.c` prints a one-line summary per command (search for the
existing `help` command's output).  Add:

```
args [ARG ...]   Set the args the next `run` will pass to the program
                 (gdb-style; argv[0] stays as the loaded program path).
```

## Test plan

New file: `tests/tt.args-cmd.s` that just prints the byte at
`argv[1][0]` and exits.  Driven from the REPL by piping commands
via stdin:

```sh
printf 'read "tt.args-cmd.s"\nargs zebra\nrun\n' \
    | spimulator > test.out
grep -q '^z$' test.out || exit 1

printf 'read "tt.args-cmd.s"\nargs amber\nrun\n' \
    | spimulator > test.out
grep -q '^a$' test.out || exit 1
```

Two invocations of the same `tt.args-cmd.s` in the same spim
session would prove that `args` between `run`s actually takes
effect.  Could be one batched script:

```sh
printf 'read "tt.args-cmd.s"\nargs zebra\nrun\nargs amber\nrun\nquit\n' \
    | spimulator > test.out
[ "$(cat test.out | tr -d '\n')" = "(spim) (spim) z(spim) (spim) a(spim) " ] || exit 1
```

(The exact match string depends on whether `-quiet` is in
effect; pick whatever's deterministic.)

Add the new test invocation to `Dockerfile` next to the existing
test block.

## Out of scope

- **`show args`** companion command.  Useful but not strictly
  needed; can come in a follow-up.
- **`run arg1 arg2 ...`** (gdb's shortcut form).  `run`'s optional
  argument is already used for a start address; making it both
  would be ambiguous (`run 0x400024` vs `run 5` — is `5` an
  address or an arg?).  Keep `args` and `run` separate.
- **Quoted args** (`args "two words"`).  spim's scanner already
  recognises `Y_STR` for quoted tokens; the prototype above
  handles them.  But escapes in those strings (`\n`, `\xff`) are
  out of scope for v1.
- **Environment variables** (gdb's `set env`).  Spim's runtime
  arranges envp in registers, but no /examples demo uses it; punt.
- **Clearing args.**  `args` with no operands could either clear
  the args or print the current ones.  Pick one in v1 — gdb's
  `set args` (no value) clears; we could match.  Or have empty-
  args mean "show current."  Decide before implementing.

## Verification expected

- `meson compile -C builddir` clean.
- All existing tests (`tt.bare.s`, `tt.core.s`, `tt.le.s`,
  `tt.argv.s`) still pass.
- New `tt.args-cmd.s` driven via stdin produces the expected
  output across multiple `args`/`run` cycles in one session.
- Manual: walk through the "target session" listed at the top of
  this doc and confirm the outputs match.
