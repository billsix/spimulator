# C23 modernization pass 4: `const` wherever it can go in `src/` and `include/`

**Status:** proposed — needs go-ahead
**Priority:** 5
**Difficulty:** 4
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Follows:** `tasks/archive/2026/11/10/c23-modernization-pass3.md`

## BLUF

Add `const` to every binding in the simulator core that is never changed, following the shared C
coding standard's `const` rules: about 460 scalar locals, 120 pointer locals that are never
repointed, 44 pointer parameters that are never written through, up to 88 more once the functions
they are passed to take `const`, and one file-scope table. Behavior must not change. Done means every
candidate in the worklist is either `const` or listed with the reason it cannot be, and the image gate
passes: both compilers at `-Werror`, the suites, and UBSan and ASan.

## Context

- **The rules** are in the `c-coding-standard.md` reference doc of github.com/billsix/runClaudeInContainer
  (section "`const` by default"):
  - pointee-`const` on pointer parameters, but not top-level `const` on parameters;
  - top-level `const` on locals, including never-repointed pointer locals (`T *const p`);
  - both `const`s on file-scope tables of pointers.
- **Opt-outs**, from that doc and the `c-modernization-sweep.md` reference doc of the same repo:
  - setjmp-live locals (`main`, `top_level`);
  - address-taken or output locals;
  - loop and list cursors;
  - teaching C under `examples/` and `pgu/`, which this project does not modernize (`CLAUDE.md`).
- **Pass 3 left this gap.** It added `const` only to format strings. It deferred const-correct
  string parameters: `DEFAULT_RUN_LOCATION` and `END_OF_TRAP_HANDLER_SYMBOL` stay macros until
  `find_symbol_address` takes a `const char*` (`tasks/archive/2026/11/10/c23-modernization-pass3.md`,
  "Opted out").
- **The worklist** is `tasks/adhoc/c23-const-correctness-pass/data/candidates.txt`, made by
  `tasks/adhoc/c23-const-correctness-pass/const_candidates.py BUILDDIR`.
  - The script walks clang's JSON AST, because clang-tidy's `misc-const-correctness` checks C++
    only and reports nothing on C. `readability-non-const-parameter` found one site.
  - Each line is `file:line: category: function: name (type)`. A line number of `?` means clang
    left the line out because it is the same as the previous node's.
  - The counts are an upper bound; the compiler decides.

  | Category | Count | Change |
  |---|---|---|
  | `local-scalar` | 459 | `const T x = …` |
  | `local-pointer` | 121 | `T *const p = …` |
  | `param-pointee` | 44 | `const T *p` (19 of them `mips_instruction *`) |
  | `param-cascade` | 88 | `const T *p` once each callee takes `const` (21 `char *`, 16 `FILE *`, 13 `mips_instruction *`, 12 `str_stream *`, 12 `imm_expr *`) |

  By file: `parser.c` 239, `explain.c` 149, `run.c` 72, `scanner.c` 39, `instruction.c` 39,
  `spim.c` 31, `ast.c` 30, `memory.c` 29, and fewer than 20 in each of the rest.
- **What the script does not cover:**
  - `static inline` functions in `include/`;
  - file-scope objects never written after initialization;
  - prototypes in `include/*.h`, which must match their definitions. Twenty-four prototypes in
    `include/` take or return a non-const `char*`.
- **File-scope table:** `src/spim.c` `spim_commands[]` is `static const char* []`. It needs the
  second `const` to move to `.rodata`.
- **The ports are unaffected.** `const` changes no behavior, so `port/` needs no change and the port
  suites need not be run apart from the image gate.

## Plan

1. One commit per category, smallest risk first:
   - the file-scope table and file-scope objects;
   - `param-pointee` with the matching header prototypes;
   - `param-cascade`, callees first, so `find_symbol_address(const char*)` lets the two string
     macros of pass 3 become `constexpr char[]`;
   - `local-pointer`;
   - `local-scalar`, file by file.
2. Leave `FILE *` parameters non-const, because stdio takes `FILE *`. Record this opt-out, and any
   other site that cannot be `const`, in the done-record with its reason.
3. Verify each commit:
   - local gcc and clang `-Werror` builds under `-Wconversion -Wsign-conversion`, and `meson test`.
   - A no-behavior-change check: the `.text` of each object at `-O2` is byte-identical before and
     after. A `const` local compiles to the same code, so any difference points at a mistake.
4. At the end, run `make image` (sanitizers on) and re-run the script. Each remaining candidate must
   be listed with its reason.
5. The script is a one-shot: `git rm` `tasks/adhoc/c23-const-correctness-pass/` in the archive
   commit.
