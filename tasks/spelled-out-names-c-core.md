# Spell out the abbreviated names in the C core (`src/`, `include/`)

**Created:** 2026-11-14 (William Emerison Six <billsix@gmail.com>)
**Done:** 2026-11-14, on branch `spelled-out-names-c-core`

## Summary

Every abbreviated or acronym name in `src/` and `include/` was spelled out: variables, parameters,
struct and union fields, functions, enum constants, typedefs, tags and macros. That came to 1,237
renames, for example `gpr` to `general_purpose_registers` and `imm_expr` to
`immediate_expression`. Architecture names carry the MIPS manual's name, or the register name a
student types, in a comment at the definition.

The machine code is unchanged: `.text` and `.rodata` of all 17 core objects are byte-identical
to before. All 349 meson tests pass: regression, the examples, and the Python, Rust and Haskell
ports. The vocabulary, and what stayed short and why, is in
`tasks/reference/spelled-out-names-glossary.md`.

## Request (William Emerison Six <billsix@gmail.com>, 2026-11-14)

"Sweep through all the code in include and src, and find all of the acronyms for stuff, variable
names, struct names, all names that could be written out more explicitly, to help the new student.
As an example, gpr, means 'general_purpose_register'. Perhaps these were useful back in the day,
when people had to type code all in themselves, no autocomplete, no llms. That day is not today."
The request included investigating which names are acronyms or abbreviations, searching online for
any whose meaning the context didn't give, and planning the fix systematically.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-14)

1. **The CP0 prefix is spelled out**, with the manual's name in a comment:
   `CP0_Status` became `coprocessor_0_status`, under a `/* CP0 Status register: */` header.
2. **Register names are spelled out by meaning**, with the name a student types in a comment:
   `REG_SP` became `STACK_POINTER_REGISTER /* $sp */`. The same applies to `REG_GP`,
   `REG_A0`..`REG_A3`, `REG_FA0`, `REG_RES`/`REG_FRES` and `HI`/`LO`.
3. **Familiar acronyms stayed**: `AST`, `ASCII`, `ASAN`, `EOF`, `IO`, `JSON`, `MIPS`, `SPIM`,
   `REPL`, `ms` and `syscall`.
4. **The allocation wrappers were renamed**: `xmalloc`, `xrealloc` and `zmalloc` became
   `allocate_or_exit`, `reallocate_or_exit` and `allocate_zeroed_or_exit`, with the GNU-style
   names in a comment above the prototypes.
5. **The math-style names of `unsigned_multiply`** (`src/run.c`) stayed: `a`, `b`, `c`, `d`,
   `ac`, `ad`, `cb`, `bd` and `mid`.
6. **The assembler's location counters became `..._location`**, not `..._program_counter`
   (`current_data_location`, `next_text_location`). They hold the address where the next datum
   or instruction is placed (`src/data.c`: "Location for next datum in user process"). The CPU's
   program counter is `PROGRAM_COUNTER`, so naming both "program counter" would invite
   confusion. The maintainer asked about it and left the choice to the agent's recommendation.
7. **"Do the work" replaced the planned stop for review.** The task's plan had a stop to review
   the rename table before any rename. The maintainer's go-ahead ("do the work for the c
   renaming of stuff, commiting incrementally, archive when done") replaced it. The table's
   policy cases were settled by decisions 1 to 5.

## Phase 1: the inventory

- clang's JSON AST of the 103 translation units listed 4,133 declarations, with 1,812 distinct
  names; `git grep` listed 125 macros. Splitting them gave 922 words, of which hunspell's en_US
  dictionary rejected 369.
- 207 of the words occur only inside the generated assembler-keyword tokens (`TOK_ADDIU_OPCODE`),
  which spell what a student types, so they stayed.
- A hand review of every short dictionary word found the abbreviations hunspell accepts: `reg`,
  `inst`, `val`, `op`, `rt`, `rd`, `enc`, `cur`, `err`.
- The research result is `tasks/adhoc/spelled-out-names-c-core/data/glossary.tsv`: 253 words,
  each with its meaning, spelling, category and source.

## How the rename was done

- **The rename table.** `rename_map.py` derived `data/renames.tsv` from the glossary: words
  spelled out by rule, keyword tokens losing only their `TOK` prefix.
  `data/renames.manual.tsv` (341 rows) holds the hand-decided names:
  - words with more than one meaning;
  - mixed-case architecture names;
  - per-declaration renames, for one spelling with different meanings;
  - the collisions the build found.
- **Applying it.** `apply_renames.py` ran in batches by declaration kind.
  - Variables, parameters, fields, functions and enum constants were renamed at the positions in
    clang's AST, so each reference follows its own declaration.
  - Typedefs, tags, macros, designated-initializer field names, and enum constants used as array
    sizes or designators were renamed by token. String and character literals were never
    touched.
- **The safety net was the build.** A missed reference fails to compile, and the core builds with
  `-Werror -Wshadow`, which turned four same-scope collisions into errors.
- **The generator.** `rename_generator_inputs.py` renamed the token and type columns of
  `include/opcodes.tsv`, the tags of `include/opcode-types.tsv`, and the C that
  `tools/gen_opcodes.py` emits.
  - `opcode-types.tsv` gained a `category` column holding the old short category names, so
    `tree-sitter/grammar.js` and its generated parser are unchanged.
  - The generator now runs `ruff` and `rustfmt` on the Python and Rust tables it writes, since the
    longer names broke the hand-built formatting.
- **The ports.** The ports get their opcode tables from the generator, so
  `rename_port_generated_refs.py` renamed the ports' references to the generated names (Haskell
  constructors, Rust constants and variants, Python `OpType` members). The ports' own names are
  the port tasks' work.
- **The docs.** `rename_doc_references.py` renamed the identifiers cited in the open docs' code
  spans, leaving prose alone.
- **The comments.** `annotate_manual_names.py` added the manual or typed name beside each
  architecture definition (decisions 1, 2 and 4).

## Commits

| Commit | What |
| --- | --- |
| `f08bd1e` | phase 1 inventory |
| `7c957d0` | the policy decisions and online sources |
| `7ed399f` | the rename table and the apply tool |
| `dfcc03d` | batch 1: locals and parameters |
| `3db0b22` | batch 2: struct and union fields |
| `c20664a` | batch 3: functions |
| `443122a` | batch 4: globals, constants, types, macros, the generator and the ports' generated-name references |
| `804f8ac` | clang-format |
| `4e7f939` | `.git-blame-ignore-revs` |
| `4b52fe0` | the manual-name comments |

## Verification

- `compare_code.sh` compared the `.text` and `.rodata` bytes of every core object with a
  baseline saved before batch 1. They were identical after every batch, after the format pass and
  after the comments.
- **The regression suite** passed after every batch. The full meson run (349 tests) passed after
  batch 4.
  - The first full run showed the Rust port not compiling. Its generated table also defines a
    named constant per keyword, which the first version of the port codemod missed.
  - The Python port's lint also failed on long lines. Both were fixed before the commit.
- **Code the build never compiles** (the `SPIM_BIGENDIAN` branches, the no-libedit input path)
  was renamed by hand from `apply_renames.py --residue`. It was compile-checked with
  `-DSPIM_BIGENDIAN`, and without `HAVE_LIBEDIT`. The only residue left is the kept `cb` of
  `unsigned_multiply`.
- `tools/gen_opcodes.py --check`, `dispatch_check` and `designated_init_check` pass.

## Exceptions and hand fixes

- **Hand fixes:**
  - Ten designated initializers whose field name means two things (`.value.i`, the explain
    snapshot's `.hi`/`.lo`).
  - A value-holding `reg` in `src/run.c` that the generic rule had named `register_number`; it
    became `register_value`.
  - `print_register_from_string`'s parameter became `register_name`.
- **Inherited comment orphans** (a lone word on the last line of a wrapped comment, all present
  before this task) were left as they are; the one this task created was fixed.
- **Lowercase prose words in comments** (`inst`, `addr`) were not renamed. The comment pass only
  renamed words that are clearly identifiers.
- **Out of scope:** `examples/` and `pgu/` (teaching C in the course compiler's subset), the
  tree-sitter grammar, and the ports' own names.

## Sources for the architecture meanings

The online sources checked on 2026-11-14 for the `architecture` rows of `glossary.tsv`:

- CP0 Config fields `MT`, `AR` and `AT`, with their bit positions and encodings: NetBSD's
  `mipsNN.h`, which mirrors the MIPS32 privileged-resource spec,
  <https://nxr.netbsd.org/xref/src/sys/arch/mips/include/mipsNN.h?r=1.14>.
- Cause `ExcCode` names and numbers:
  - Imperial College lecture notes on MIPS exceptions,
    <https://www.doc.ic.ac.uk/~wl/teachlocal/arch/HP05/exceptions.pdf>.
  - The Rust `mips` crate's Cause register, the only source found for `MDMX` as code 22,
    <https://docs.rs/crate/mips/latest/source/src/registers/cp0/cause.rs>.
- The FCSR condition-code (`FCC`) layout, and the `ND`/`TF` bits of the floating-point branches:
  the GDB MIPS simulator's `cp1.h`,
  <https://gnu.googlesource.com/binutils-gdb/+/2e917d2873a4ebffb1b7e8a8c043af65dfb5b708/sim/mips/cp1.h>.
- Not confirmed online:
  - The Config `BE` bit (bit 15, big-endian) comes from general knowledge of the spec.
  - `ND`/`TF` were found only in simulator code. The MIPS32 Architecture for Programmers volumes
    (II for BC1F/BC1T and MADD.fmt, III for CP0) are the authority for both.

## Follow-ups

- The port tasks: `tasks/spelled-out-names-python-port.md`,
  `tasks/spelled-out-names-haskell-port.md` and `tasks/spelled-out-names-rust-port.md`. They read the glossary doc and
  `tasks/adhoc/spelled-out-names-c-core/data/renames.tsv`, so that directory stays until the last
  port task is archived; that task `git rm`s it.
- **Harvesting the method into the shared layer.** The maintainer asked that the sweep be
  something to request for another repo in one line, through
  github.com/billsix/runClaudeInContainer (and runCrushInContainer). This happens after the port
  tasks, so the method is written from what worked. The doc must make a new task check the
  repo's situation before planning:
  - **Upstream merges** (the maintainer's first example): does the repo take merges from an
    upstream it forked? Renames make every later merge conflict. This repo takes none.
  - **Generated code:** rename the generator's input, never the output. Also check what else the
    generator feeds (here the three ports' tables and the tree-sitter keyword lists).
  - **Code the build doesn't compile:** `#if` branches and other configurations; list them from
    the residue and compile-check each.
  - **Exemptions:** names defined outside the code; text the program prints or reads, so the
    goldens stay byte-identical; what the user types (mnemonics, register names).
  - **Mirrors:** ports or bindings that share the names.
  - **Code held to another contract:** a teaching subset, or code meant to go upstream as patches.
  - **Consumers:** a renamed public name of a pinned library is a breaking change.
  - **Mechanics:**
    - rename by AST or LSP position;
    - batch by declaration kind, building and testing each batch;
    - prove a pure rename by comparing code bytes;
    - add the commits to `.git-blame-ignore-revs`;
    - format once at the end;
    - fix comment orphans by hand.
