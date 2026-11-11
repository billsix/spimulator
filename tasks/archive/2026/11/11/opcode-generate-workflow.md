# Make the documented opcode-change workflow (`make generate`) work

**Created:** 2026-11-11 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-11
**Branch:** `review-2026-11-11-batch`
**Source:** tasks/review/2026/11/11/repo-review.md §2.1 F1

## Outcome

`make generate` no longer depends on `make image`. It runs `tools/gen_opcodes.py` on the existing
image and rewrites the tables in the bind-mounted tree. If there is no image yet, it exits 2 with a
hint (build one first, or run the generator on the host).

Before, `generate: image` rebuilt the image first. The image build runs `gen_opcodes.py --check`
(`Dockerfile`), so after a `.tsv` edit it failed before the generator could run, and the documented
"edit `opcodes.tsv`, then `make generate`" could not work. `make image`, `make shell` and
`make format` still fail between the edit and `make generate`, and the docs now say so.

`README.md` ("Changing the opcode table"), `CLAUDE.md` and `tasks/reference/opcode-table-generator.md`
describe the flow and the host fallback: `python3 tools/gen_opcodes.py`, which needs a
`clang-format` of the image's major version.

## Decisions (William Emerison Six <billsix@gmail.com>, 2026-11-11)

1. `generate` runs on the existing image (option (a)), not a separate build stage.

## Verification

In a scratch clone, the `ssnop` row was deleted from `include/opcodes.tsv`:
- `gen_opcodes.py --check` then failed (exit 1);
- `make generate` succeeded and rewrote the eight generated files;
- `--check` then passed (exit 0).

`make generate CONTAINER_NAME=no-such-image` prints the hint and exits 2.
