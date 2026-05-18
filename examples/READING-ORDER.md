# Reading order

The recommended order to read the demos for someone learning
MIPS assembly who already knows the algorithms from a
high-level language (Java, Python, etc).

**Directory numbering now matches this order.**  The 29 demos
in Parts 1-6 below correspond directly to `src/01-…` through
`src/29-…`.  Entries 30-33 exist on disk but are not part of
the main reading order (see "Extras" at the bottom).

See [`PLAN-curriculum-order.md`](PLAN-curriculum-order.md) for
the rationale behind this order.

## How to use this list

Each entry is a `<demo-name>` directory under `src/`.  Inside
each you'll find a `<demo-name>.c` (the C version, closer to a
Java program with pointers) and a `<demo-name>.asm` (the MIPS
port).  Read the C first, then the asm — the asm's header
comment block references the C source line by line.

A few demos that show a bug-and-fix pair (or a longhand /
idiomatic pair) have two asm variants per single C source:
`<demo-name>-1.asm` and `<demo-name>-2.asm`.  That convention
is preserved.

To run a demo on the asm side:

```sh
spimulator -f src/<demo-name>/<demo-name>.asm   # plus any argv it takes
```

To run the C side natively (Linux):

```sh
cd src && meson compile -C builddir
./builddir/<demo-name>                          # plus any argv
```

## Stdin-or-file gap (worth knowing up front)

The Unix-filter demos are **one or the other**, not both.
The stdin-only demos (10-wc, 11-head, 12-rev, 13-tr,
14-rot13, 15-expand, 16-cat) read only stdin; the file-via-
argv demos (23-cat-file, 24-head-file) read only their
named file.  Real Unix tools combine both behaviors in one
program; we don't yet.  See
[`PLAN-stdin-or-file.md`](PLAN-stdin-or-file.md) for the
planned cleanup.

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

6. **`06-fizzbuzz`** — modulo (`div`/`mfhi`), multi-way branching.
7. **`07-bubble-sort`** — nested loops, `.word` array, in-place swap.
8. **`08-pascals-triangle`** — in-place right-to-left row update.
9. **`09-sieve`** — `.space` byte array, byte-granular `lb`/`sb`.

## Part 3 — Unix filters: stdin byte loops (6 demos)

Recognisable command-line tools.  Simple stdin → stdout
transforms; spim uses `~` as a stand-in for EOF.

10. **`10-wc`** — multi-counter byte loop.
11. **`11-head`** — early termination.
12. **`12-rev`** — line buffer + reverse walk.
13. **`13-tr`** *(sidebar)* — uppercase byte transform; the
    simpler warmup for the next demo.
14. **`14-rot13`** — byte transform with modular wraparound;
    self-inverse.
15. **`15-expand`** — stream-state counter for tab expansion.

## Part 4 — Files (3 demos)

File descriptors and block I/O.

16. **`16-cat`** — block I/O via syscall 14/15.
17. **`17-nologin`** — first `open`/`close`.
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
21. **`21-gcd`** — two atois back-to-back; the "park argv in
    `$s2`" trick.
22. **`22-binary-search`** — `.word` array, linear + binary
    search variants; target from argv.
23. **`23-cat-file`** — argv + open.
24. **`24-head-file`** — `str_eq` for the `-n` flag, `atoi` for
    `N`, `open` for the filename.
25. **`25-tee`** — variable argc, fd array in `.data`, per-block
    fan-out write.

## Part 6 — Stack frames and recursion (4 demos, with one having two variants)

The deepest material.  State that can't live in `$s*` because
each recursive invocation would overwrite the same register.

26. **`26-get-char-from-user`** — read both variants:
    - **`-1.asm`** is intentionally misaligned (the bug);
    - **`-2.asm`** is the fix (word-aligned frame).
    Both share the same C source.
27. **`27-fibonacci`** — first per-call stack frame; iter vs
    rec in one demo.
28. **`28-hanoi`** — per-call frame with FOUR saved args.
29. **`29-queens`** — backtracking; `col` loop counter is
    stack-resident because each recursive call has its own.

---

## Extras (not in the main reading order)

These exist on disk but the curriculum doesn't include them
in the main path.  See `PLAN-curriculum-order.md` for the
reasoning.

- **`30-print-out-ascii`** — signed -128..127 walk.  Useful
  for sign-extension intuition; not on the main path because
  the *task* isn't pedagogically compelling on its own.
- **`31-commaAndPeriodCounter`** — redundant with `10-wc`
  (same multi-counter byte loop shape, less recognisable
  framing).
- **`32-subrountines`** — longhand and idiomatic
  stack-based calling convention around `mxPlusB`.
  Subroutine linkage is taught instead via algorithms the
  student already cares about (atoi in 20, str_eq in 24,
  print_uint in 18).  Both variants (`-1.asm`, `-2.asm`)
  share one `.c`.
- **`33-testStringsForEquality`** — `str1`/`str2`/`str3`
  comparison with no recognisable hook.  The `str_eq`
  concept reappears in `24-head-file` with a real purpose
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
| `.space` working memory + `lb`/`sb` | 09-sieve |
| stdin byte loop | 10-wc |
| byte-conditional transform | 13-tr / 14-rot13 |
| stream-state counter | 15-expand |
| block I/O (syscall 14/15) | 16-cat |
| open/close (syscall 13/16) | 17-nologin |
| `$s*` callee-save discipline | 18-cksum |
| 256-entry lookup table + bitwise ops | 18-cksum |
| argv (crt0.h shim) | 19-echo |
| `atoi` + private subroutine | 20-factorial |
| cross-call `$s*` becomes load-bearing | 21-gcd |
| `.word` arrays + strided index | 22-binary-search |
| argv + file open | 23-cat-file |
| str_eq + flag parsing | 24-head-file |
| variable argc + fd array + fan-out | 25-tee |
| stack frame + word alignment | 26-get-char-from-user |
| per-call stack frame for recursion | 27-fibonacci |
| per-call frame with multiple args | 28-hanoi |
| backtracking | 29-queens |
