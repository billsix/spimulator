# Spell out the abbreviated names in the C core (`src/`, `include/`)

**Status:** in progress — phase 1 (investigate) done and the policy questions answered, on
branch `spelled-out-names-c-core`; phase 2 (the rename table) is next, on go-ahead
**Priority:** 5
**Difficulty:** 7
**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)

## BLUF

Find every abbreviated or acronym name in `src/` and `include/`: variables, functions, parameters,
struct and union tags and fields, typedefs, enum constants and macros. Write each one out in full so
a new student can read the code without a decoder ring. An example is `gpr`, which becomes
`general_purpose_registers`. The task runs in three phases: investigate (an inventory with each
name's meaning), plan (a reviewed rename table and an order to apply it), and rename (in batches,
each gated). Done means the inventory's approved renames are applied, the gates pass, and the
glossary in `tasks/reference/` records every name that was kept and why.

## Request (William Emerison Six <billsix@gmail.com>, 2026-11-14)

"Sweep through all the code in include and src, and find all of the acronyms for stuff, variable
names, struct names, all names that could be written out more explicitly, to help the new student.
As an example, gpr, means 'general_purpose_register'. Perhaps these were useful back in the day,
when people had to type code all in themselves, no autocomplete, no llms. That day is not today."
The request includes investigating which names are acronyms or abbreviations, searching online for
any whose meaning the context doesn't give, and making a plan to fix them systematically.

## Context

- Scope: the 43 files in `src/` and `include/`, about 18,600 lines. A sample of what is there
  (`include/registers.h`, `include/spim.h`): `gpr`, `reg_word`, `u_reg_word`, `mem_addr`,
  `int_reg_names`, `fp_double_view`, `FPR_S`/`FPR_D`/`FPR_W`, `FCR`/`FIR`/`FCSR`, `CP0_EPC`,
  `CP0_BadVAddr`, `accept_pseudo_insts`, and the field macros `RS`/`RT`/`RD`/`IOFFSET`/`BASE`.
- Much of the naming comes from upstream SPIM (James Larus), from an era when short names saved
  typing. This repo has no upstream SPIM remote and takes no merges from it, so renames cost no
  merge pain.
- Conventions to follow: the project `CLAUDE.md` › Conventions, and the shared C standard
  (github.com/billsix/runClaudeInContainer `tasks/reference/c-coding-standard.md`, "Naming and
  comments").
- **Generated code:** `src/opcode-tables.c` is generated from `include/opcodes.tsv` by
  `tools/gen_opcodes.py` (`tasks/reference/opcode-table-generator.md`). Rename there by changing the
  generator or the TSV, then regenerate. Never hand-edit the output.
- **The ports mirror the C engine's names** (`tasks/reference/engine-ports-design.md`), so the
  glossary this task writes is the input to the three port follow-ups:
  `tasks/spelled-out-names-python-port.md`, `tasks/spelled-out-names-haskell-port.md`,
  `tasks/spelled-out-names-rust-port.md`.
- Out of scope: `examples/` and `pgu/` (teaching C in the course compiler's subset,
  `tasks/reference/example-c-teaching-style.md`), the tree-sitter grammar, and the ports.

## What counts, and what is exempt

A name is a **candidate** if a student would have to look a part of it up or guess it: a shortened
word (`reg`, `mem`, `addr`, `insts`, `fp`), an initialism (`gpr`, `epc`, `fcsr`), or a single
letter outside a tight scope.

Exempt, recorded in the glossary with the reason:

- **Externally defined names** (the shared rule, `CLAUDE.md` › "An externally-defined name always
  wins"): libc/POSIX and libedit API names, `main`'s `argc`/`argv`, names a tool looks up, CLI
  flags, and anything the tree-sitter or meson tooling dictates.
- **Text the program prints or reads.** This task renames identifiers only. Messages, teaching-mode
  output, assembler directives, register names in assembly (`$sp`, `$ra`), instruction mnemonics and
  syscall names as the user types them all stay as they are. So the regression and explain goldens
  must not change at all, and that is one of the checks.
- **Tight-scope conventional names:** a loop index `i`, a file descriptor `fd`, and the like, where
  the short form is the common word. The plan lists each kept one.
- **Acronyms more familiar than their expansion**, which are names in their own right: `MIPS`,
  `ASCII`, `IEEE`, `UTF8`. The plan lists each kept one.

**Architecture vocabulary is spelled out** in identifiers, following the maintainer's own `gpr`
example. The MIPS manual's term goes in a comment at the definition so a student can find it in the
manual: `general_purpose_registers /* the ISA's GPRs */`, `exception_program_counter /* CP0 EPC */`.

## Plan

### Phase 1 — investigate: the inventory

1. **List every declared name** mechanically, never by reading files front to back.
   - Declarations: clang's JSON AST through a clang-configured compile database (the same approach
     as `tools/c_dispatch_check.py`). Collect `VarDecl`, `ParmVarDecl`, `FunctionDecl`,
     `FieldDecl`, `RecordDecl`, `TypedefDecl`, `EnumDecl` and `EnumConstantDecl` whose location is
     in `src/` or `include/`.
   - Macros: `#define` names, discovered with `git grep -n -E '^\s*#\s*define'` over `src` and
     `include`. The AST does not show macros.
   - Save the scripts as `tasks/adhoc/spelled-out-names-c-core/discover.sh` (plus a Python helper
     for the AST walk) and the result as a worklog in `tasks/adhoc/spelled-out-names-c-core/data/`.
2. **Split each name into words** (on `_` and case changes) and flag the words not found in an
   English word list (`/usr/share/dict/words`, or hunspell's `en_US` dictionary; install it if
   missing). Flagged words, plus a short hand-review of the rest (`sub`, `rest` and `base` are real
   words that can still be abbreviations), make the candidate list.
3. **Find what each candidate means.** Use the code (its use sites, comments and the upstream SPIM
   documentation in `Documentation/`) first. Then use the MIPS32 architecture manuals and the SPIM
   documentation online for architecture terms. Record the source for every non-obvious meaning.
4. **Write the inventory** as a table in this doc (or a linked `data/` file if it is long), one row
   per distinct abbreviated *word*, not per name, since one word (`reg`) appears in dozens of names:
   word → meaning → proposed spelling → names affected (count) → exempt? and why.

### Phase 2 — plan: the rename table, for review

5. **Derive the rename table** from the word table: every affected name, old → new. Check each new
   name for a collision with an existing name, a C keyword or a libc name, and for line-length
   fallout (clang-format reflows code; comments need hand reflow, per the shared rule).
6. **Pick the batches.** One batch is one word or a tight family (all the `reg` names), applied
   across the whole tree in one commit, so each commit is one reviewable idea. Order them from least
   to most entangled: locals and parameters first, then file-static names, then the names exported
   from `include/` and the generated tables.
7. **Stop for the maintainer's review of the word table and the exemptions** before any rename.
   That review settles the policy cases (each kept short name, each kept acronym).

### Phase 3 — rename, batch by batch

8. **Rename by AST, not text,** wherever the AST sees the name: rewrite the declaration and every
   reference at the offsets the clang AST gives. Check first whether `clangd`'s rename (over LSP,
   or `clangd --check`) can do it in batch. `clang-rename` was reportedly removed in LLVM 20, so
   verify before relying on it. Macros and names inside macro bodies are renamed textually with
   word-boundary matches and re-grepped to zero. A generated name changes in
   `tools/gen_opcodes.py` or `include/opcodes.tsv`, and then the output is regenerated.
9. **Gate every batch:** `make image MINIMAL_IMAGE=1 RUN_SANITIZERS=1` (or its `podman run` line
   against the existing image), which runs the `-Werror` build, the meson suites including
   `dispatch_check` and `designated_init_check`, and the regression suite under UBSan and ASan. Add
   `tools/gen_opcodes.py --check`. **The goldens must be byte-identical**, because identifiers never
   reach the output. Also confirm the ports' parity tests still pass (they compare against the C
   engine's behaviour, not its names).
10. **Add each batch commit to `.git-blame-ignore-revs`**, since a pure rename buries blame.
11. **Docs move with the names:** `CLAUDE.md`, `tasks/reference/*` and the man page wherever they
    cite a renamed identifier (`git grep -w` each old name over the docs).

### Afterwards

12. **Harvest the glossary** into `tasks/reference/spelled-out-names-glossary.md`: each abbreviated
    word and its spelling, the architecture terms with their manual names, and each exemption with
    its reason. The port tasks and any later C code read it.
13. **Harvest the method into the shared layer**, so the maintainer can ask for the same sweep in
    another repo in one line (request, William Emerison Six <billsix@gmail.com>, 2026-11-14). This
    happens after this task and its port follow-ups, so the method is written from what actually
    worked. The likely shape is a language-agnostic reference doc in
    github.com/billsix/runClaudeInContainer `tasks/reference/` (mirrored, with its baked copy, in
    runCrushInContainer) plus a command that scaffolds this task for the current repo. The
    maintainer decides that shape at the time. The doc must make a new task **check the repo's
    situation before planning**. The maintainer named the first example, and this task met the
    rest:
    - **Upstream merges:** does the repo take merges from an upstream it forked (an upstream remote,
      vendored or imported code)? Renames make every later merge conflict, so such code is left
      alone or the cost is surfaced first. This repo has no upstream SPIM remote, so there is no
      cost here.
    - **Generated code:** rename in the generator or its input, never in the output.
    - **Externally defined names**, and text the program prints or reads, are exempt, so goldens
      stay byte-identical.
    - **Mirrors:** ports, bindings or a second implementation that share the names need follow-up
      tasks fed by the glossary.
    - **Code held to another contract:** teaching code in a restricted subset, or code meant to go
      back upstream as patches, is out of scope.
    - **Consumers:** a renamed public name in a library others pin is a breaking change, so it needs
      a version bump and a changelog entry. This repo exports no library API.
    - **Mechanics:** rename by AST or LSP rather than by text where the language allows it, batch
      by word, add the commits to `.git-blame-ignore-revs`, and reflow comments by hand.

## Phase 1 results (2026-11-14)

The inventory is in `tasks/adhoc/spelled-out-names-c-core/data/`. It stays until the port
follow-ups are done, because they reuse it. To regenerate the first three files, run
`tasks/adhoc/spelled-out-names-c-core/discover.sh BUILDDIR` (any configured meson build; it needs
clang and hunspell). A gcc-configured build gives byte-identical output. `glossary.tsv` is
written by hand.

- `names.tsv`: 4,133 declarations (1,812 distinct names), from clang's AST of the 103 translation
  units. Columns: kind, name, path, line.
- `macros.tsv`: 125 `#define` names, from `git grep`.
- `words.tsv`: 922 distinct words. Columns: word, whether hunspell's en_US dictionary has it,
  distinct names containing it, identifier uses in `src/` and `include/`, kinds, example names,
  and `keyword_only`. 369 words are not in the dictionary.
- `glossary.tsv`: **the research result**, 253 rows. Columns: word, meaning, proposed spelling,
  category, and source or note. The categories:
  - `spell-out` (148): a shortened word or initialism, such as `addr`, `reg`, `inst`, `imm`,
    `expr`, `tok`, `buf`, `seg` and `ss` (string stream).
  - `architecture` (54): a MIPS manual term, spelled out with the manual's name kept in a comment.
    These are the instruction fields `rs`/`rt`/`rd`/`shamt`/`funct`/`fs`/`ft`/`fd`/`fr`, plus
    `gpr`, `pc`, `epc`, `fcsr` and `fir`. Then come the Status/Cause/Config bit fields (`EXL`,
    `IE`, `IM`, `UM`, `CU`, `BD`, `IP`, `BE`, `AT`, `AR`, `MT`) and the exception codes (`AdEL`,
    `AdES`, `IBE`, `DBE`, `RI`, `CpU`, `Ov`, `Tr`, `Sys`, `Bp`).
  - `keyword` (14): assembly text a student types, either a directive (`.globl`, `.asciiz`,
    `.ktext`) or a register name (`$sp`, `$gp`). Kept.
  - `familiar` (15), `external` (6), `math` (2) and `not-abbreviation` (5): kept, each with its
    reason.
  - A few words have more than one meaning. They carry a split category and are decided per name:
    `fd` (the floating-point destination field, or a POSIX file descriptor), `fp` (floating point,
    `$fp`, or a `FILE*`), and `op` (opcode or operation).
- **207 words occur only inside the generated assembler-keyword tokens** (`TOK_ADDIU_OPCODE`,
  `TOK_LI_PSEUDO_OP`, `TOK_ASCIIZ_DIRECTIVE`). They are the instruction mnemonics and directives,
  which a student types. So the keyword part is kept and only `TOK` is spelled out
  (`TOKEN_ADDIU_OPCODE`), in `tools/gen_opcodes.py`.
- **How the candidates were found:** a word was a candidate if hunspell rejected it. A hand review
  also covered every dictionary word of five letters or fewer that occurs outside the keyword
  tokens, because hunspell accepts `reg`, `inst`, `val`, `op`, `rt`, `rd`, `enc`, `cur`, `err` and
  most single letters. Longer dictionary words were taken as real words.
- **The meanings** come from the declarations and their comments, and each row cites a file. The
  CP0 and FPU field meanings were checked against NetBSD's `mipsNN.h` header and the register
  descriptions in the MIPS32 manuals.
- **Not yet in the glossary: names that are only one or two letters long.** The most common are
  `rt` (96 declarations), `rs` (81), `op` (71), `i` (43), `ss` (35), `n` (35) and `s` (28). The
  architecture ones follow the glossary. The rest (loop indices, `n`, `s`, `p`) depend on scope,
  so phase 2 decides each one from `names.tsv` under the tight-scope exemption.
- The scripts are `discover.sh`, `list_names.py`, `split_words.py` and `classify_words.py`. They
  are candidates for promotion to a shared tool at the end (step 13).

### Sources for the architecture meanings

The online sources checked on 2026-11-14 for the `architecture` rows of `glossary.tsv`:

- CP0 Config fields `MT`, `AR` and `AT`, with their bit positions and encodings: NetBSD's
  `mipsNN.h`, which mirrors the MIPS32 privileged-resource spec,
  <https://nxr.netbsd.org/xref/src/sys/arch/mips/include/mipsNN.h?r=1.14>.
- Cause `ExcCode` names and numbers (`Int`, `Mod`, `TLBL`, `TLBS`, `AdEL`, `AdES`, `IBE`, `DBE`,
  `Sys`, `Bp`, `RI`, `CpU`, `Ov`, `Tr`, `FPE`):
  - Imperial College lecture notes on MIPS exceptions,
    <https://www.doc.ic.ac.uk/~wl/teachlocal/arch/HP05/exceptions.pdf>.
  - The Rust `mips` crate's Cause register, which is also the only source found for `MDMX` as
    code 22, <https://docs.rs/crate/mips/latest/source/src/registers/cp0/cause.rs>.
- FCSR condition-code (`FCC`) layout, and the `ND`/`TF` bits of the floating-point branches: the
  GDB MIPS simulator's `cp1.h`,
  <https://gnu.googlesource.com/binutils-gdb/+/2e917d2873a4ebffb1b7e8a8c043af65dfb5b708/sim/mips/cp1.h>.

Not confirmed online:
- **Config `BE`** (big-endian, bit 15) comes from general knowledge of the spec.
- **`ND`/`TF`** were found only in simulator code (gem5's MIPS decoder, GDB's `cp1.h`), not in the
  manual text.

The authoritative references for both are the MIPS32 Architecture for Programmers volumes: Volume
II for the BC1F/BC1T and MADD.fmt pages, and Volume III for the CP0 registers. Check them before
renaming the `be`, `nd` and `tf` rows.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-14)

These settle the policy cases. The `glossary.tsv` rows they affect cite them as "decision N".

1. **The CP0 prefix is spelled out**, with the manual's name in a comment:
   `CP0_Status` becomes `coprocessor_0_status /* CP0 Status */`, as with `gpr`.
2. **Register names are spelled out by meaning**, with the name a student types in a comment:
   `REG_SP` becomes `stack_pointer_register /* $sp */`. The same goes for `REG_GP`, `REG_A0` to
   `REG_A3`, `REG_FA0`, `REG_RES`/`REG_FRES`, and `HI`/`LO`. This follows decision 1.
3. **These familiar acronyms stay as they are:** `AST`, `ASCII`, `ASAN`, `EOF`, `IO`, `JSON`,
   `MIPS`, `SPIM`, `REPL`, `ms` and `syscall`.
4. **The allocation wrappers are renamed**: `xmalloc` becomes `allocate_or_exit`, `xrealloc`
   becomes `reallocate_or_exit`, and `zmalloc` becomes `allocate_zeroed_or_exit`. A comment beside
   each definition gives the old GNU-style name (`/* xmalloc in GNU style */`), so a reader who
   knows the idiom can find it.
5. **The math-style names in `unsigned_multiply` (`src/run.c`) stay**: the 16-bit halves `a`,
   `b`, `c` and `d`, their products `ac`, `ad`, `cb` and `bd`, and `mid`.

## Open questions

None. Phase 2 still ends with a review of the rename table (step 7).
