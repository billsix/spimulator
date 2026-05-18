# Reading order

The recommended order to read the demos for someone learning
MIPS assembly who already knows the algorithms from a
high-level language (Java, Python, etc).

**Directory numbering doesn't match this order — that's
intentional.**  See [`PLAN-curriculum-order.md`](PLAN-curriculum-order.md)
for the rationale.

## How to use this list

Each entry is a `<demo-name>` directory under `src/`.  Inside
each you'll find a `*-1.c` (the C version, closer to a Java
program with pointers) and a `*.asm` (the MIPS port).  Read
the C first, then the asm — the asm's header comment block
will reference the C source line by line.

To run a demo on the asm side:

```sh
spimulator -f src/<demo-name>/<demo-name>.asm   # plus any argv it takes
```

To run the C side natively (Linux):

```sh
cd src && meson compile -C builddir
./builddir/<demo-name>-1                         # plus any argv
```

---

## Part 1 — First contact (5 demos)

The minimum syntax to read any of the rest.

1. **`01-helloworld`** — the simplest possible program; one syscall.
2. **`02-print1through10`** — `li`, `addi`, a branch loop.
3. **`03-increment-ints`** — multiple `$t`-regs, integer ops.
4. **`09-clear`** — ANSI escape bytes; immediate visual reward.
5. **`10-yes`** — the tightest possible loop.

> Sidebar after this Part: `05-print-out-ascii` — a signed -128..127
> walk that's useful for sign-extension intuition.

## Part 2 — Algorithms you already know (4 demos, no argv yet)

Familiar algorithms in asm.  Each one introduces a new MIPS
concept on territory you walk in confident about.

6. **`25-fizzbuzz`** — modulo (`div`/`mfhi`), multi-way branching.
7. **`28-bubble-sort`** — nested loops, `.word` array, in-place swap.
8. **`31-pascals-triangle`** — in-place right-to-left row update.
9. **`32-sieve`** — `.space` byte array, byte-granular `lb`/`sb`.

## Part 3 — Unix filters: stdin byte loops (6 demos)

Recognisable command-line tools.  Simple stdin → stdout
transforms; spim uses `~` as a stand-in for EOF.

10. **`12-wc`** — multi-counter byte loop.
11. **`13-head`** — early termination.
12. **`14-rev`** — line buffer + reverse walk.
13. **`16-tr`** *(sidebar)* — uppercase byte transform; the
    simpler warmup for the next demo.
14. **`29-rot13`** — byte transform with modular wraparound;
    self-inverse.
15. **`17-expand`** — stream-state counter for tab expansion.

## Part 4 — Files (3 demos)

File descriptors and block I/O.

16. **`11-cat`** — block I/O via syscall 14/15.
17. **`15-nologin`** — first `open`/`close`.
18. **`18-cksum`** — `$s*` save discipline, 256-entry lookup
    table, bitwise ops, private `print_uint` subroutine.  The
    heaviest demo before argv shows up.

## Part 5 — argv: taking inputs from the command line (7 demos)

The point where programs start taking student-supplied
inputs.  Introduces the `crt0.h` shim, `parse_int`, and the
"`$s*` held across `jal`" discipline becoming load-bearing.

19. **`19-echo`** — argv walk; no atoi.  Just establishes the
    crt0.h shim and `$a0=argc`, `$a1=argv` at entry.
20. **`20-factorial`** — argv + `atoi` as a private subroutine.
21. **`22-gcd`** — two atois back-to-back; the "park argv in
    `$s2`" trick.
22. **`27-binary-search`** — `.word` array, linear + binary
    search variants; target from argv.
23. **`21-cat-file`** — argv + open.
24. **`23-head-file`** — `str_eq` for the `-n` flag, `atoi` for
    `N`, `open` for the filename.
25. **`24-tee`** — variable argc, fd array in `.data`, per-block
    fan-out write.

## Part 6 — Stack frames and recursion (5 demos)

The deepest material.  State that can't live in `$s*` because
each recursive invocation would overwrite the same register.

26. **`04-get-char-from-user-1`** — intentional misaligned frame.
    The bug.
27. **`04-get-char-from-user-2`** — the fix.  Word-aligned frame.
28. **`26-fibonacci`** — first per-call stack frame; iter vs
    rec in one demo.
29. **`30-hanoi`** — per-call frame with FOUR saved args.
30. **`33-queens`** — backtracking; `col` loop counter is
    stack-resident because each recursive call has its own.

---

## Demos that exist but aren't in the main reading order

These live on disk for completeness but the curriculum
doesn't include them in the main path.  See
`PLAN-curriculum-order.md` for the reasoning.

- **`06-commaAndPeriodCounter`** — redundant with `12-wc`
  (same multi-counter byte loop shape).
- **`07-subrountines-1`** — longhand stack-based calling
  convention around `mxPlusB`.  Subroutine linkage is taught
  instead via algorithms the student already cares about
  (atoi in 20, str_eq in 23, print_uint in 18).
- **`07-subrountines-2`** — idiomatic version of the same
  abstract `mxPlusB` demo.
- **`08-testStringsForEquality`** — `str1`/`str2`/`str3`
  comparison with no recognisable hook.  The `str_eq`
  concept reappears in 23-head-file with a real purpose.

## Quick concept index

Where each MIPS idea first lands in this order:

| MIPS concept | First demo |
|---|---|
| `.asciiz`, syscall 4 | 01 |
| `li`, `addi`, branches | 02 |
| ANSI escape bytes | 09 |
| modulo via `div`/`mfhi` | 25-fizzbuzz |
| nested loops + array swap | 28-bubble-sort |
| right-to-left in-place update | 31-pascals-triangle |
| `.space` working memory + `lb`/`sb` | 32-sieve |
| stdin byte loop | 12-wc |
| byte-conditional transform | 16-tr / 29-rot13 |
| stream-state counter | 17-expand |
| block I/O (syscall 14/15) | 11-cat |
| open/close (syscall 13/16) | 15-nologin |
| `$s*` callee-save discipline | 18-cksum |
| 256-entry lookup table + bitwise ops | 18-cksum |
| argv (crt0.h shim) | 19-echo |
| `atoi` + private subroutine | 20-factorial |
| cross-call `$s*` becomes load-bearing | 22-gcd |
| `.word` arrays + strided index | 27-binary-search |
| argv + file open | 21-cat-file |
| str_eq + flag parsing | 23-head-file |
| variable argc + fd array + fan-out | 24-tee |
| stack frame + word alignment | 04 (pair) |
| per-call stack frame for recursion | 26-fibonacci |
| per-call frame with multiple args | 30-hanoi |
| backtracking | 33-queens |
