# Parser / scanner allocation lifetime cleanup

## Status — Option A LANDED and verified (2026-08-03)

**Priority:** 4
**Difficulty:** 6

Filed during the post-C23-sweep ASan/valgrind audit (May 2026).
Pre-existing.

**Resolution: Option A (fix the specific drop sites).**  The leak is
gone — valgrind now reports `definitely lost: 0 bytes in 0 blocks` on
`tt.argv.s` (was 224/26), `tt.explain.s` (was 351/44), and a
broad-coverage input.  All 32 meson tests pass; the ASan address gate
passes 25/25 with no double-free.  See "Resolution — the actual
defects" below.

**Option C (delete the non-default `PARSE_DIRECT` codepath) is now
DECOUPLED from the leak** — it no longer *has* to happen to close the
valgrind finding.  It remains a reasonable forward-consistency cleanup
(one ownership model instead of two) but that is a design decision for
Bill, not forced by memory hygiene.  NB the ordering note at the bottom
still holds: `PARSE_DIRECT` still exists, so `ast-column-tracking.md` is
*not* yet unblocked by a codepath deletion.

## Resolution — the actual defects (Option A, 2026-08-03)

The ownership model is: `make_imm_expr(offs, sym, ...)` and
`make_addr_expr(offs, sym, reg)` both only *read* `sym` (via
`lookup_label`, storing the resulting `label*`); neither retains nor
frees the string.  So the caller always owns `sym`.  The leaks were the
paths that violated that contract, plus two orphaned `imm_expr` nodes:

1. **`make_addr_expr` (`instruction.c`)** — `strdup(sym)`'d the symbol
   before handing it to `make_imm_expr`, which promptly dropped the
   pointer.  That copy was orphaned on **every** call with a symbol —
   the single largest source.  Fix: pass `sym` straight through, no
   copy.
2. **`parse_label` (`parser.c`)** — read the TOK_ID string, built the
   `imm_expr`, never freed it; the comment wrongly claimed
   `make_imm_expr` "stores it".  Fix: `free(sym)`.
3. **`parse_imm32` (`parser.c`)** — the bare-`TOK_ID` return path had
   the same wrong "ownership passes to make_imm_expr" comment and leaked
   `sym`.  Fix: `free(sym)`.
4. **`parse_address` (`parser.c`)** — the `ABS_ADDR '+' ID` path did not
   free `sym` (the sibling `TOK_ID` path did).  Fix: `free(sym)`.
5. **`sync_to_nl` (`parser.c`)** — discarded whole lines (ignored
   directives like `.set noreorder`, error recovery) without freeing the
   TOK_ID/TOK_STR strings it advanced past.  Fix: free `scan_value.p`
   when the discarded token is a TOK_ID or TOK_STR.
6. **`i_type_inst_full_word` (`instruction.c`)** — the out-of-range
   load/store path did `lower_bits_of_expr(const_imm_expr(low))`;
   `lower_bits_of_expr` *copies*, so the `const_imm_expr` node was
   orphaned.  Fix: capture the intermediate and free it.
7. **`li.d` / `li.s` pseudo-ops (`parser.c`)** — passed inline
   `const_imm_expr(...)` nodes to `emit_i` (which keeps caller
   ownership), so the nodes leaked.  Fix: use `emit_i_free` (transfers +
   frees), matching the idiom used everywhere else in the file.  (The FP
   literal `scan_value.p` here is *not* a leak — it points at a `static
   double` in the scanner.)

Files changed: `src/instruction.c`, `src/parser.c` (github.com/billsix/spimulator).

## Symptom

Valgrind on `./builddir/spimulator -f tests/tt.argv.s ...`:

```
definitely lost: 224 bytes in 26 blocks
indirectly lost: 0 bytes in 0 blocks
ERROR SUMMARY: 0 errors
```

Larger programs leak proportionally more (e.g. `tt.explain.s`
at 220 lines leaks 351 bytes / 44 blocks).  ASan with
`detect_leaks=1` reports the same.

The leak count scales with program size and stays constant
across runs of the same program — these are one-shot
"allocate, use, never bother freeing" patterns, not growing
leaks.

## Sites

Three call stacks account for all reported leaks:

1. **`scan_identifier` → `str_copy`** (`src/scanner.c:431`)
   Each identifier (label name, register name, mnemonic) that
   the scanner emits gets `str_copy`'d into a heap allocation
   the token owns.  Some of these get adopted by symbol-table
   entries (and live for the program's lifetime — which is
   fine).  But identifiers that are immediately consumed by
   the parser as transient lookup keys never become anyone's
   responsibility to free.

2. **`make_addr_expr` → `str_copy`** (`src/inst.c:1035`)
   The symbolic-name part of an `addr_expr` (`label+offset(reg)`)
   is `str_copy`'d.  The expr itself usually winds up owned by
   an `instruction*` and lives forever; the symbol name inside
   it is what's leaking when the address parsing path discards
   intermediate `addr_expr` nodes.

3. **`make_imm_expr`** (`src/inst.c:915`) and
   **`const_imm_expr`** (`src/inst.c:960`)
   The `imm_expr` node itself.  Same story — usually adopted
   by an `instruction*`, but the parser sometimes builds one
   speculatively (e.g. in `i_type_inst_full_word` when emitting
   a `lui + ori` pair for an out-of-range immediate) and the
   intermediate is dropped.

## Root cause

The parser's ownership model treats heap-allocated parse
artifacts (identifiers, immediate expressions, address
expressions) as "transferred to the next layer."  Most paths
do transfer (the inst tables and the symbol table
hold the long-lived references).  A few paths — error
recovery, pseudo-op expansion that picks an alternate form,
speculative-then-rejected emit attempts — drop the
intermediates on the floor without freeing.

The leak is bounded: each parse path leaks O(1) per source
line, not O(N).  And the OS reclaims everything at exit.  But
it's correctness debt that ASan/valgrind flag and that would
matter if spim ever ran as a long-lived REPL processing many
files.

## Three possible fixes

### Option A — fix the specific drop sites

Audit each leak stack, identify which intermediates aren't
adopted, free them at the drop site.  Tight scope; preserves
the existing ownership model.

Drawbacks: ad-hoc, and easy for a future code change to
re-introduce a drop site that was missed.

Estimated effort: 1-2 days for the existing leak sites, plus
ongoing maintenance.

### Option B — arena allocator for parse-transient state

Wrap parser allocations in a per-`parse_file` arena that gets
reset / freed at end of file.  Allocations that need to outlive
the parse (instructions in the text segment, label entries in
the symbol table) get copied out.

Drawbacks: invasive — touches every `xmalloc` site inside the
parser.  Changes ownership semantics, which means every
"transfer ownership" comment needs revisiting.  Requires
distinguishing transient from persistent at every alloc.

Estimated effort: 1 week with careful audit.

### Option C — AST as the lifetime owner (lean on existing work)

The AST migration (already merged to master) gives `ast_node`
ownership of identifier strings and `imm_expr`/`addr_expr`
nodes.  `ast_free` recursively frees a tree.  The leak only
matters in `PARSE_DIRECT` mode, where there's no tree to own
things.  (Status refresh 2026-07-07: `PARSE_AST` is now the
**default** — see `include/ast.h` / `include/parser.h` — so
Option C has shrunk to "delete the non-default PARSE_DIRECT
codepath," which was already the stated future direction.)

The cleanest long-term fix: make `PARSE_AST` the only mode,
delete the `PARSE_DIRECT` codepath, and let `ast_free` clean
up at end of parse.

Drawbacks: removes the SDT codepath that the AST work
deliberately kept alive as a parallel reference.  Need
confidence that `PARSE_AST` is byte-identical (the
`ast_parity_all` test asserts this on 20 representative
programs — good baseline but not proof).

Estimated effort: 2-3 days.  Most of the work is the
PARSE_DIRECT removal, which is already a stated future
direction in the AST migration notes.

## Recommendation

Option C is the right long-term move and is already a follow-on
direction from the AST work — see
[`archive/2026/05/22/c23-modernization.md`](archive/2026/05/22/c23-modernization.md)
or [`HANDOFF-2026-05-22.md`](archive/2026/05/23/HANDOFF-2026-05-22.md) for the
existing PARSE_AST status.

Option A is the right short-term move if a hostile valgrind
verdict is needed in the next CI run.

Option B is over-engineered for the current shape of spim.

## Verification

1. `valgrind --leak-check=full ./builddir/spimulator -f
   tests/tt.argv.s alpha beta gamma`
2. Confirm `definitely lost: 0 bytes in 0 blocks` (or close to
   it — `in use at exit` will still be ~1MB of intentionally-
   never-freed static buffers, which valgrind reports as
   "still reachable", not leaked).
3. ASan with `detect_leaks=1` — no "Direct leak" entries from
   parser/scanner sites.
4. 22/22 regression tests pass.

## Why this matters

Not currently causing user-visible breakage.  Spim is a
short-lived process that the OS cleans up; the leaks don't
grow with runtime.

The fix matters for three reasons:
1. **Hygiene** — closes the valgrind/ASan finding that future
   contributors will see and have to triage.
2. **REPL longevity** — if spim ever runs as a long-lived
   process (e.g. embedded in an IDE), parsing many files in
   sequence would slowly leak.  Currently bounded; remove the
   bound by removing the leak.
3. **Forward consistency** — the AST path already has
   well-defined ownership.  Keeping the SDT path's
   leak-on-drop pattern makes the codebase carry two ownership
   models, which is the kind of thing that bites a future
   refactor.

## Effort

- Option A: 1-2 days
- Option B: 1 week
- Option C: 2-3 days (recommended)

## Ordering

Do this **before** [`ast-column-tracking.md`](ast-column-tracking.md): Option
C deletes the PARSE_DIRECT codepath, and column tracking touches the same
scanner/AST-constructor surface — landing the deletion first means the column
plumbing is written once, against one parse mode.
