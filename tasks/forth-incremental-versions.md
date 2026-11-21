# A Forth for spim, modelled on jonesforth, built as an incremental ladder like the calculator

**Status:** proposed — needs go-ahead (open questions below)
**Priority:** 5
**Difficulty:** 8 (a threaded interpreter in MIPS asm, about nine rungs, each with goldens)
**Created:** 2026-11-21 (William Emerison Six <billsix@gmail.com>)

## BLUF

Write a Forth in spim assembly, porting the design of Richard W.M. Jones's **jonesforth** (a
literate, public-domain i386 Forth) to MIPS and spim's syscalls. Build it the way the calculator was
built: an ordered ladder of small programs, each runnable and each adding **one** idea, from "run
three hand-threaded primitives" up to "a Forth that reads and compiles its own control structures".
Done = every rung in `examples/src/lang/forth/` with `.asm` + `.input` + `.expected` passing under
the examples test harness, the ladder recorded in a reference doc, and the rungs placed in
`examples/READING-ORDER.md`.

## Context

- Read first: `CLAUDE.md`, `tasks/reference/incremental-example-decomposition.md` (the method:
  one new idea per rung, the triage), `tasks/reference/calc-incremental-versions.md` (the model: a
  per-rung build spec with grammar, `.input` → `.expected` and "diff from the previous rung"), the
  calculator ladder itself (`examples/src/lang/calc/calc-1-numbers.*` …), its plan
  `tasks/archive/2026/10/30/calc-incremental-versions.md`, `tasks/reference/mips-asm-teaching-style.md`,
  and `examples/READING-ORDER.md`.
- **Source being ported (checked 2026-11-21):** jonesforth version 47,
  `$Id: jonesforth.S,v 1.47 2009-09-11` and `jonesforth.f,v 1.18`, read from the mirror
  https://raw.githubusercontent.com/nornagon/jonesforth/master/jonesforth.S and `…/jonesforth.f`.
  Both files name http://annexia.org/forth as the home page (not fetched). Public domain, so a port
  has no licence constraint; credit it in each file's header anyway.
- **jonesforth's design, as read from the source:**
  - **Indirect threading.** `NEXT` = `lodsl; jmp *(%eax)`: load the next codeword address through
    the instruction pointer, then jump through it. Registers: `%esi` = instruction pointer, `%esp` =
    data stack, `%ebp` = return stack, `%eax` = the current codeword (which `DOCOL` uses to find the
    word's body).
  - **Dictionary header:** 4-byte link to the previous word, 1 byte of length (low 5 bits, max 31) +
    flags (`F_IMMED` 0x80, `F_HIDDEN` 0x20), the name zero-padded to 4 bytes, then the codeword
    (`DOCOL` for words written in Forth, the asm label for primitives). `LATEST` points at the
    newest word.
  - **Order of jonesforth.S:** NEXT / threading → `DOCOL` + return-stack macros → start-up → the
    `defword`/`defcode` macros → primitives (stack: `DROP SWAP DUP OVER ROT -ROT 2DROP 2DUP 2SWAP
    ?DUP`; arithmetic: `1+ 1- 4+ 4- + - * /MOD`; comparisons `= <> < > <= >= 0= …` returning 1/0, not
    ANS −1/0; bitwise `AND OR XOR INVERT`) → `EXIT` → `LIT` → memory `! @ +! -! C! C@ CMOVE` →
    variables `STATE HERE LATEST S0 BASE` → constants → return-stack words `>R R> RSP@ RSP! RDROP`
    → `DSP@ DSP!` → I/O `KEY EMIT WORD NUMBER` → lookup `FIND >CFA >DFA` → compiling `CREATE , [ ]
    : ;` → `IMMEDIATE HIDDEN HIDE '` → `BRANCH 0BRANCH` → `LITSTRING TELL` → `QUIT INTERPRET` →
    `CHAR EXECUTE` + syscalls. (The second half of the file was read from a summary, not word for
    word; re-read it before writing rungs 6–7.)
  - **jonesforth.f** then defines in Forth: `/ MOD`, `CR SPACE NEGATE TRUE FALSE NOT`, `LITERAL`,
    `[COMPILE] RECURSE`, `IF THEN ELSE`, `BEGIN UNTIL AGAIN WHILE REPEAT UNLESS`, nesting `( )`
    comments, `NIP TUCK PICK SPACES DECIMAL HEX`, number printing `U. . .S`, strings `S" ."`,
    `CONSTANT ALLOT CELLS VARIABLE`, `VALUE TO`, `WORDS FORGET DUMP`, `CASE`, the decompiler `SEE`,
    and later `CATCH THROW`, C strings, files, and an inline assembler.
- **MIPS/spim mapping (a proposal, to settle in rung 1):** IP in a saved register (e.g. `$s7`),
  data stack on `$sp`, return stack in another (e.g. `$s6`) over a `.data` buffer, `W` in `$t0`.
  `NEXT` = `lw $t0, 0($s7)` / `addiu $s7, $s7, 4` / `lw $t1, 0($t0)` / `jr $t1` (spim has no `.macro`
  in every mode; check, else repeat the four lines or jump to a shared `next:`). Linux syscalls map
  to spim's: read char (12) for `KEY`, print char (11) for `EMIT`, `sbrk` (9) for dictionary space,
  exit (10) for `BYE`. Dictionary and compiled code live in `.data`/heap (threaded code is data, so
  nothing writes the text segment). The inline assembler, files and `SYSCALL3` are out of scope.

## The ladder (proposal; one new idea per rung)

Each rung reuses the previous rung's code and lists its diff, as the calc spec does.

1. **`forth-1-next`** — a hand-written threaded program in `.data`
   (`LIT 2 LIT 3 + . BYE`) run by `NEXT`. Primitives `LIT + .`, a data stack. Idea: threaded code
   and the inner interpreter. No dictionary, no input.
2. **`forth-2-docol`** — hand-threaded colon words: `DOCOL` / `EXIT` and the return stack
   (`SQUARE = DUP * EXIT`, called from the main thread). Idea: nesting calls.
3. **`forth-3-primitives`** — the stack, arithmetic, comparison, memory words and `BRANCH`/`0BRANCH`,
   shown by a hand-threaded loop that prints 1 to 10. Idea: control flow inside threaded code.
4. **`forth-4-dictionary`** — headers, `LATEST`, `KEY`, `WORD`, `FIND`, `>CFA`, and an outer loop that
   reads words from stdin and executes them. No numbers yet (input like `HELLO HELLO`). Idea: look-up
   by name.
5. **`forth-5-numbers`** — `NUMBER`, `BASE`; unknown words that parse as numbers are pushed. It is now
   an RPN calculator (`2 3 + .`), a link to the `rpn` ladder. Idea: the interpreter's number rule.
6. **`forth-6-compiling`** — `HERE`, `STATE`, `,`, `CREATE`, `[ ]`, `:` `;`, `IMMEDIATE`, `HIDDEN`, `'`.
   The user defines words (`: SQUARE DUP * ; 7 SQUARE .`). Idea: compile vs interpret mode.
7. **`forth-7-quit`** — `QUIT`/`INTERPRET` as in jonesforth, `LITSTRING TELL CHAR EXECUTE EMIT`, a
   "PARSE ERROR" message for an unknown word, return-stack reset. Idea: the full outer interpreter.
8. **`forth-8-control`** — Forth source fed on stdin (from jonesforth.f's first part): `IF THEN ELSE`,
   `BEGIN UNTIL AGAIN WHILE REPEAT`, `( )` comments, `RECURSE`, then `.` / `.S` written in Forth.
   Idea: the language extends itself with `IMMEDIATE` words.
9. **`forth-9-prelude`** ("full enough") — `S" ."`, `VARIABLE CONSTANT ALLOT VALUE TO`, `WORDS`,
   `CASE`, `SEE`. Stop here unless Open question 3 says otherwise.

## Plan

1. Re-read jonesforth.S/.f in full (the second halves came from a summary) and confirm the ladder.
2. Write `tasks/reference/forth-incremental-versions.md`, the per-rung build spec (the
   calc-incremental-versions shape), before any code.
3. Build the rungs in order, each with goldens, under the examples test harness; add them to
   `examples/READING-ORDER.md`.

## Open questions

1. **C twins:** every calc rung is a `.c` + `.asm` pair. Should each Forth rung also have a C version
   (threading through an array of function pointers), or is the Forth asm-only like jonesforth?
   *Recommend asm-only; jonesforth is asm by design, and a function-pointer Forth in C teaches
   something different.*
2. **Truth values:** keep jonesforth's 1/0 from comparisons, or use ANS Forth's −1/0? *Recommend
   jonesforth's 1/0, since the port follows it; note the difference in the reference doc.*
3. **End point:** is rung 9 "full enough", or should the ladder go on to `CATCH`/`THROW` and
   `FORGET`/`DUMP`? *Recommend stopping at rung 9.*
4. **Asm teaching style:** the repo's stack-visible style (`tasks/reference/mips-asm-teaching-style.md`:
   each C local in a `-N($fp)` slot, symbol table, stack-frame snapshot) assumes C-shaped routines.
   A threaded Forth keeps its state in fixed registers and two hand-run stacks. Mark the Forth rungs
   exempt (like the register/idiom demos), with a header that documents the register map instead?
   *Recommend yes.*
5. **Reading-order placement:** after the calculator ladder (the end of `lang/`), or as its own
   section after the PGU port? *Recommend right after `calc-tree`; it is the next "language" lesson.*
