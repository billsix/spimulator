# Task: fix argv command-line handling

## Goal

A user can pass arguments to a MIPS program on the spimulator
command line and have them reach `main` via `$a0` (argc) and `$a1`
(argv) the standard way:

```
spimulator -file fact.asm 5
spimulator -file gcd.asm 1071 462
spimulator -file cat.asm /etc/motd
```

After this fix, `argc` reflects the number of tokens after
`-file` (inclusive of the program's own name as argv[0]) and each
`argv[i]` is a pointer to the null-terminated string the user
typed.

## Why this is worth doing

Today, the `/examples` curriculum can only port stdin-only sbase
tools because spimulator's command-line parser silently drops all
but the last user argument.  `cat <file>`, `echo <args>`,
`head -n N <file>`, `tee`, `mkfifo`, and friends are blocked.
With argv working, Phase 4 of `/examples/PLAN-unix-tools.md`
becomes implementable, and several `PLAN-cs-demos.md` entries
(GCD with command-line operands, factorial with N from argv,
fizzbuzz with arbitrary upper bound) become natural rather than
hardcoded.

The runtime is already wired up — `exceptions.s` does the
standard

```
__start:
    lw    $a0, 0($sp)     # argc
    addiu $a1, $sp, 4     # argv
    ...
    jal main
```

and `initialize_run_stack()` (`src/spim-utils.c:238`) lays argc,
argv, and envp onto the simulated stack in the conventional
layout.  All that's broken is the option parser.

## Bug — what's wrong today

In `src/spim.c` lines 454-470 the relevant branch reads:

```c
} else if (((streq(argv[i], "-file") || streq(argv[i], "-f")) &&
            (i + 1 < argc))
           || (argv[i][0] != '-')) {
  int program_i = (argv[i][0] == '-') ? (i + 1) : i;
  program_argc = argc - program_i;
  program_argv = &argv[program_i];

  if (!assembly_file_loaded) /* Only load one file */
  {
    initialize_world(load_exception_handler ? exception_file_name : NULL, !quiet);
    initialize_run_stack(program_argc, program_argv);
    assembly_file_loaded = read_assembly_file(argv[program_i]);
  }
  i = program_i;
}
```

The disjunct `|| (argv[i][0] != '-')` is meant to catch the case
where the user omits `-file` and just types `spimulator
foo.asm`.  But after the file has been loaded, the parser keeps
walking, and every subsequent non-dash token (any program arg)
re-enters this branch and overwrites `program_argc` /
`program_argv` to progressively smaller slices.  The final value
of `program_argc` is `1` and `program_argv = ["<last_arg>"]`.

The `initialize_run_stack` call at line 527 (the run-the-file
case) then uses these stomped values.

Demonstration before the fix:

```
$ spimulator -file test_argv.s alpha beta gamma
argc=1 argv[0]=gamma
```

(expected: `argc=4 argv[0]=test_argv.s`)

## Proposed fix

A small change to the same branch:

```c
} else if (((streq(argv[i], "-file") || streq(argv[i], "-f")) &&
            (i + 1 < argc))
           || (argv[i][0] != '-')) {
  if (assembly_file_loaded) continue;       // already captured argv
  int program_i = (argv[i][0] == '-') ? (i + 1) : i;
  program_argc = argc - program_i;
  program_argv = &argv[program_i];

  initialize_world(load_exception_handler ? exception_file_name : NULL, !quiet);
  initialize_run_stack(program_argc, program_argv);
  assembly_file_loaded = read_assembly_file(argv[program_i]);
  break;                                    // remaining tokens are program argv
}
```

Two new lines:

1. `if (assembly_file_loaded) continue;` — once a file is loaded
   we never need to re-enter this branch.  Skip anything else
   the loop tries to feed in.
2. `break;` — after we've captured program_argc/argv and loaded
   the file, stop parsing spim options.  Anything after the
   program's .asm path belongs to the program, not to spim.

Side effect: spim flags that come AFTER `-file foo.asm` will no
longer take effect — they'll be passed to the program as argv
strings.  That's the conventional Unix order (`prog -options
args ...` not `prog args ... -options`), and our examples all
follow it.  Document this in the usage message if needed.

## Test plan

New file: `tests/tt.argv.s`.  Invoked with three known string
args; verifies `$a0`, `argv[1][0]`, `argv[2][0]`, `argv[3][0]`.
On all-pass prints `Passed all tests` (the existing test
convention); on any failure prints `Failed test` and exits.

Dockerfile invocation (runs after `meson install` so the default
exception-handler path resolves; no `-ef` needed):

```sh
spimulator -f tt.argv.s alpha beta gamma >& test.out
tail -n 1 test.out | grep -q "^Passed all tests$" || exit 1
```

The test FAILS on the unpatched code (because argc comes back
as 1) and PASSES after the patch.  Regression coverage going
forward.

## Verification done locally

- Build: `meson compile -C builddir`.
- Install: `meson install -C builddir`.
- Manual: `spimulator -f tests/tt.argv.s alpha beta gamma`
  ⇒ "Passed all tests".

(While iterating, in a build tree that hasn't been installed
yet, you can still run `./builddir/spimulator -ef src/exceptions.s
-f tests/tt.argv.s ...` — the long form is just the dev-tree
fallback when the default path doesn't resolve.)

## Out of scope

* envp.  `initialize_run_stack` already sets up envp on the
  stack and the runtime puts a pointer in `$a2`, but no demo
  needs it yet.  Could be tested separately.
* argv argument quoting.  Shell handles that; spim sees pre-split
  tokens.
* Interactive REPL `run <args>` support.  The REPL's `run`
  command takes a start address, not program args.  A future
  task could add `run with args`.
