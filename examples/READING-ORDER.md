# Reading order

The recommended order to read the demos for someone learning
MIPS assembly who already knows the algorithms from a
high-level language (Java, Python, etc).

**Directory numbering matches this order.**  The 27 demos in
Parts 1-6 below correspond directly to `src/01-…` through
`src/27-…`.  Entries 28-31 exist on disk but are not part of
the main reading order (see "Extras" at the bottom).

See [`PLAN-curriculum-order.md`](PLAN-curriculum-order.md) for
the rationale.

## How to use this list

Each entry is a `<demo-name>` directory under `src/`.  Inside
each you'll find a `<demo-name>.c` (the C version, closer to a
Java program with pointers) and a `<demo-name>.asm` (the MIPS
port).  Read the C first, then the asm — the asm's header
comment block references the C source line by line.

A few demos that show a bug-and-fix pair (or a longhand /
idiomatic pair) have two asm variants per single C source:
`<demo-name>-1.asm` and `<demo-name>-2.asm`.

To run a demo on the asm side:

```sh
spimulator -f src/<demo-name>/<demo-name>.asm   # plus any argv it takes
```

To run the C side natively (Linux):

```sh
cd src && meson compile -C builddir
./builddir/<demo-name>                          # plus any argv
```

## Real-Unix argv convention

The filter demos (`wc`, `head`, `rev`, `expand`, `cat`,
`cksum`) follow the standard Unix argv shape:

```sh
demo                    # reads stdin
demo -                  # reads stdin (explicit dash)
demo FILE               # opens FILE and reads it
```

`head` also accepts `-n N` before any filename.  The pure
stdin-filter demos `13-tr` and `14-rot13` take no file
argument — real `tr` doesn't either.

---

## Part 1 — First contact (5 demos)

The minimum syntax to read any of the rest.

1. **`01-helloworld`** — the simplest possible program; one syscall.
2. **`02-print1through10`** — `li`, `addi`, a branch loop.
3. **`03-increment-ints`** — multiple `$t`-regs, integer ops.
4. **`04-clear`** — ANSI escape bytes; immediate visual reward.
5. **`05-yes`** — the tightest possible loop.

## Part 2 — Algorithms you already know (4 demos, no argv yet)

Familiar algorithms in asm.  Each one introduces a new MIPS
concept on territory you walk in confident about.

6. **`06-fizzbuzz [N]`** — modulo (`div`/`mfhi`), multi-way
   branching; N from argv (default 100).
7. **`07-bubble-sort`** — reads ints from stdin (syscall 5 +
   `$a3` EOF flag); nested loops, in-place swap.
8. **`08-pascals-triangle [N]`** — in-place right-to-left row
   update; N from argv (default 10).
9. **`09-sieve [N]`** — byte-granular `lb`/`sb`; **sbrk
   (syscall 9)** for the dynamic flag array.

## Part 3 — Unix filters (6 demos)

Recognisable command-line tools.  Each demo accepts either
stdin (bare or `-`) or a filename, except the pure filter
forms (13/14) which take no file arg.

10. **`10-wc`** — byte + line counters; stdin or file.
11. **`11-head`** — `-n N` flag + stdin or file (subsumes the
    old `head-file` variant).
12. **`12-rev`** — line buffer + reverse walk; stdin or file.
13. **`13-tr`** *(sidebar)* — uppercase byte transform; pure
    stdin filter (matches real `tr`).
14. **`14-rot13`** — byte transform with modular wraparound;
    pure stdin filter; self-inverse.
15. **`15-expand`** — tab-expansion column counter; stdin or file.

## Part 4 — Files (3 demos)

16. **`16-cat`** — block I/O via syscall 14/15; stdin or file
    (subsumes the old `cat-file` variant).
17. **`17-nologin`** — first `open`/`close`; hardcoded path
    (`/etc/nologin.txt`) — the demo's lesson is the
    open/close pattern, not argv parsing.
18. **`18-cksum`** — `$s*` save discipline, 256-entry lookup
    table, bitwise ops, private `print_uint` subroutine;
    stdin or file (prints filename column in file mode, like
    real `cksum`).

## Part 5 — argv: taking inputs from the command line (5 demos)

19. **`19-echo`** — argv walk; no atoi.  Just establishes the
    crt0.h shim and `$a0=argc`, `$a1=argv` at entry.
20. **`20-factorial`** — argv + `atoi` as a private subroutine.
21. **`21-gcd`** — two atois back-to-back; the "park argv in
    `$s2`" trick.
22. **`22-binary-search TARGET`** — linear + binary search
    variants; target from argv, sorted ints from stdin.
23. **`23-tee`** — variable argc, fd array in `.data`, per-block
    fan-out write.

## Part 6 — Stack frames and recursion (4 demos)

The deepest material.  State that can't live in `$s*` because
each recursive invocation would overwrite the same register.

24. **`24-get-char-from-user`** — read both variants:
    - **`-1.asm`** is intentionally misaligned (the bug);
    - **`-2.asm`** is the fix (word-aligned frame).
    Both share the same C source.
25. **`25-fibonacci`** — first per-call stack frame; iter vs
    rec in one demo.
26. **`26-hanoi`** — per-call frame with FOUR saved args.
27. **`27-queens [N]`** — N-queens via backtracking; `col`
    loop counter is stack-resident because each recursive
    call has its own.  Default N=8 (92 solutions); cap N=12.

---

## Extras (not in the main reading order)

These exist on disk but the curriculum doesn't include them
in the main path.  See `PLAN-curriculum-order.md` for the
reasoning.

- **`40-print-out-ascii`** — signed -128..127 walk.  Useful
  for sign-extension intuition; not on the main path because
  the *task* isn't pedagogically compelling on its own.
- **`41-commaAndPeriodCounter`** — redundant with `10-wc`
  (same multi-counter byte loop shape, less recognisable
  framing).
- **`42-subrountines`** — longhand and idiomatic stack-based
  calling convention around `mxPlusB`.  Subroutine linkage
  is taught instead via algorithms the student already cares
  about (atoi in 20, str_eq in 11, print_uint in 18).
- **`43-testStringsForEquality`** — `str1`/`str2`/`str3`
  comparison with no recognisable hook.  The `str_eq`
  concept reappears in `11-head` with a real purpose
  (parsing the `-n` flag).

## Quick concept index

Where each MIPS idea first lands in this order:

| MIPS concept | First demo |
|---|---|
| `.asciiz`, syscall 4 | 01 |
| `li`, `addi`, branches | 02 |
| ANSI escape bytes | 04-clear |
| modulo via `div`/`mfhi` | 06-fizzbuzz |
| nested loops + array swap | 07-bubble-sort |
| right-to-left in-place update | 08-pascals-triangle |
| sbrk (syscall 9) for dynamic memory | 09-sieve |
| `lb`/`sb` byte-granularity access | 09-sieve |
| read_int (syscall 5) + `$a3` EOF flag | 07-bubble-sort |
| argv-with-dash convention | 10-wc |
| stdin byte loop on chosen fd | 10-wc |
| `-n N` flag parsing | 11-head |
| byte-conditional transform | 13-tr / 14-rot13 |
| stream-state counter | 15-expand |
| block I/O (syscall 14/15) | 16-cat |
| open/close (syscall 13/16) | 17-nologin |
| `$s*` callee-save discipline | 18-cksum |
| 256-entry lookup table + bitwise ops | 18-cksum |
| `crt0.h` shim for `my_main(argc, argv)` | 19-echo |
| `atoi` + private subroutine | 20-factorial |
| cross-call `$s*` becomes load-bearing | 21-gcd |
| `.word` arrays + strided index | 22-binary-search |
| variable argc + fd array + fan-out | 23-tee |
| stack frame + word alignment | 24-get-char-from-user |
| per-call stack frame for recursion | 25-fibonacci |
| per-call frame with multiple args | 26-hanoi |
| backtracking | 27-queens |
