# Reading order

The recommended order to read the demos for someone learning
MIPS assembly who already knows the algorithms from a
high-level language (Java, Python, etc).

**Filesystem layout is by topic, not by reading order.**  Demos
live in seven topic folders under `src/`:

| Folder | Contents |
|---|---|
| `intro/` | smallest possible programs (Part 0–1 below) |
| `algorithms/` | familiar algorithms in asm (Part 2 + binary-search) |
| `transforms/` | stdin → stdout transforms (Parts 3 + Unix toolchest filters) |
| `fileio/` | open/close/read/write (Part 4 + Unix toolchest file demos) |
| `arguments/` | argv handling, atoi (Part 5 + Unix toolchest argv demos) |
| `recursion/` | per-call stack frames (Part 6) |
| `extras/` | off-curriculum demos |

The Parts below give the engagement-first reading sequence
across those topics.  See
[`PLAN-curriculum-order.md`](PLAN-curriculum-order.md) for the
rationale.

## How to use this list

Each entry is a `<demo-name>` directory under one of the topic
folders.  Inside each you'll find a `<demo-name>.c` (the C
version, closer to a Java program with pointers) and a
`<demo-name>.asm` (the MIPS port).  Read the C first, then the
asm — the asm's header comment block references the C source
line by line.

A few demos that show a bug-and-fix pair (or a longhand /
idiomatic pair) have two asm variants per single C source:
`<demo-name>-1.asm` and `<demo-name>-2.asm`.

To run a demo on the asm side:

```sh
spimulator -f src/<topic>/<demo-name>/<demo-name>.asm   # plus any argv it takes
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
stdin-filter demos `tr` and `rot13` take no file
argument — real `tr` doesn't either.

---

## Part 0 — The smallest possible program (1 demo)

0. **`exit`** — set a status code, exit, observe it via
   `echo $?`.  The whole syscall mechanism on its smallest
   working example: `$v0` holds the syscall number, `$a0..$a3`
   hold arguments, `syscall` transfers control to the kernel.
   Reads as a direct port of Programming from the Ground Up's
   first program, retargeted to MIPS + spim.  Also serves as
   the canonical reference for "why every other demo sets
   `$v0` before its final `jr $ra`" — see the demo's header
   comment block.

## Part 1 — First contact (5 demos)

The minimum syntax to read any of the rest.

1. **`helloworld`** — the simplest possible program that
   produces output; one print + one exit.
2. **`print1through10`** — `li`, `addi`, a branch loop.
3. **`increment-ints`** — multiple `$t`-regs, integer ops.
4. **`clear`** — ANSI escape bytes; immediate visual reward.
5. **`yes`** — the tightest possible loop.

## Part 2 — Algorithms you already know (4 demos, no argv yet)

Familiar algorithms in asm.  Each one introduces a new MIPS
concept on territory you walk in confident about.

6. **`fizzbuzz [N]`** — modulo (`div`/`mfhi`), multi-way
   branching; N from argv (default 100).
7. **`bubble-sort`** — reads ints from stdin (syscall 5 +
   `$a3` EOF flag); nested loops, in-place swap.
8. **`pascals-triangle [N]`** — in-place right-to-left row
   update; N from argv (default 10).
9. **`sieve [N]`** — byte-granular `lb`/`sb`; **sbrk
   (syscall 9)** for the dynamic flag array.

## Part 3 — Unix filters (6 demos)

Recognisable command-line tools.  Each demo accepts either
stdin (bare or `-`) or a filename, except the pure filter
forms (13/14) which take no file arg.

10. **`wc`** — byte + line counters; stdin or file.
11. **`head`** — `-n N` flag + stdin or file (subsumes the
    old `head-file` variant).
12. **`rev`** — line buffer + reverse walk; stdin or file.
13. **`tr`** *(sidebar)* — uppercase byte transform; pure
    stdin filter (matches real `tr`).
14. **`rot13`** — byte transform with modular wraparound;
    pure stdin filter; self-inverse.
15. **`expand`** — tab-expansion column counter; stdin or file.

## Part 4 — Files (3 demos)

16. **`cat`** — block I/O via syscall 14/15; stdin or file
    (subsumes the old `cat-file` variant).
17. **`nologin`** — first `open`/`close`; hardcoded path
    (`/etc/nologin.txt`) — the demo's lesson is the
    open/close pattern, not argv parsing.
18. **`cksum`** — `$s*` save discipline, 256-entry lookup
    table, bitwise ops, private `print_uint` subroutine;
    stdin or file (prints filename column in file mode, like
    real `cksum`).

## Part 5 — argv: taking inputs from the command line (5 demos)

19. **`echo`** — argv walk; no atoi.  Just establishes the
    crt0.h shim and `$a0=argc`, `$a1=argv` at entry.
20. **`factorial`** — argv + `atoi` as a private subroutine.
21. **`gcd`** — two atois back-to-back; the "park argv in
    `$s2`" trick.
22. **`binary-search TARGET`** — linear + binary search
    variants; target from argv, sorted ints from stdin.
23. **`tee`** — variable argc, fd array in `.data`, per-block
    fan-out write.

## Part 6 — Stack frames and recursion (4 demos)

The deepest material.  State that can't live in `$s*` because
each recursive invocation would overwrite the same register.

24. **`get-char-from-user`** — read both variants:
    - **`-1.asm`** is intentionally misaligned (the bug);
    - **`-2.asm`** is the fix (word-aligned frame).
    Both share the same C source.
25. **`fibonacci`** — first per-call stack frame; iter vs
    rec in one demo.
26. **`hanoi`** — per-call frame with FOUR saved args.
27. **`queens [N]`** — N-queens via backtracking; `col`
    loop counter is stack-resident because each recursive
    call has its own.  Default N=8 (92 solutions); cap N=12.

## Part 7 — Unix toolchest (12 demos)

A wider practice set, inspired by sbase/ubase.  Read any of
these after Part 6 in any order — none introduces a brand-new
register-class concept, but each takes a real Unix tool's job
and forces you to think about a specific asm pattern.

28. **`seq M N`** — two atois, signed-aware ascending or
    descending walk.  Print-uint vs. print-int distinction.
29. **`touch FILE`** — `os_open` with `O_CREAT|O_WRONLY`;
    the demo whose body is essentially just the syscall.
30. **`factor N`** — trial division up to √N (well, up to
    `i*i <= n`); print_uint sequence with space-separated
    output.
31. **`cp SRC DST`** — two fds open simultaneously
    (read + write); block-loop copy via syscall 14/15.
32. **`uniq`** — adjacent-duplicate filter; needs a
    persistent "previous line" buffer in `.data`.
33. **`nl`** — per-line counter; print_uint + tab + line.
34. **`cut -c N-M`** — column extractor; `-c` flag parse
    + the substring offsets into each line.
35. **`od -c`** — 16-byte rows with leading 7-digit octal
    offset; per-byte printable/escape/octal dispatch.
36. **`tac`** — **incremental sbrk** for an unbounded
    input buffer; reverse-walk on the saved bytes.  The
    curriculum's first taste of "grow the heap as you go".
37. **`tail -n N`** — ring buffer of the last N lines;
    end-relative output.
38. **`comm A B`** — two files open for reading
    simultaneously; line-by-line merge.
39. **`base64`** — bit-twiddling across three-byte input
    triples to produce four 6-bit indices; column wrap.

---

## Extras (not in the main reading order)

These exist on disk but the curriculum doesn't include them
in the main path.  See `PLAN-curriculum-order.md` for the
reasoning.

- **`print-out-ascii`** — signed -128..127 walk.  Useful
  for sign-extension intuition; not on the main path because
  the *task* isn't pedagogically compelling on its own.
- **`commaAndPeriodCounter`** — redundant with `wc`
  (same multi-counter byte loop shape, less recognisable
  framing).
- **`subrountines`** — longhand and idiomatic stack-based
  calling convention around `mxPlusB`.  Subroutine linkage
  is taught instead via algorithms the student already cares
  about (atoi in 20, str_eq in 11, print_uint in 18).
- **`testStringsForEquality`** — `str1`/`str2`/`str3`
  comparison with no recognisable hook.  The `str_eq`
  concept reappears in `head` with a real purpose
  (parsing the `-n` flag).

## Quick concept index

Where each MIPS idea first lands in this order:

| MIPS concept | First demo |
|---|---|
| `syscall` mechanism + `$v0`/`$a0..$a3` convention | exit |
| exit status + `$?` | exit |
| `.asciiz`, syscall 4 | helloworld |
| `li`, `addi`, branches | print1through10 |
| ANSI escape bytes | clear |
| modulo via `div`/`mfhi` | fizzbuzz |
| nested loops + array swap | bubble-sort |
| right-to-left in-place update | pascals-triangle |
| sbrk (syscall 9) for dynamic memory | sieve |
| `lb`/`sb` byte-granularity access | sieve |
| read_int (syscall 5) + `$a3` EOF flag | bubble-sort |
| argv-with-dash convention | wc |
| stdin byte loop on chosen fd | wc |
| `-n N` flag parsing | head |
| byte-conditional transform | tr / rot13 |
| stream-state counter | expand |
| block I/O (syscall 14/15) | cat |
| open/close (syscall 13/16) | nologin |
| `$s*` callee-save discipline | cksum |
| 256-entry lookup table + bitwise ops | cksum |
| `crt0.h` shim for `my_main(argc, argv)` | echo |
| `atoi` + private subroutine | factorial |
| cross-call `$s*` becomes load-bearing | gcd |
| `.word` arrays + strided index | binary-search |
| variable argc + fd array + fan-out | tee |
| stack frame + word alignment | get-char-from-user |
| per-call stack frame for recursion | fibonacci |
| per-call frame with multiple args | hanoi |
| backtracking | queens |
| signed print_int vs unsigned print_uint | seq |
| open with O_CREAT | touch |
| two fds open simultaneously | cp |
| persistent "previous" buffer in `.data` | uniq |
| flag parsing + ranged extract | cut |
| row-oriented output + per-byte dispatch | od |
| incremental sbrk for unbounded buffer | tac |
| ring buffer over a stream | tail |
| bit-pack across input bytes | base64 |
