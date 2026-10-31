# MIPS assembly teaching style (stack-visible)

Durable style guide for the hand-written MIPS `.asm` teaching demos under
`examples/src/`. States how the demos are written and why. Read it before converting or
reviewing a demo. Companion to `tasks/reference/example-c-teaching-style.md` (the C side)
and an extension of the 4-layer comment system in
`tasks/archive/2026/05/23/PLAN-asm-comments.md`.

Established 2026-10-31 with the maintainer (William Emerison Six <billsix@gmail.com>) via
the pilot in `tasks/asm-simplification-sweep.md`. Worked examples (converted in the pilot):
`intro/increment-ints/increment-ints-1.asm`, `arguments/factorial/factorial.asm`,
`arguments/gcd/gcd.asm`, `transforms/wc/wc.asm`.

## The goal: values live on the stack, registers are working copies

A novice reading a demo should see each C variable **living in a named stack slot** and
"forget as much as possible about what C values are stored in which registers." A scratch
register holds a **working copy** of a value only while an instruction needs it; the result
is stored straight back to the variable's stack slot. The symbol table's `memory addr`
column answers "where does this C variable live"; the `register` column answers "which
temporary is holding a copy of it right now".

### This reverses the 2026-05-17 "register-only" decision, on purpose

`PLAN-asm-comments.md` records a 2026-05-17 decision to keep simple demos **register-only**
("Most of those frames were ceremony") and introduce a stack frame only where it taught
something. This style **overturns that** corpus-wide (maintainer's call, 2026-10-31): the
stack-visible view is now the default even for simple demos, because seeing values on the
stack is the lesson. The register-only idiom is still taught, but only in the exempt
deliberate-lesson files (below), as a deliberate counter-example.

## The stack frame convention

Each routine that has C locals, or makes a `jal`, builds a frame. `$fp` is a **fixed base**
for the whole call, so every local has a constant `-N($fp)` address. Layout, from the top
(highest address) down:

```
   -4($fp)   saved $ra     (ONLY if the routine makes a `jal`)
   -8($fp)   saved $fp     (caller's frame pointer)
  -12($fp)   first C local
  -16($fp)   second C local
   ...
   $sp  = $fp - FRAMESIZE   (bottom of the frame)
```

Prologue / epilogue (FRAMESIZE must be a multiple of 4; the saved-reg offsets are the
top of the just-allocated frame):

```
        addiu $sp, $sp, -FRAMESIZE
        sw $ra, FRAMESIZE-4($sp)     # -> -4($fp)   (omit if no `jal`)
        sw $fp, FRAMESIZE-8($sp)     # -> -8($fp)
        addiu $fp, $sp, FRAMESIZE    # $fp = top of our frame
        ...
        lw $ra, -4($fp)              # (omit if no `jal`)
        lw $fp, -8($fp)              # restore caller's $fp (last use of our $fp)
        addiu $sp, $sp, FRAMESIZE
        jr $ra
```

- **Save `$ra` on the stack only when the routine makes a `jal`.** Syscalls preserve `$ra`,
  so a demo that only makes syscalls (e.g. `increment-ints-1`, `wc`) saves `$fp` but not
  `$ra` and returns on the untouched `$ra`. This keeps the frame honest — no ceremony for a
  `$ra` that was never at risk.
- Saving `$ra` on the stack (rather than parking it in `$s0`, as the pre-sweep demos did) is
  itself part of the lesson: the return address is a value, and the student sees which slot
  holds it.
- A local whose **address is taken** in C must be a real stack slot so the address exists —
  e.g. `wc`'s `char c; os_read(fd, &c, 1)` reads into `-24($fp)` and passes that address with
  `addiu $a1, $fp, -24`.

## How few registers: persistent locals on the stack, transient temporaries in a register

"As few registers as possible" means: **every C local that persists across a statement or a
loop iteration gets a stack home.** A **transient sub-expression temporary that is produced
and consumed immediately** may stay in a scratch register — forcing it to memory would be
noise, and its value lands on the stack as soon as it is assigned to a real variable.

Worked example (`gcd`): `a` and `b` survive the whole loop → stack slots. The loop's
`int t = a % b;` is computed and immediately stored into `b`'s slot (`b = t`), so `t` stays
in `$t2`. The symbol table lists `t` with `—` in `memory addr` and a "transient temporary"
note. (This boundary is the maintainer's for the pilot to confirm; if full memory-machine
spilling is wanted later, `t` too would get a slot.)

**Shared leaf helpers stay register-based.** `atoi`/`print_uint` take their arg in `$a0`,
return in `$v0`, call nothing, and are repeated verbatim across demos by the self-contained
convention. They keep their register implementation and say so in their table header
(`routine: atoi — leaf helper, register-based`); the teaching focus is `main`.

## The SYMBOL TABLE (one per routine)

A fixed, aligned, six-column table per routine, placed in the routine's header comment
(this is the 4-layer system's L2, made columnar). Column order groups a variable's
**location** right after its name:

```
#  SYMBOL TABLE (routine: main)
#
#  C variable  | memory addr | register   | C type       | bytes | role / notes
#  ------------+-------------+------------+--------------+-------+---------------------------
#  argc        | —           | $a0        | int          |   4   | entry only; tested, then dead
#  argv        | —           | $a1        | char **      |   4   | entry only; argv[1] fetched
#  n           | -12($fp)    | $t0 (copy) | int          |   4   | loop counter; home on stack
#  result      | -16($fp)    | $t1 (copy) | unsigned int |   4   | running product
#  (saved $ra) | -4($fp)     | —          | code addr    |   4   | return into the runtime
#  (saved $fp) | -8($fp)     | —          | frame ptr    |   4   | caller's frame pointer
```

- **`C variable`** — the C name; machine bookkeeping that isn't a C variable is parenthesized
  (`(saved $ra)`, `(saved $fp)`, `(arg) str`).
- **`memory addr`** — the variable's stack home (`-N($fp)`) or `.data` label; `—` if it lives
  only in a register.
- **`register`** — the register holding it, or a working copy (`$t0 (copy)`); `—` if it lives
  only in memory. A value with BOTH columns filled is "on the stack, currently shadowed in a
  register" — that is the relationship the table is built to show.
- **`C type`** — the C type (`int`, `unsigned int`, `char **`, `char[16]`, …).
- **`bytes`** — width of the storage (pointers are 4 bytes under MIPS32; `char` is 1).
- **`role / notes`** — short; keep it terse.

When one scratch register is reused for several variables at different times, note it (see
`wc`: `fd`/`byte_count`/`line_count` each use `$t0` at different moments).

## The SNAPSHOT (ascii-art frame with traced values)

Below the symbol tables, an ascii-art diagram of the frame at a chosen point of **one
canonical input**, with **concrete traced values** in the slots and their register shadows:

```
#  SNAPSHOT (input: factorial 5, at factorial_loop pass 3, ...)
#
#          main's stack frame                 scratch registers
#     +--------------------------+           +----------------------+
#     | -12($fp) n       =   3   | <-------- | $t0 =  3  (copy of n)
#     | -16($fp) result  =  20   | <-------- | $t1 = 20  (copy of result)
#     +--------------------------+           +----------------------+
```

Keep snapshots to a **few key points** (loop top, after a `jal`), not every instruction, to
limit the maintenance surface. State the canonical input in the snapshot header, and narrate
how the run proceeds from the snapshot.

### Traced values are anchored to a real run by the checker

Hand-traced values can rot, and the stdout golden tests cannot catch a wrong value in a
comment. So every annotated demo carries a machine-readable `#CANONICAL` block naming one
invocation and its exact stdout, and `tools/check_asm_canonical.py` runs the demo under spim
and fails on a mismatch:

```
#CANONICAL (machine-checked by tools/check_asm_canonical.py)
#  args: 5
#  stdout:
#  | 120
```

`stdin: <path rel to examples/src>` replaces `args:` for stdin-driven demos. The checker
guarantees the canonical run's **final output** — the anchor the snapshot values are derived
from. The intermediate slot/register values in the diagram remain **human-traced from that
same canonical run** and human-reviewed; the checker does not single-step them. Run it with
`SPIM=builddir/spimulator python3 tools/check_asm_canonical.py <files>` (or `--all`). It is
not wired into the meson/CI gate (proposed, not auto-added).

## Comment-block width

SYMBOL TABLE and SNAPSHOT blocks are **tables and diagrams**, exempt from the 80-column
guideline (like `-fverbose-asm` output and the "never reflow tables/ASCII diagrams" carve-out
in the global style rules). **Instruction lines stay within 80 columns**; the inline `#`
mechanic comment on an instruction is trimmed, not the table.

## Exempt deliberate-lesson files (do NOT convert to this style)

These teach the register / idiomatic / bug lesson on purpose; converting them would destroy
the lesson. Leave them register-based and reference them here as the intentional
counter-examples:

- `extras/subrountines/subrountines-1.asm` (longhand stack) vs `-2.asm` (idiomatic register
  calling convention) — the register-vs-stack contrast IS the demo.
- `recursion/get-char-from-user/get-char-from-user-1.asm` vs `-2.asm` — a deliberate bug/fix
  pair.
- `algorithms/rpn/rpn.asm`, `rpn-1-stack.asm`, `rpn-2-ops.asm`, `rpn-3-float.asm` — the whole
  RPN ladder uses `$sp` AS the operand stack on purpose (push = `addiu $sp,-N` + store, pop =
  load + `addiu $sp,N`); Shape B is incompatible with that lesson. (Confirmed/expanded from the
  original rpn.asm + rpn-1-stack.asm pair during the 2026-10-31 algorithms rollout; the
  `rpn-eval`/`rpn-eval-test` siblings instead use a `.data` operand array and ARE converted.)
- `intro/exit/exit.asm` — the `set $v0 before jr $ra` exit lesson.

(Confirm the full exempt set as the rollout proceeds; these are the known ones.)

## Verifying a conversion (the rollout method)

The sweep CHANGES INSTRUCTIONS, so behavior must be preserved. Most demos have **no golden
test** (only ~a third do), so the primary gate is **before/after output equivalence**:

1. Before editing, capture the demo's stdout under spim for a range of inputs (including edge
   and error cases) — e.g. `spim -f demo.asm <args>` and piped stdin.
2. Convert.
3. Re-run the same inputs and `diff` against the captured baseline — it must be **identical**.

Plus, where they exist, the `run-demo.sh` goldens (`meson test --suite examples`), and the
`#CANONICAL` checker on every annotated file. The containerized `make image` gate is the
project's "done" bar.
