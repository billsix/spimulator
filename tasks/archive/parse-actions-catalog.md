# Parse-actions catalog

Phase 2a deliverable.  Enumerates every external (state-mutating)
call from `src/parser.c` into the simulator's downstream layers
(`inst.c`, `data.c`, `sym-tbl.c`, the segment/PC globals), and
proposes how each absorbs into the AST in Phase 2b.

Total external call sites in parser.c at this snapshot: **167**.
The vast majority (~135) are instruction-emission calls — pseudo-op
expansion accounts for most of the multiplicity.

---

## 1. Instruction emission

Six helper families produce one MIPS instruction each.  All live in
`inst.c` and end at `store_instruction()` which allocates an
`instruction*`, writes it via `mem_write_inst`, and bumps the text
PC.

| Action helper | Carries | Call sites in parser.c | Proposed AST node |
|---|---|---|---|
| `r_type_inst(op, rd, rs, rt)` | opcode, 3 ints | ~60 | `AST_INST_R` |
| `r_sh_type_inst(op, rd, rt, shamt)` | opcode, 2 regs, 1 shamt | ~12 | `AST_INST_R_SHIFT` |
| `i_type_inst(op, rt, rs, imm_expr*)` | opcode, 2 regs, imm_expr | ~30 | `AST_INST_I` |
| `i_type_inst_free(op, rt, rs, imm_expr*)` | same, takes ownership | ~25 | `AST_INST_I` |
| `j_type_inst(op, imm_expr*)` | opcode + target imm_expr | 2 | `AST_INST_J` |
| `r_co_type_inst(op, fd, fs, ft)` | opcode + 3 FP/co indices | ~12 | `AST_INST_FP_R` |
| `r_cond_type_inst(op, fs, ft, cc)` | opcode + 2 FP regs + cc | 1 | `AST_INST_FP_COMPARE` |

**Notes:**

- `i_type_inst` vs `i_type_inst_free` is purely a memory-ownership
  distinction (does the helper free `imm_expr`?); AST-side, both
  become `AST_INST_I` and ownership lives in the node payload.
- `i_type_inst_full_word(op, rt, rs, expr, value_known, value)`
  is an internal `inst.c` helper called *by* `i_type_inst` when an
  immediate doesn't fit 16 bits.  Not called directly by parser.c
  — the AST builder doesn't need to model it; it's an emit-pass
  detail.
- For each emitted instruction the action chain also (potentially)
  calls `record_inst_uses_symbol` if the immediate references an
  unresolved label.  See section 3.

### Where the call sites cluster

Most of the multiplicity is in `parse_pseudo()`'s expansion of
pseudo-ops into multiple real instructions.  Examples:

- `parse_pseudo` → `TOK_ROL_POP` / `TOK_ROR_POP` (parser.c
  ~899-919) emits 4 instructions per source line.
- `parse_pseudo` → `TOK_ULW_POP` (parser.c ~1151) emits 2-3.
- `parse_pseudo` → `TOK_BGE_POP` / `TOK_BLE_POP` /
  `TOK_BGT_POP` / `TOK_BLT_POP` (parser.c ~1240-1326) each emit
  1-3 instructions depending on whether the second operand is a
  register or immediate.

These are the **pseudo-op expansion sites** that Phase 3 will
collapse into `AST_PSEUDO` nodes wrapping their expansion as
children.  Phase 2 keeps them inline (multiple AST_INST_* nodes
per source line); Phase 3 introduces the wrapping.

---

## 2. Data emission

Three families produce bytes in the data segment.  Two go through
a function-pointer dispatch (`store_op` / `store_fp_op`) so the
generic `parse_expr_list` / `parse_fp_expr_list` can serve all
data directives.

| Action helper | Carries | Call sites | Proposed AST node |
|---|---|---|---|
| `store_byte(int v)` | one byte (low 8 bits of v) | indirect via store_op | `AST_DATA_BYTE` |
| `store_half(int v)` | one halfword | indirect | `AST_DATA_HALF` |
| `store_word(int v)` | one word | indirect | `AST_DATA_WORD` |
| `store_double(double* v)` | one double | indirect via store_fp_op | `AST_DATA_DOUBLE` |
| `store_float(double* v)` | one float (stored as 4 bytes) | indirect | `AST_DATA_FLOAT` |
| `store_string(s, len, null_term)` | string + length + NUL flag | 1 (parser.c:808) | `AST_DATA_STRING` |

**Dispatch detail:**

`parse_dir_word` / `parse_dir_half` / `parse_dir_byte` /
`parse_dir_float` / `parse_dir_double` each set a module-level
function pointer (`store_op` or `store_fp_op`) to one of the
above before calling the generic `parse_expr_list` /
`parse_fp_expr_list`.  This dispatch goes away in the AST
version — `parse_dir_word` produces `AST_DATA_WORD` nodes
directly.

**`AST_DATA_*` shape consideration:**

Each data directive (e.g. `.word 1, 2, 3`) emits multiple
values.  Two design options for the AST:

- **One node per value** — `AST_DATA_WORD{value:1}`,
  `AST_DATA_WORD{value:2}`, etc.  Simpler shape, more nodes.
- **One node per directive with a value list** —
  `AST_DATA_WORD{values:[1,2,3]}`.  Fewer nodes, slightly more
  complex emit.

Recommend **one node per directive** because it preserves the
source intent (`.word 1, 2, 3` is one user action), and the
listing/print-AST output reads naturally.

---

## 3. Symbol-table operations

| Action helper | Where called from parser.c | Proposed AST node / behavior |
|---|---|---|
| `record_label(name, addr, resolve_uses=0)` | `parse_opt_label` for `ID :` (line 1709) | `AST_LABEL_DEF{kind=normal, name, location=current PC}` |
| `record_label(name, addr, resolve_uses=1)` | `parse_opt_label` for `ID = EXPR` (line 1720) | `AST_LABEL_DEF{kind=const, name, value}` |
| `record_label(name, current_data_pc(), 1)` | `parse_dir_extern` (line 695) | `AST_DIR_EXTERN{name, size}` (label def is implicit) |
| `record_label(name, current_data_pc(), 1)` | `parse_dir_comm` (line 712) | `AST_DIR_COMM{name, size}` |
| `make_label_global(sym)` | `parse_dir_globl` (line 671), `parse_dir_extern` (line 693), `parse_dir_comm` (in some paths) | `AST_DIR_GLOBL{name}` *or* a `global=true` flag on the next `AST_LABEL_DEF` |
| `record_data_uses_symbol(addr, sym)` | `parse_factor` (line 203) — forward reference in a `.word EXPR` where EXPR is an undefined identifier | Disappears in the two-pass emit — pass 1 populates the symbol table, pass 2 resolves the reference. |

**Forward references implicit in `i_type_inst`:**
`i_type_inst` checks if its `imm_expr` references an unresolved
label and (internally) calls `record_inst_uses_symbol`.  In the
AST world this also disappears — the AST node holds the imm_expr
including the symbol reference; pass 2 resolves at emit time.

**Symbol-table population timing — important architectural
change:**

Today, labels enter the symbol table at parse time.  In the
two-pass AST model, labels enter the symbol table during **pass
1** of the emit phase, *not* at parse.  This affects:

- The REPL `print_symbols` command — must run after `parse_file`
  + `emit_ast` (today it can run after just parse).  Probably
  unaffected because the REPL invokes after a successful file
  load anyway.
- The `-explain` mode — must not look up symbols during parse,
  only during runtime which is post-emit.  Already true.
- Any future tooling that wants to inspect "what symbols were
  defined?" — must wait until emit completes.

---

## 4. Segment + PC control

| Action helper | Where called | Proposed AST node |
|---|---|---|
| `user_kernel_text_segment(false)` | `parse_dir_text(kernel=false)` (line 655) | `AST_DIR_TEXT{kernel=false}` |
| `user_kernel_text_segment(true)`  | `parse_dir_text(kernel=true)` | `AST_DIR_KTEXT` |
| `user_kernel_data_segment(false)` | `parse_dir_data(false)` (line 643) | `AST_DIR_DATA{kernel=false}` |
| `user_kernel_data_segment(true)`  | `parse_dir_data(true)` | `AST_DIR_KDATA` |
| `set_text_pc(addr)` | `parse_dir_text` (line 660) when `.text ADDR` is used | Optional `start_addr` payload on `AST_DIR_TEXT`/`AST_DIR_KTEXT` |
| `set_data_pc(addr)` | `parse_dir_data` (line 650) when `.data ADDR` is used | Optional `start_addr` payload on `AST_DIR_DATA`/`AST_DIR_KDATA` |
| `increment_data_pc(sz)` | `parse_dir_space` (line 724), `parse_dir_extern` (698), `parse_dir_comm` (715) | Embedded in `AST_DIR_SPACE{size}` / `AST_DIR_EXTERN{size}` / `AST_DIR_COMM{size}` |

The `text_dir` / `data_dir` parser globals can be derived during
the emit pass from the most recent segment-change node; no AST
node needed for the flag itself.

---

## 5. Alignment

| Action helper | Where called | Proposed AST node |
|---|---|---|
| `align_text(N)` | `parse_dir_align` (line 679) when in text segment | `AST_DIR_ALIGN{n}` |
| `align_data(N)` | `parse_dir_align` (681) when in data segment, also `parse_dir_comm` (710) | same |
| `set_data_alignment(N)` | `parse_dir_word/half/float/double` to record that the next data datum should be auto-aligned to 2^N | No node — recomputed in emit pass from the directive type. |
| `enable_data_alignment()` | `parse_dir_data` | Part of `AST_DIR_DATA` semantics — implicit. |
| `align_labels_to(N)` | `parse_dir_word/half/float/double` — local parser helper that pre-fixes `this_line_labels` to the aligned address | Disappears in the AST model.  Pass 1 of emit assigns label addresses *after* alignment; there's nothing to fix up retroactively. |

`align_labels_to` and the `this_line_labels` cons-list are the
parser's workaround for the fact that today, labels are entered
into the symbol table *before* the alignment caused by the next
directive bumps the PC.  In the two-pass AST model, pass 1 walks
nodes in order, so the alignment node's PC bump happens *before*
the next label-def node's address is recorded.  The workaround
goes away naturally.

This is a clean architectural win — `fix_current_label_address`
+ `this_line_labels` + `align_labels_to` are about 30 lines of
parser plumbing that simply disappear.

---

## 6. Helpers called at parse time (not actions)

These don't mutate the simulator's memory but *do* read or
construct values used by action helpers.  All but `make_imm_expr`
(for branches) and the `current_*_pc` family can be called either
at parse or emit time with no semantic change.

| Helper | Side effect | Phase-2 disposition |
|---|---|---|
| `make_imm_expr(offset, sym, pc_relative)` | allocates payload | Called at parse to populate AST node imm fields |
| `make_addr_expr(offset, sym, reg)` | allocates payload | Same |
| `const_imm_expr(v)` | allocates payload | Same |
| `eval_imm_expr(e)` | reads the expr's int value if no symbol | OK to call at parse for constant-imm tracking |
| `lookup_label(name)` | reads/inserts a label entry | Inserts a *placeholder* row when missing — fine to keep at parse; pass 1 of emit will fill addr. |
| `current_text_pc()` / `current_data_pc()` | reads parser-owned PC | Becomes `current_emit_pc()` during pass 1. |
| `addr_expr_imm(addr)` / `addr_expr_reg(addr)` | pure getters | Same role. |

**The one parse-time PC dependency to flag explicitly:**

`parse_label()` at parser.c:319-321 builds an `imm_expr` with
`offset = -current_text_pc()` to start the PC-relative
displacement calculation.  This `-pc` initial value is
overwritten by `resolve_a_label_sub` at fix-up time with the
final `(target - pc)` displacement.

In the AST model, the AST node holds just the symbol; the actual
displacement (and its initial seed) is computed during pass 2
when the use-site PC is known.

---

## 7. End-of-file

`end_of_assembly_file()` is called by `read_assembly_file` (in
spim-utils.c), not from inside the parser proper.  It clears
`in_kernel` and re-enables data auto-alignment for the next file.
In the AST world this stays where it is — `parse_file()` returns
an AST, `read_assembly_file` continues to wrap parser + emit and
calls `end_of_assembly_file()` after both.

---

## Summary of proposed AST node set (refined from PLAN-parse-tree-migration.md)

Phase 2b will define these in `include/ast.h`:

**Instruction nodes** (7):
- `AST_INST_R`, `AST_INST_R_SHIFT`, `AST_INST_I`, `AST_INST_J`
- `AST_INST_FP_R`, `AST_INST_FP_COMPARE`
- `AST_PSEUDO` (introduced in Phase 3 — wraps expansion children)

**Data nodes** (6):
- `AST_DATA_BYTE`, `AST_DATA_HALF`, `AST_DATA_WORD`
- `AST_DATA_FLOAT`, `AST_DATA_DOUBLE`
- `AST_DATA_STRING`

**Directive nodes** (8):
- `AST_DIR_TEXT`, `AST_DIR_DATA`, `AST_DIR_KTEXT`, `AST_DIR_KDATA`
- `AST_DIR_ALIGN`
- `AST_DIR_GLOBL`, `AST_DIR_EXTERN`, `AST_DIR_COMM`, `AST_DIR_SPACE`

**Label nodes** (1):
- `AST_LABEL_DEF` — covers both `ID :` (placement) and `ID = EXPR`
  (constant) variants, distinguished by a `kind` enum.

**Structural** (1):
- `AST_FILE` — root, holds an ordered list of all of the above.

**Sentinel for error recovery** (optional, for Phase 2d):
- `AST_ERROR` — placeholder when `sync_to_nl` fires, preserves
  source line for diagnostic display.

Total: **23 node types**.

---

## Disappearing surface

These are concrete pieces of code that **go away** during the
AST refactor.  Counted because the project budget gets credit
for what it deletes:

- `this_line_labels` linked-list + `cons_label`/`clear_labels`
  (~30 lines in parser.c)
- `fix_current_label_address` (~15 lines, parser.c + data.c +
  inst.c declarations)
- `align_labels_to` (~10 lines in parser.c)
- `record_inst_uses_symbol` + `record_data_uses_symbol` + the
  per-label `uses` list in sym-tbl.c (~80 lines)
- `resolve_label_uses` + `resolve_a_label_sub` (~50 lines)
- The forward-reference branches in `parse_factor`, `parse_label`,
  `parse_imm32` that compute offsets relative to the current PC
  (~30 lines scattered)

Estimated total deletion: **~215 lines** of parse-time forward-
reference and label-fix-up plumbing.

---

## Open follow-ups for Phase 2b

1. Decide list-vs-single-value shape for `AST_DATA_*` (recommended
   above: one node per directive, value list inside).
2. Decide whether `.globl` is its own node (`AST_DIR_GLOBL`) or
   becomes a `global=true` flag on the following `AST_LABEL_DEF`.
   I lean toward its own node — `.globl` can appear before the
   label or be standalone for a label defined elsewhere.
3. Confirm `AST_PSEUDO` shape — wraps expansion children with the
   original mnemonic stored separately for `-show-expansion`
   display.  This is a Phase 3 concern but the Phase 2b node-set
   should leave a sensibly-shaped hole for it.

Each is small and answerable in 10-15 minutes of design.  None
block Phase 2b from starting.
