# Task: print a full program listing before execution

## Goal

Before the first instruction executes (or as a new REPL command),
emit a complete listing of the loaded code: every word in the text
segment, with address, encoded hex, disassembly, and the source line
that produced it. The student gets a static view of the whole program
they're about to step through.

## Why

Today, the only way a student sees what's loaded is to invoke
`step` instruction-by-instruction. They have no way to scroll back
and see the whole layout, no way to predict what's coming. Showing
the listing up-front means:

- The student can match `run 0xADDR` arguments against actual
  loaded addresses.
- Pseudo-op expansion is visible at a glance — they see that
  `la $t7, dst` turned into two real instructions before stepping
  begins.
- Branch targets and jump destinations are inspectable against
  the listing.
- Acts as a baseline reference they can flip back to while
  stepping (in a terminal scrollback or paged view).

## Scope

- Integer and floating-point instructions in the user text segment.
- Both the kernel text segment (if loaded) and the user text segment.
- Each line:

  ```
  [0xADDR]  0xENCODING   DISASSEMBLY                ; LINE: SOURCE
  ```

  Format matches the per-step "Stepped" line so the student sees the
  same shape they'll later see during execution.

Out of scope (for now):
- Data-segment dump (separate concern; spim already has
  `dump_data_seg` / `print_data` for that).
- Pretty-printing labels with their addresses as headers (the
  per-line `; LINE: SOURCE` already encodes label-bearing lines).

## Integration options

There are three plausible hooks; pick one per the trade-off:

1. **REPL command** `listing` (or `code` / `disassemble`). Student
   types `listing` to dump. No automatic firing; user controls
   when to see it. Minimal-impact.
2. **Auto-print on load.** After `load "file.s"` parses successfully,
   print the listing once. Better discoverability — the student
   doesn't have to know to ask — but adds output to every load.
3. **Auto-print on first run/step when `-explain` is on.** Pairs
   the listing with the existing teaching mode. Once per session
   (use the same `legend_emitted`-style flag).

Recommendation: **(1)** for the REPL command, plus **(3)** for the
auto-print under `-explain` so the teaching-mode flow shows it
without the student needing to know about it. (2) is too noisy for
batch/script invocations.

## Implementation outline

There's already a `dump_text_seg(bool kernel_also)` in
`src/spim.c:1108-ish` that writes the text segment to a file. The
listing function is a sibling: walk the text segment the same way,
but write through `write_output(message_out, ...)` and format each
line as above.

Pseudocode:

```c
static void print_listing(bool kernel_also) {
  mem_addr addr = TEXT_BOT;
  mem_addr end = next_text_pc;  /* the assembler's "highest used" PC */
  while (addr < end) {
    instruction* inst = read_mem_inst(addr);
    if (inst != NULL) {
      char* dis = inst_to_string(addr);
      write_output(message_out, "  %s", dis);
      if (dis[strlen(dis) - 1] != '\n')
        write_output(message_out, "\n");
      free(dis);
    }
    addr += BYTES_PER_WORD;
  }
  if (kernel_also) {
    /* same walk over K_TEXT_BOT .. next_k_text_pc */
  }
}
```

The exact extents (`TEXT_BOT`, `next_text_pc`) come from spim's
memory module — check `mem.h` / `mem.c` for the existing names.

For the new REPL command, add an `LIST_CODE_CMD` token next to
`DUMP_TEXT_CMD` in the enum in `src/spim.c`, a `str_prefix(..., "listing", 4)`
case in `read_assembly_command()`, and a case in
`parse_spim_command()` calling `print_listing(false)` (or
`print_listing(true)` if the student typed `listing kernel`).

For the auto-print at the start of `-explain` runs, hook into
`top_level()` (or the first `explain_before` call) — gated by the
same kind of file-static `bool` used for `legend_emitted`.

## Risks / open questions

- **Long programs.** A program with thousands of instructions
  produces a giant listing. Worth capping at, say, 256 lines with a
  "(N more — use `listing all` to see them)" trailer. Probably not
  needed for the teaching-tool use case (assembly programs are
  small) but worth thinking about.
- **Empty / partially-loaded segments.** Reading `read_mem_inst`
  past the assembled end may return uninitialized data. Stop at
  `next_text_pc` — don't iterate the full segment.
- **Pseudo-ops in the listing.** Multi-instruction expansions
  (`la` → `lui`+`ori`) will print two listing entries, both with
  the same source line. The existing pseudo-op header logic
  handles this fine during stepping; in the listing, the source
  line just appears twice. Acceptable. (Optionally, suppress the
  second occurrence's `; LINE: SOURCE` portion to make it visually
  obvious it's a continuation.)

## Effort

Small. A few hours including tests:
- Add the `print_listing` function in `src/spim.c` (or a new
  small file).
- Add the REPL command (token, parse case, dispatch case).
- Add the once-per-session auto-print hook under `-explain`.
- Verify with `helloworld.s` and `tests/tt.explain.s`.
