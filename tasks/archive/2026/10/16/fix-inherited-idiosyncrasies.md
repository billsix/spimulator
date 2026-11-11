# Folded the inherited-idiosyncrasy correctness fixes into the C23 modernization pass

**Created:** 2026-08-27 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-10-16 (superseded)

This task was opened to apply the mechanical correctness fixes found by the audit recorded in
`tasks/reference/inherited-idiosyncrasies.md`. Its scope was the self-justifying correctness fixes
only; cosmetic renames and demo-name fixes were excluded because they needed maintainer direction.

None of its five plan items was done under this task. On 2026-10-16 all four correctness fixes (and
their verification) were done instead as Tier 1 of
`tasks/archive/2026/10/16/c23-modernization-pass2.md`, and this doc was archived alongside it. The
superseded items were:

- The HIGH bug in `fatal_error` (`src/spim.c`): a `fmt = va_arg(args, char*);` line overwrote the
  format string and `va_end(args)` was missing, which broke all 14 error paths (UB, segfault or
  wrong output). It was planned as a fix on its own, ahead of the rest.
- `src/dump-opcodes.c` (since removed from the tree): `main(int argc, char** argv)` with unused
  arguments to `int main(void)`.
- `src/spim-utils.c`: `xmalloc(int size)` and `zmalloc(int size)` to take `size_t size`.
- `src/parser.c`: removal of a dead `kernel` parameter.
- Verification of each fix in its own small commit with `meson test` and the `make image` ASan gate.

Comment/code drift and copy-paste asm residue are catalogued in the reference doc but were left out
of this work as lower value and partly needing direction.
