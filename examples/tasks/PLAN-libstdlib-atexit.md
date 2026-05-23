# Plan: libstdlib — atexit + exit (cleanup-on-exit chain)

## Goal

Add `atexit(void (*fn)(void))` and `exit(int status)` to
libstdlib.  `atexit` registers a function to run when the
program exits normally; `exit` walks the registered list in
reverse-registration order before calling `_Exit` to actually
terminate.

This is the second `jalr` lesson in the curriculum (the first
is `bsearch`'s comparator), so this task is ordered after
bsearch — students see `jalr` first in a focused single-call
context, then again as iteration over a function-pointer table.

## Why this helps a novice

- **Concrete, observable.**  Register a handler, exit, see the
  handler run.  Every step has a print.  No async magic.
- **Reinforces `jalr` from a second angle.**  Where bsearch
  `jalr`s a single comparator, exit `jalr`s every entry in a
  table walked in reverse — students see indirect call as the
  table-driven dispatch primitive it really is.
- **Sets up the `noreturn` discipline.**  exit never returns;
  the asm version, like _Exit, deliberately doesn't have a
  `jr $ra` after the final call into _Exit.  Worth a comment.
- **Honest about scope.**  This is NOT real signal handling
  (POSIX `signal`/`sigaction` etc.) — see
  [`PLAN-libstdlib.md`](PLAN-libstdlib.md) and the discussion
  in SESSION_NOTES around 2026-05-23 for why spim's
  single-threaded simulator can't honestly implement those.
  atexit is the "exit cleanup" half of what students might
  initially conflate with "signal handlers".

## Functions

| Function | C signature | Behavior |
|---|---|---|
| `atexit` | `int atexit(void (*fn)(void))` | append fn to handlers list; return 0 on success, -1 if list is full |
| `exit`   | `__attribute__((noreturn)) void exit(int status)` | walk handlers in reverse order, calling each via fn(); then `_Exit(status)` |

## Calling-convention contract

Same as the rest of libstdlib:
- `$a0` — input (function pointer for atexit; int status for exit)
- `$v0` — return value (atexit only)
- `$s*` — preserved
- `$ra` — preserved (atexit returns normally; exit doesn't)

exit is non-leaf — it `jalr`s registered handlers in a loop.
Needs a frame to save `$ra` plus the loop-state `$s` regs.

## Implementation sketch

### C side (libstdlib.c, ~25 lines added)

```c
#define MAX_ATEXIT 32
static void (*handlers[MAX_ATEXIT])(void);
static int handler_count = 0;

int atexit(void (*fn)(void)) {
    if (handler_count >= MAX_ATEXIT) return -1;
    handlers[handler_count++] = fn;
    return 0;
}

__attribute__((noreturn)) void exit(int status) {
    /* POSIX: handlers run in reverse registration order. */
    for (int i = handler_count - 1; i >= 0; i--)
        handlers[i]();
    _Exit(status);
}
```

`MAX_ATEXIT = 32` matches the C99 minimum; bigger isn't worth
the .data footprint in a teaching lib.  Plenty for any demo.

### Asm side (libstdlib.asm, ~50 lines added)

```
        .data
        .align  2
handlers:        .space 128         # 32 entries * 4 bytes
handler_count:   .word  0

        .text
        .globl  atexit
atexit:
        lw      $t0, handler_count
        li      $t1, 32
        bge     $t0, $t1, atexit_full
        sll     $t2, $t0, 2         # offset = count * 4
        la      $t3, handlers
        addu    $t3, $t3, $t2
        sw      $a0, 0($t3)         # handlers[count] = fn
        addiu   $t0, $t0, 1
        sw      $t0, handler_count  # count++
        li      $v0, 0
        jr      $ra
atexit_full:
        li      $v0, -1
        jr      $ra

        .globl  exit
exit:
        # Save $ra and the $s* regs we'll use across the
        # handler calls.  $s0 = current index, $s1 = status,
        # $s2 = base of handlers table.
        addiu   $sp, $sp, -20
        sw      $ra,  0($sp)
        sw      $s0,  4($sp)
        sw      $s1,  8($sp)
        sw      $s2, 12($sp)
        move    $s1, $a0            # save status for later _Exit
        lw      $s0, handler_count
        addiu   $s0, $s0, -1        # i = count - 1
        la      $s2, handlers

exit_loop:
        bltz    $s0, exit_done
        sll     $t0, $s0, 2
        addu    $t0, $s2, $t0
        lw      $t1, 0($t0)         # fn = handlers[i]
        jalr    $t1                 # fn();  $ra clobbered, $a0 free
        addiu   $s0, $s0, -1
        j       exit_loop

exit_done:
        # Tail call to _Exit($s1).  Don't bother restoring the
        # $s* regs — we're not returning.  (For pedagogical
        # consistency the demo could restore them anyway, but
        # the production-style version skips the wasted work.)
        move    $a0, $s1
        j       _Exit
```

The `j _Exit` at the end is a tail call — saves one `jal`
overhead since exit never returns.  Worth a comment for
students reading both forms.

## Test/demo

`/examples/src/lib/libstdlib-demo/atexit-demo.{c,asm}`:

- Register 3 handlers (`h1`, `h2`, `h3`) that each print a
  distinct line ("handler 1 ran", etc.).
- Print "main about to exit".
- Call `exit(42)`.
- Output should be:
  ```
  main about to exit
  handler 3 ran
  handler 2 ran
  handler 1 ran
  ```
- Verify both stdout AND exit status 42 match between C and asm
  (same dual-check pattern as exit-demo).

The reverse order is the lesson — students often expect FIFO,
POSIX says LIFO.  Worth a comment in the demo source.

## What's NOT in scope

- `on_exit()` (Linux extension, takes a status arg).
- Cleanup on `_Exit` (POSIX: _Exit does NOT run atexit
  handlers — that's the whole point of the _Exit/exit split).
- Returning from main running atexit handlers.  In real C,
  `return 42;` from main is equivalent to `exit(42);`.  In this
  curriculum, `main` returns to `__start` which does its own
  syscall 17.  Wiring "main return → exit chain" would require
  changes to spim's exception handler (`exceptions.s`).
  Document the difference; don't try to bridge it.
- Thread-local exit handlers.  No threads in spim.
- Cleanup ordering with C++ destructors.  No C++.

## Where this sits in the curriculum

After `bsearch` (the `jalr` introduction) and before any
follow-up work on libstr.

## Status

Not started.  Estimated effort: ~half a day (C + asm + demo +
golden + meson wiring).  Smaller than atoi (no inter-library
dependencies; uses _Exit which is already in libstdlib).
