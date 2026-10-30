# calc incremental versions — the per-rung build spec

**Reference document** — the deterministic blueprint for the six leader rungs of the calculator
ladder. Read with `tasks/calc-incremental-versions.md` (the plan/decisions) and
`tasks/reference/incremental-example-decomposition.md` (the method). Each rung is a `.c` + a matching
spim `.asm` + `.input` + `.expected`, trimmed from the finished `calc-sdt.{c,asm}`. Integer-first
(rungs 1–5 integer; rung 6 introduces the FPU/decimals); each rung does its own stdin I/O; no
whitespace and no error path until the final `calc-sdt` (rung 7, unchanged). Written 2026-10-29
(William Emerison Six <billsix@gmail.com>).

## The asm idiom (from `calc-sdt.asm`, the proven reference)

- Entry label `main` (`.globl main`); `move $s0,$ra` on entry, `move $ra,$s0; li $v0,0; jr $ra` to
  exit. Read one byte: `li $v0,12; syscall` → `$v0` (−1 at EOF). `print_char`: `li $v0,11; li $a0,c;
  syscall`. `print_int` (signed): `li $v0,1; move $a0,r; syscall`. `print_string`: `li $v0,4; la
  $a0,label; syscall`. The lookahead char lives in a `.data` word (`curchar`); `advance` re-reads it.
- Integer rungs accumulate digits in a `$t` register (`acc = acc*10 + (c-'0')`) and print with
  syscall 1 — no FPU. Rung 6 switches to doubles exactly as `calc-sdt.asm` does
  (`cvt.d.w`, `add.d`/`sub.d`/`mul.d`/`div.d`, `trunc.w.d`, the `print_double` routine).

## The rungs

Grammar grows one rule per rung. `DIGITS` = `[0-9]+` (rungs 1–5); at rung 6 `NUMBER` gains an optional
`.`-fraction. Inputs are space-free and well-formed through rung 6.

### 1 — `calc-1-numbers`  (read & print one whole integer per line)
Grammar: `line := DIGITS`. C: `int`, `read_char` loop accumulating digits, `print_int`, per-line loop.
`.input` → `.expected`: `0`→`0`, `7`→`7`, `42`→`42`, `100`→`100`.
Diff from nothing: the number tokenizer + the per-line read/print loop.

### 2 — `calc-2-addsub`  (binary `+` `-`, left to right, integer)
Grammar: `expr := DIGITS (('+'|'-') DIGITS)*`. A left-fold loop over the operator-then-operand tail.
`.input`→`.expected`: `7`→`7`, `1+2`→`3`, `10-3`→`7`, `2+3-1`→`4`.
Diff from rung 1: the `expr` fold loop around the number.

### 3 — `calc-3-muldiv`  (`*` `/`, integer, binding tighter)
Grammar: `expr := term (('+'|'-') term)*`, `term := DIGITS (('*'|'/') DIGITS)*`. Integer `div`/`mflo`.
`.input`→`.expected`: `2*3`→`6`, `12/4`→`3`, `2+3*4`→`14`, `10-2*3`→`4`.
Diff from rung 2: split the operand into a `term` layer (precedence by nesting).

### 4 — `calc-4-parens`  (grouping)
Grammar: `factor := DIGITS | '(' expr ')'`; `term` over `factor`. Mutual recursion `expr↔factor`;
save the operand across the recursive call (`addi $sp,-16` frame, as `calc-sdt.asm`).
`.input`→`.expected`: `(2+3)*4`→`20`, `2*(3+4)`→`14`, `((1+2))`→`3`, `(10-2)/4`→`2`.
Diff from rung 3: the `factor` layer + the `'(' expr ')'` rule.

### 5 — `calc-5-neg`  (unary minus / negative integers)
Grammar: `factor := … | '-' factor`. A prefix rule recursing on itself; `neg`/signed `print_int`.
`.input`→`.expected`: `-5`→`-5`, `-5+3`→`-2`, `-(2+3)*2`→`-10`, `3*-2`→`-6`.
Diff from rung 4: the `'-' factor` prefix rule.

### 6 — `calc-6-float`  (the FPU: decimal literals + double arithmetic)
Grammar unchanged; `NUMBER` gains an optional `.`-fraction and all arithmetic becomes `.d`. This is
`calc-sdt.{c,asm}` minus `skip_blanks` and minus the error path — doubles throughout, the
`print_double` routine from `calc-sdt`. **The one representation-change rung: division now yields
decimals** (`10/4` → `2.500000`, not `2`).
`.input`→`.expected`: `10/4`→`2.500000`, `3.5+1`→`4.500000`, `(3+4)*2-10/4`→`11.500000`, `.5*2`→`1`.
Diff from rung 5: int → double across the number parser and every operator.

### 7 — `calc-sdt`  (the finished demo — UNCHANGED)
Adds `skip_blanks` (intra-line whitespace, blank lines) + per-line `error` recovery to rung 6. Its
existing `calc-sdt.input`/`.expected` already pin spaces + the `error` case. Not rewritten by this
work; it is the destination. `calc-tree` stays the architectural epilogue (shares calc-sdt's golden).

## Wiring checklist (per new rung)

1. Files in `examples/src/lang/calc/`: `<rung>.c`, `<rung>.asm`, `<rung>.input`, `<rung>.expected`
   (license header verbatim from `calc-sdt.c`/`.asm`; C in the paired-demo teaching style —
   `tasks/reference/example-c-teaching-style.md`: locals at top, flat control flow).
2. `examples/src/meson.build`: add `['<rung>', 'lang/calc/<rung>.c'],` to the demo table (after the
   `calc-*` rows) **and** add `'<rung>'` to the `lib_demo_tests` list.
3. `examples/tests/run-demo.sh`: add a `case` arm setting `expected`/`input`/`asm_files` for `<rung>`.
4. `examples/READING-ORDER.md`: list the rungs as sub-entries under calc's entry (#42).
5. Verify: `meson compile -C builddir` then `meson test -C builddir --suite examples` (or
   `examples/tests/run-demo.sh <builddir> <spim> <exception-file> <rung>`) — both the native C binary
   and the asm-under-spim must match the golden.

## Status

Spec complete 2026-10-29. Implementation proceeds rung 1 → 6, each verified before the next.
