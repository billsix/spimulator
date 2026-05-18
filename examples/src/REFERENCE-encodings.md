# MIPS instruction encoding — reference

A single page covering the three instruction formats used by every
`.asm` file in this tree.  Each `.asm` cross-references this file
instead of duplicating the boxes inline.

## R-type — three-register operations

```
┌────────┬───────┬───────┬───────┬───────┬────────┐
│ opcode │  rs   │  rt   │  rd   │ shamt │ funct  │
│   6b   │  5b   │  5b   │  5b   │  5b   │   6b   │
└────────┴───────┴───────┴───────┴───────┴────────┘
```

`opcode` is always `000000` for R-type — the actual operation is
selected by the 6-bit `funct` at the bottom of the word.  Examples
seen in the demos:

| Source                 | Real instruction       | Hex word    |
|------------------------|------------------------|-------------|
| `move $fp, $sp`        | `addu $s8, $0, $sp`    | `0x001df021`|
| `mult $a1, $a0`        | `mult $a1, $a0`        | `0x00850018`|
| `mflo $v0`             | `mflo $v0`             | `0x00001012`|
| `addu $v0, $a2, $v0`   | `addu $v0, $a2, $v0`   | `0x00c21021`|
| `jr $ra`               | `jr $ra`               | `0x03e00008`|

`shamt` is the shift amount (used by `sll`/`srl`/`sra`).  For
non-shift R-type instructions it's `00000`.

## I-type — register + 16-bit immediate

```
┌────────┬───────┬───────┬──────────────────┐
│ opcode │  rs   │  rt   │  imm (signed)    │
│   6b   │  5b   │  5b   │       16b        │
└────────┴───────┴───────┴──────────────────┘
```

The 16-bit immediate is sign-extended to 32 bits before use by
arithmetic instructions; for `lui` it's placed in the upper half
and the lower half is zero.  Examples:

| Source                | Real instruction        | Hex word    |
|-----------------------|-------------------------|-------------|
| `addi $fp, $fp, -4`   | `addi $s8, $s8, -4`     | `0x23defffc`|
| `li $t0, 0`           | `ori $t0, $0, 0`        | `0x34080000`|
| `li $v0, 4`           | `ori $v0, $0, 4`        | `0x34020004`|
| `la $a0, helloworld`* | `lui $a0, 0x1001`       | `0x3c041001`|
| `lw $v0, 0($fp)`      | `lw $v0, 0($s8)`        | `0x8fc20000`|
| `sw $t0, 0($fp)`      | `sw $t0, 0($s8)`        | `0xafc80000`|
| `beq $t0, 'a', label` | `beq $t0, ..., offset`  | varies      |

\* `la` is a one-word encoding only when the symbol's low 16 bits
are zero (i.e. the symbol is 64-KiB aligned).  Otherwise it expands
to `lui` + `ori` — see the pseudo-instruction note in each .asm.

## J-type — 26-bit jump target

```
┌────────┬────────────────────────────┐
│ opcode │  target (26-bit word idx)  │
│   6b   │           26b              │
└────────┴────────────────────────────┘
```

The actual jump address the CPU computes is:

```
   (PC[31:28]) | (target << 2)
```

i.e. the low 28 bits come from the encoded `target` field shifted
left by 2 (since instructions are word-aligned), and the high 4
bits come from the current PC.  Examples:

| Source         | Real instruction              | Hex word    |
|----------------|-------------------------------|-------------|
| `jal main`     | `jal 0x00400024`              | `0x0c100009`|
| `j loopBegin`  | `j <addr>`                    | varies      |

`jal` differs from `j` only in that it ALSO writes `PC + 4` (the
address of the next instruction) into `$ra` before jumping.

## Register name ↔ number cheat sheet

The disassembler emits the ABI register names in some places
(`$sp`, `$ra`, `$fp`, `$a0`) and the numbered forms in others
(`$s8`, `$r0`).  They're the same registers:

| Number | ABI name | Use                            |
|--------|----------|--------------------------------|
| `$0`   | `$zero`  | hard-wired zero                |
| `$1`   | `$at`    | assembler temporary            |
| `$2`–`$3` | `$v0`,`$v1` | return values             |
| `$4`–`$7` | `$a0`–`$a3` | first 4 args              |
| `$8`–`$15`,`$24`,`$25` | `$t0`–`$t9` | caller-save temps |
| `$16`–`$23` | `$s0`–`$s7` | callee-save regs          |
| `$28`  | `$gp`    | global pointer                 |
| `$29`  | `$sp`    | stack pointer                  |
| `$30`  | `$s8`    | **also `$fp`** — frame pointer |
| `$31`  | `$ra`    | return address                 |

The disassembled `addu $s8, $0, $sp` and the source `move $fp, $sp`
are the same instruction — `$fp` is just the assembler's name for
register 30.

## How to read the trace output

Each step in a `-explain=4` trace begins with:

```
Stepped at PC = 0xADDR:
    memory[0xADDR] = 0xWORD   →   real-instruction
    source line N: ...
```

The `[PC] enc → real` prefix you see above each instruction in the
`.asm` files is the same three pieces, just rendered above the
source line.

## Spim-specific gotchas to know about

### `$at` is reserved by the assembler

`$at` (register 1) is the "assembler temporary."  Multi-word
pseudo-instructions like `bne $t0, 'a', label` use it to stage
immediate operands (`ori $at, $0, 'a'; bne $at, $t0, label`).  That
much is documented above.

What the disassembler/encoding view doesn't tell you: **spim's
assembler refuses to assemble source that writes to `$at`
directly.**  Try `slti $at, $t0, 256` and you get:

```
spim: (parser) Register 1 is reserved for assembler on line N
```

The syntax error trickles forward and any labels after it appear
"undefined."  If you need a scratch register beyond `$t0..$t9`,
pick `$t9` or save/restore an `$s` register — don't reach for
`$at`.

### `jr $ra` from main always exits 0

`exceptions.s` (the startup that spim links in front of your code)
ends with `jal main; nop; li $v0, 10; syscall`.  After your `main`
returns via `jr $ra`, control falls into `li $v0, 10` (the exit
syscall) — and syscall 10's handler in spim does:

```c
spim_return_value = 0;
```

unconditionally.  Your `$v0` is ignored.  So writing
`li $v0, 1; jr $ra` to "exit with status 1" produces an exit
status of **zero**.

To actually exit with a non-zero status, issue syscall **17**
(exit2) yourself, with the desired status in `$a0`:

```
        li $a0, 1
        li $v0, 17
        syscall
```

The other demos work fine with `li $v0, 0; jr $ra` because they
all intend to exit 0 — the `li $v0, 0` is misleading documentation
(spim ignores it), and the actual exit-zero behaviour comes from
the runtime's syscall-10 hand-off.  17-nologin is the first demo
in the tree that needs a non-zero status, and its `#NOTES` block
explains the syscall-17 pattern in place.

### Negation of a char literal is rejected

`'a'` (and any other single-quoted char literal) is fine as a
*positive* immediate in spim's assembler:

```
        bne $t0, 'a', label          # works
        addi $t0, $t0, 'a'           # works
```

But the negation form **does not parse**:

```
        addi $t1, $t0, -'a'          # spim: (parser) syntax error
```

The same line in GCC's MIPS assembler would compile fine.  Spim's
narrower expression grammar trips on the `-` followed by `'`.
The fix is to compute the numeric value yourself and use it
explicitly, with a comment so readers don't have to look up the
ASCII chart:

```
        addi $t1, $t0, -97           # offset = ch - 'a'   ('a' = 97)
```

14-rot13 is where this first matters in the curriculum.
