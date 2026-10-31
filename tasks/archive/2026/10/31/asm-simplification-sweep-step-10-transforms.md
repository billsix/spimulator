# Step 10 — asm simplification rollout: transforms/

**Part of:** `asm-simplification-sweep.md`
**Depends on:** Step 1 (the pilot + `tasks/reference/mips-asm-teaching-style.md`)
**Next:** none — this is the final rollout topic.

**Status:** done + archived 2026-10-31 — 14 demos converted (wc was the pilot's), all
byte-identical to pre-change output; 10 checker-anchored, 4 gated by equivalence.
**Priority:** 5
**Difficulty:** 6

## BLUF

Convert the 14 non-pilot `transforms/` demos to the stack-visible style. 15 `.asm`; `wc` was
done in the pilot. None exempt. "Done" = each byte-identical to its pre-change output; the step
archives. This completes the whole sweep.

## The 14 files

Byte/line stdin utilities. Pattern: each demo's persistent C locals (fd, the argv path/flags,
counters, column/line state) move onto `-N($fp)` slots; large buffers / tables stay in `.data`
or the sbrk heap; shared display/leaf helpers stay register-based.

- **rot13, tr** — the byte `ch` on the stack (no frame otherwise).
- **rev, uniq** — fd/argv1/(len) on the stack; line buffers stay `.data`; uniq's compare helper
  `emit_if_new` works on the `.data` line globals (leaf).
- **head-1-count, strings** — the counter/`run`/`ch` on the stack; `pending` stays `.data`.
- **nl** — fd/argv1/line_num/line_started on the stack; `print_padded` is a display helper.
- **tac** — fd/buf(sbrk base)/total/capacity/argv1/i on the stack; bytes in the sbrk heap.
- **head, tail** — the full `[-n N] [FILE|-]` locals on the stack; `str_eq`/`atoi` leaf. tail's
  ring index/total/line-len move to the stack, which also cleans up its old emit-phase `$s*`
  juggling. ring/ringLen/curLine stay `.data`.
- **expand, cut, od, base64** — col/range/offset/trio-bytes on the stack. Notable: **base64's
  three input bytes b0/b1/b2 move onto main's frame** (so the old "emit_char must avoid
  $t0-$t2" constraint is gone) and its wrap counter `out_col` becomes a `.data` global shared
  with emit_char; **cut** passes its `lo`/`hi`/`len` to `emit_range` as arguments rather than
  via shared `$s*`.

### `#CANONICAL` coverage

- Added (with new per-demo `.input` files where needed): rot13, tr, rev, uniq, base64, cut,
  tac, tail (new inputs); head-1-count, head, strings (existing inputs). All green.
- SKIPPED (noted in-file; gated by before/after equivalence): expand (tab→variable spaces),
  nl (leading-number + tab), od (fixed column spacing + trailing offset line).

## Verification (done)

- Full before/after output equivalence across modes/args, all 14 byte-identical (rev/tac
  no-trailing-newline, head/tail -n, cut past-end, base64 padding, od 16-byte row wrap, uniq
  dedup, rot13 self-inverse).
- `tools/check_asm_canonical.py --all`: 47/47 annotated demos PASS (exit 0). The 4 transforms
  with `run-demo.sh` goldens (head-1-count, head, strings, wc) stay green.

## Exempt

None in `transforms/`.
