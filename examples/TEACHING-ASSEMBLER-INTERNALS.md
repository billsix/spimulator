# Teaching assembler internals with spim

For students who've worked through the demos under `src/` and want
to look one layer deeper — *how does the assembler actually turn
the text I wrote into bytes in memory?* — spim ships with four
teaching surfaces that together cover that question.

This doc walks through a tiny example program and explains what
each surface shows you, with concrete invocations and excerpted
output.

## The four surfaces

| Flag | Layer | What it shows |
|---|---|---|
| `-print-ast` | Parse | The abstract syntax tree the parser built from your source |
| `-show-expansion` | Parse | Just the pseudo-op wrappers and their parser-level expansions |
| `-listing` | Assemble | An ordered event trace: every label defined, instruction emitted, data byte written, forward reference resolved |
| `-explain` | Runtime | Per-instruction narration during execution — encoding, semantics, register effects |

Together they let a student answer:

- *What did the parser see?* → `-print-ast`
- *What did the parser rewrite my pseudo-ops into?* → `-show-expansion`
- *What bytes ended up in memory and in what order?* → `-listing`
- *What does each instruction do when it runs?* → `-explain`

## The two parser modes — when each is active

spim has two parser implementations that produce byte-identical
results:

- **SDT (syntax-directed translation)** — the default.  As the
  parser reads each statement, it directly calls action helpers
  that write into memory and the symbol table.  No tree is built.
  This is what every plain `spimulator -f foo.asm` invocation uses.
- **AST** — the parser builds an abstract syntax tree first, then
  a separate `emit_ast` pass walks the tree calling the same action
  helpers.  Enables structural inspection (`-print-ast`,
  `-show-expansion`, `-print-ast-json`).

You almost never need to think about the modes.  The four teaching
flags below set up the right mode automatically:

| Flag | Implicit mode |
|---|---|
| `-listing FILE` | either (observer fires the same way in both modes) |
| `-explain` | either |
| `-print-ast` | AST (also skips emit so spim just dumps the tree and exits) |
| `-show-expansion` | AST (also skips emit) |
| `-print-ast-json` | AST (also skips emit) |

If you want to force a mode explicitly:

```sh
spimulator -parser=sdt -f foo.asm     # SDT (same as no flag)
spimulator -parser=ast -f foo.asm     # AST as the driver
```

The `-parser=ast` form is useful when you want to **run** the
program through the AST path (no inspection, no skip-emit) — for
example, when checking that AST mode produces the same output as
SDT for a particular program.

## A worked example

Save this as `hello.asm`:

```asm
        .text
        .globl  main
main:
        li      $v0, 4           # syscall 4 = print_string
        la      $a0, msg         # load address of msg
        syscall
        li      $v0, 17          # syscall 17 = exit2
        addi    $a0, $0, 0       # exit code 0
        syscall

        .data
msg:    .asciiz "hello, world\n"
```

Then run each surface in turn.

### 1. `-print-ast`: the tree

```sh
spimulator -noexception -print-ast -f hello.asm
```

Output (excerpted, indentation collapsed):

```
[line 1] FILE source=hello.asm
  [line 1] DIR_TEXT
  [line 2] DIR_GLOBL name=main
  [line 3] LABEL_DEF name=main (placement)
  [line 4] PSEUDO mnemonic=li
    [line 5] INST_I op=544 rt=2 rs=0 imm=4
  [line 5] PSEUDO mnemonic=la
    [line 6] INST_I op=544 rt=4 rs=0 imm=msg
  [line 6] INST_R op=615 rd=0 rs=0 rt=0
  [line 7] PSEUDO mnemonic=li
    [line 8] INST_I op=544 rt=2 rs=0 imm=17
  [line 8] INST_I op=313 rt=4 rs=0 imm=0
  [line 9] INST_R op=615 rd=0 rs=0 rt=0
  [line 11] DIR_DATA
  [line 12] LABEL_DEF name=msg (placement)
  [line 12] DATA_STRING len=13 null_term=yes value="hello, world\n"
```

This is the **shape** of your source as the parser understands it.
Notice that:

- Each `li`, `la` source line has become a `PSEUDO` node — the
  parser knows you wrote a pseudo-op and what real instruction
  it's rewriting into.
- `syscall` is `INST_R` (op=615) with all-zero register fields.
- `.asciiz "hello, world\n"` became one `DATA_STRING` node with
  length=13 and `null_term=yes` (the trailing `\0`).

The tree is the same data structure the assembler uses to drive
emit — there is nothing else hidden.

### 2. `-show-expansion`: just the rewrites

```sh
spimulator -noexception -show-expansion -f hello.asm
```

Output:

```
[line 4] PSEUDO mnemonic=li
  [line 5] INST_I op=544 mnemonic=ori rt=2 rs=0 imm=4
[line 5] PSEUDO mnemonic=la
  [line 6] INST_I op=544 mnemonic=ori rt=4 rs=0 imm=msg
[line 7] PSEUDO mnemonic=li
  [line 8] INST_I op=544 mnemonic=ori rt=2 rs=0 imm=17
```

Strips away everything that isn't a pseudo-op wrapper, so you can
quickly see *exactly* what gets rewritten.  Reading this, a student
should walk away knowing:

- `li $rt, IMM` becomes `ori $rt, $0, IMM` (the parser-level
  rewrite — for small immediates that fit in 16 bits).
- `la $rt, label` becomes `ori $rt, $0, label`-style addressing
  (the parser's first cut; the encoder may further split into
  `lui + ori` if the address doesn't fit 16 bits — that step
  isn't visible in the AST but does show in the listing below).

### 3. `-listing`: every byte the assembler writes

```sh
spimulator -noexception -listing - -f hello.asm 2>&1
```

Output:

```
line   12  label  msg                      defined at 0x10010000
line   12  data   0x10010000  string  len=13 (+\0)
line    3  label  main                     defined at 0x00400000  (global)
line    5  text   0x00400000  ori $v0, $r0, 4              0x34020004
line    6  text   0x00400004  lui $at, 4097 [msg]          0x3c011001
line    6  text   0x00400008  ori $a0, $at, 0 [msg]        0x34240000
line    6  text   0x0040000c  syscall                      0x0000000c
line    8  text   0x00400010  ori $v0, $r0, 17             0x34020011
line    8  text   0x00400014  addi $a0, $r0, 0             0x20040000
line    9  text   0x00400018  syscall                      0x0000000c
```

This is the **emit** trace.  Things to notice:

- `la $a0, msg` (one source line) emitted as **`lui $at, ...; ori
  $a0, $at, ...`** — two instructions.  The parser's PSEUDO node
  showed only one INST_I (the parser-level rewrite), but the
  encoder split it further when the immediate didn't fit.  *This
  is the deeper layer of expansion that `-show-expansion` doesn't
  capture* — you only see it by looking at what actually got
  written.
- The hex encoding (right column) is the literal 32-bit word
  written at that address.  You can verify by hand: `ori $v0, $0,
  4` should be `0x34020004` (opcode 0x0d, rs=0, rt=2, imm=4).
- The `.data` directive's string emitted first (at 0x10010000)
  even though it appeared *after* the text section in source order
  — the directive is what decides the segment, not the source
  position.

### 4. `-explain`: each instruction at runtime

```sh
spimulator -explain=2 -f hello.asm
```

For each executed instruction this prints multi-line narration:
the bit-layout diagram, the decoding annotation (e.g. "opcode=0x0d
selects `ori`"), the symbolic effect ("Compute $r0 + 4; result is
placed in $v0"), and a snapshot of registers/memory that changed.
Much longer per-instruction; runtime focus.

## What each surface is good for

- **`-print-ast` / `-print-ast-json`** — when you're curious about
  the parser as a thing in itself, or you want to write tooling
  that consumes the structure (the `-json` variant is a clean data
  surface for IDE plugins, GUIs, autograders, etc.).

- **`-show-expansion`** — for a quick "what's the rewrite shape"
  answer.  Particularly clear in office-hours conversations where
  a student asks "is `move` an instruction?".

- **`-listing`** — for understanding the **assemble-time** mechanic
  of what's actually getting written to memory.  Pairs naturally
  with `-print-ast` for the parse-vs-emit comparison.

- **`-explain`** — for understanding the **runtime** mechanic of
  individual instructions.

## Suggested reading order for the internals

1. Work through `src/intro/` and at least one demo from
   `src/algorithms/` to get comfortable writing MIPS.
2. Re-run any of those programs with `-explain=2` to see runtime
   narration.
3. Try `-show-expansion` on something with `la` and `li` to see
   the parser-level pseudo rewrites.
4. Then `-print-ast` on the same program to see the full tree.
5. Finally `-listing - 2>&1 | less` to follow the assembler's
   byte-by-byte commit order.

The combined experience: starting from source you wrote, you can
trace exactly how each layer turns it into runnable bytes.

## Where the layers stop being visible

Three deeper layers exist but aren't directly inspectable from
these flags:

- **The MIPS instruction encoder inside `inst.c`** — when a 32-bit
  immediate doesn't fit a 16-bit field, the encoder splits it into
  `lui + ori` (or `addu + ori`).  The split happens during the
  emit pass and shows up in `-listing` as separate text events,
  but the parser-level AST doesn't capture the split.
- **The dynamic linker** — there isn't one.  spim is single-file;
  forward-reference resolution is built into the parser via the
  symbol table.
- **The CPU pipeline** — spim is an architecturally accurate
  interpreter but not a microarchitectural one.  No branch
  prediction, no pipeline stalls; `-explain` shows the
  architectural state, not the microarchitectural one.

## Source for these tools

For students who want to look at the source:

- The AST lives in `include/ast.h` and `src/ast.c`.
- The parser-to-AST construction is in `src/parser.c`.
- The emit pass that walks the AST and calls action helpers is
  also in `src/parser.c` (`emit_ast` near the bottom).
- The event-log layer that produces `-listing` is in
  `include/asm_event.h` and `src/asm_event.c`.
- The runtime narration that produces `-explain` is in
  `src/explain.c`.

Each of those four pieces is independently understandable; you
can read them in any order.
