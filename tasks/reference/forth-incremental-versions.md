# Forth incremental versions — the per-rung build spec

**Reference document** — the blueprint and design record of the nine-rung Forth ladder in
`examples/src/lang/forth/`, a port of the design of **jonesforth** to MIPS and spim. Read with
`tasks/reference/incremental-example-decomposition.md` (the method: one new idea per rung) and
`tasks/reference/calc-incremental-versions.md` (the first ladder, whose shape this doc follows). The
work record is `tasks/archive/2026/11/21/forth-incremental-versions.md`. Written 2026-11-21
(William Emerison Six <billsix@gmail.com>).

**Source ported:** jonesforth version 47 (`jonesforth.S,v 1.47 2009-09-11`, `jonesforth.f,v 1.18`)
by Richard W.M. Jones, public domain, home page http://annexia.org/forth, read from the mirror
https://github.com/nornagon/jonesforth. Every rung's header credits it. Rungs 8 and 9 feed
`jonesforth.f` itself, verbatim, on stdin.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-21)

- **Assembly only.** Each rung is one `.asm` + `.expected` (+ `.input` from rung 4); there is no C
  twin. The examples harness has an asm-only path for it (below).
- **Truth values are jonesforth's 1/0,** not ANS Forth's −1/0. `0=`, `<` and friends return 1 for
  true. A program that relies on `TRUE INVERT` being 0 (ANS) behaves differently.
- **Nine rungs;** `CATCH`/`THROW`, `FORGET`, `DUMP`, execution-token words (`:NONAME`, `[']`), C
  strings, files and the inline assembler are out of scope.
- **Exempt from the stack-visible asm style** (`tasks/reference/mips-asm-teaching-style.md`, exempt
  list): the machine state lives in fixed registers, and each file's header has a REGISTER MAP.
- **Reading order:** right after `calc-tree`, as sub-entries 43a–43i of `examples/READING-ORDER.md`.

## The machine (shared by every rung)

| register | Forth name | role |
|---|---|---|
| `$s7` | IP | the next cell of the thread |
| `$s6` | RSP | return stack, over `return_stack` (1 KiB) in `.data`, growing down |
| `$sp` | DSP | data stack, top at `0($sp)`, growing down (spim's own stack) |
| `$t0` | W | the codeword address NEXT just fetched; DOCOL reads the body at W + 4 |
| `$t1`–`$t9` | — | scratch inside one primitive |
| `$a0`/`$a1`, `$v0`/`$v1` | — | arguments and results of the `jal` helpers |

- **Indirect threading,** as jonesforth. A thread is a list of codeword addresses; a codeword cell
  holds the address of machine code (`code_X` for a primitive, `docol` for a word written in Forth).
  NEXT is `lw $t0,0($s7)` / `addiu $s7,$s7,4` / `lw $t1,0($t0)` / `jr $t1`. spim has no `.macro`,
  so NEXT is written once at `next:` and every primitive ends `j next`.
- **Headers** as jonesforth: a 4-byte link, a length/flags byte (`F_IMMED` 0x80, `F_HIDDEN` 0x20,
  `F_LENMASK` 0x1f), the name, padding to a cell, the codeword. Built-in headers are written out in
  `.data`, oldest first; user words are built at HERE in 64 KiB taken with `sbrk` (syscall 9) at
  start-up, so they lie above the built-ins. `CFA>` and `SEE` in jonesforth.f depend on that order.
- **Helpers** (`_KEY`, `_WORD`, `_FIND`, `_TCFA`, `_NUMBER`, `_COMMA`) are `jal` subroutines; the
  one non-leaf, `_WORD`, keeps its `$ra` on the data stack while it runs, as an i386 `call` does.
- **I/O:** `KEY` is spim's read_char (syscall 12), `EMIT` print_char (11). End of input exits with
  status 0 from inside `_KEY` (jonesforth does the same), so every stdin-driven rung ends that way.
  `BYE` is exit (10).
- **Differences from jonesforth,** each deliberate: `PARSE ERROR: <word>` goes to stdout (spim's
  print syscalls write stdout) and shows the word, not 40 bytes of the input buffer (KEY reads one
  byte at a time, so there is no buffer); `.` and `CR` are asm primitives in rungs 1–7 (jonesforth
  defines them in jonesforth.f, as rungs 8–9 do); `BYE` is a primitive (jonesforth.f builds it from
  `SYSCALL0`); there are no `SYSCALL*`, `C@C!`, `VERSION`, `O_*`/`SYS_*` constants.
- **Inherited limits:** WORD's 32-byte buffer is not bounds-checked; there are no stack under/overflow
  checks; a parse error inside a definition leaves STATE at 1 and the word hidden; WORD's buffer is
  reused by the interpreter's own WORD, so `WORD x TELL` typed at the top level prints `TELL`'s
  name (rung 7 wraps it in a definition).

## The rungs

### 1 — `forth-1-next` (threaded code, NEXT)
Primitives `LIT + . CR BYE`, a hand-written thread `2 3 + . CR 10 20 12 + + . CR BYE`. No input.
`.expected`: `5 ` / `42 `. Diff from nothing: NEXT, the data stack, five primitives.

### 2 — `forth-2-docol` (words written in Forth)
`DOCOL`, `EXIT`, the return stack, `DUP`, `*`. Hand-threaded `SQUARE` and `CUBE` (CUBE calls SQUARE).
`.expected`: `49 ` / `27 ` / `81 `. Diff: DOCOL/EXIT and `$s6`.

### 3 — `forth-3-primitives` (the primitive set, branches)
jonesforth's machine-code set: stack (`DROP SWAP OVER ROT -ROT 2DROP 2DUP 2SWAP ?DUP`), arithmetic
(`1+ 1- 4+ 4- - /MOD`), comparisons (`= <> < > <= >= 0= 0<> 0< 0> 0<= 0>=`), bitwise, memory
(`! @ +! -! C! C@ CMOVE`), `>R R> RSP@ RSP! RDROP DSP@ DSP!`, `BRANCH 0BRANCH` (a byte offset added
to the offset cell's address). Hand-threaded loops COUNT-TO-10 and COUNTDOWN, offsets counted by
hand. `.expected` pins the loops and one line per word family. Diff: the primitives.

### 4 — `forth-4-dictionary` (look-up by name)
Headers on every word, `LATEST`, `KEY EMIT WORD FIND >CFA EXECUTE`, `\` comments, and a hand-threaded
outer interpreter `INTERPRET` (WORD FIND, run it or print `?`) under `QUIT` (INTERPRET BRANCH −8). No
numbers; demo words `HI` and `STAR`. Input `HI` / `STAR STAR STAR CR` / `KEY A EMIT KEY B EMIT CR` /
`HI BOGUS CR` → `HI` / `***` / `AB` / `HI` / `?`. Diff: the dictionary and the outer loop.

### 5 — `forth-5-numbers` (the number rule)
`NUMBER` (optional `-`, digits 0–9 A–Z below BASE; returns n and the unparsed count) and `BASE`.
INTERPRET pushes a word that is not found but parses. An RPN calculator, as the `rpn` demo, but
extensible. Input covers `+ - * /MOD`, a negative, `16 BASE !` (then `10` is sixteen and `A BASE !`
returns to decimal), `ROT`, and `12X` → `?`. Diff: NUMBER, BASE, one rule; HI/STAR removed.

### 6 — `forth-6-compiling` (compile vs interpret)
`STATE HERE CREATE , [ ] IMMEDIATE HIDDEN HIDE '`, and `:` `;` written in Forth (as jonesforth).
INTERPRET (45 threaded cells) appends codewords and `LIT n` while STATE is 1, and runs F_IMMED words
anyway. Input defines SQUARE/CUBE/SHOUT, shows an IMMEDIATE word running at compile time, a
redefinition that calls the older word of the same name (`:` hides the new one), HIDE, and `'`.
Diff: the compiler.

### 7 — `forth-7-quit` (jonesforth's outer interpreter)
INTERPRET becomes one asm primitive with the same rule; `QUIT` is `R0 RSP! INTERPRET BRANCH −8`; an
unknown word prints `PARSE ERROR: <word>`; `TELL CHAR S0 R0`. Input shows the error, `: ECHO WORD
TELL ;`, CHAR, EXECUTE on a `'` token, and the return-stack depth at the top level (0) and inside a
word (4). Diff: INTERPRET in asm, the error path, the stack bases.

### 8 — `forth-8-control` (the language extends itself)
The asm is rung 7 minus the `.` and `CR` primitives. The input is `jonesforth.f` lines 1–402
verbatim (`/ MOD CR SPACE NEGATE TRUE FALSE NOT LITERAL`, character constants, `[COMPILE] RECURSE`,
`IF ELSE THEN BEGIN UNTIL AGAIN WHILE REPEAT UNLESS`, `( )` comments, `NIP TUCK PICK SPACES DECIMAL
HEX`, `U. .S UWIDTH U.R .R . U. ?`), then a program: nested IF, a recursive factorial, the loops,
UNLESS, `.S` (prints top first: `3 2 1 `), PICK, HEX and `.R`, `/` and `MOD` truncating toward zero.
Diff: no new asm; IF/THEN compute the branch offsets that rung 3 counted by hand.

### 9 — `forth-9-prelude` ("full enough")
The asm adds `LITSTRING`, `>DFA` (written in Forth), and the constants `DOCOL F_IMMED F_HIDDEN
F_LENMASK`. The input is `jonesforth.f` lines 1–792 and 881–1107 verbatim (leaving out FORGET and
DUMP, lines 793–880, and everything after SEE): adds `WITHIN DEPTH ALIGNED ALIGN C, S" ." CONSTANT
ALLOT CELLS VARIABLE VALUE TO +TO ID. ?HIDDEN ?IMMEDIATE WORDS CASE OF ENDOF ENDCASE CFA> SEE`. The
program prints strings compiled and interpreted, uses CONSTANT/VARIABLE/VALUE/ALLOT and CASE, runs
`SEE` on a recursive word (RECURSE decompiles as the word's own name) and on a word with a string,
and ends with `WORDS`, the whole dictionary newest first. (jonesforth's DEPTH counts bytes, not
cells; it is defined but not used.) Diff: four asm words; the rest is Forth.

## Wiring (how a rung is registered)

1. Files in `examples/src/lang/forth/`: `<rung>.asm`, `<rung>.expected`, and `<rung>.input` when the
   rung reads stdin (rungs 4–9).
2. `examples/src/meson.build`: the name in `lib_demo_tests`. No demo-table row and nothing in
   `examples/src/Makefile`: both build C, and the ladder has none.
3. `examples/tests/run-demo.sh`: the `forth-*` case arm covers every rung. It sets `asm_only`, which
   skips the C run and requires the spim run to exit 0. The port suites (`port/meson.build`) run the
   same list through each engine port; `port/harness/compare_all_programs.py` also runs every rung on
   empty stdin, which ends rungs 4–9 at once.
4. `tools/check_example_commands.py` reads BUILD AND RUN headers from `.c` files only, so the ladder
   has none; each `.asm` header gives its invocation instead.
5. The dictionary blocks were first written out by a one-shot script; they are now ordinary source.
   A new built-in word is a header added by hand after the last one, with `var_LATEST` pointed at it
   (and the next header's link, if it is inserted in the middle).

Verify a rung: `examples/tests/run-demo.sh <builddir>/examples/src <builddir>/spimulator
src/exceptions.s <rung>` (the C-binary directory is not read for a Forth rung), or `meson test -C
<builddir> --suite examples`. rung 9 takes about 1.5 s under the C engine's debug build and about the
same under the Python port.
