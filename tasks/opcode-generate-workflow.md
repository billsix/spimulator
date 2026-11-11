# Make the documented opcode-change workflow (`make generate`) work

**Status:** proposed — needs go-ahead
**Priority:** 3
**Difficulty:** 3
**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Source:** tasks/review/2026/11/11/repo-review.md §2.1 F1

## BLUF

The docs say: edit `include/opcodes.tsv`, then `make generate`. That can't work. `generate:` depends on
`image`, and the image build runs `python3 tools/gen_opcodes.py --check` on the edited `.tsv` against the
old committed tables, so it fails before the generator runs. Done means the documented two steps work
on a fresh edit, and the README, `CLAUDE.md` and `tasks/reference/opcode-table-generator.md` say
what actually happens, including a host fallback.

## Context

- `Makefile:110` `generate: image`; the recipe runs `python3 tools/gen_opcodes.py` in the image.
- `Dockerfile:113` `RUN cd ${SPIM_SRC_DIR} && python3 tools/gen_opcodes.py --check`, after the
  `COPY` of `include/`, `tools/`, `port/` and `tree-sitter/scripts/keyword-lists.js`.
- `make image`'s recipe (`Makefile:47-56`) passes no way to skip the check.
- The docs: `README.md:69-72` ("Changing the opcode table"), `CLAUDE.md` (Conventions, "make
  generate"), `tasks/reference/opcode-table-generator.md:174`.
- `format:` and `shell:` also depend on `image`, so after a `.tsv` edit they fail the same way.
- The generator needs only Python 3 and `clang-format` (for the C output). The 2026-11-11 table
  changes ran `python3 tools/gen_opcodes.py` directly, on a host with Fedora 45's clang-format, and
  `--check` then passed in the image.

## Plan

1. Pick a mechanism (see question 1). Candidates:
   - a `generate` target that runs on the existing image (`generate: ` with no `image` prerequisite,
     failing with a hint if there is no image yet);
   - a small multi-stage `generator` target (`podman build --target generator`) whose stage has
     Python and clang-format but stops before the check.
2. Document the host fallback: `python3 tools/gen_opcodes.py`, which needs a `clang-format` of the
   image's major version, or the C output's formatting differs.
3. Prove it: in a scratch clone, add a dummy row to `include/opcodes.tsv`, run the documented steps, and
   confirm `make image` then passes. Revert the dummy row.
4. Update `README.md`, `CLAUDE.md` and `tasks/reference/opcode-table-generator.md`.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. `generate` runs on the existing image (option (a)): it no longer depends on `image`, and fails with a hint when no image exists yet.
