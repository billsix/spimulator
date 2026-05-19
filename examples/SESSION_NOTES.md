# Session notes — May 2026

Durable handoff for future Claude sessions in `/examples`.  Container
is ephemeral; commit history + this file + `PLAN-asm-comments.md`
+ `PLAN-unix-tools.md` are the only persistent memory.

## Update — 2026-05-19 evening (spim Unix-process conformance + curriculum sweep)

Bill asked whether spim acts as a regular Unix process (motivated
by the possible PGU rewrite).  Investigation in
[`/spimulator/tasks/unix-process-conformance.md`](../spimulator/tasks/unix-process-conformance.md);
four defects identified and fixed:

1. Banner `Loaded: …` was on stdout, polluting pipelines.  Now on
   stderr.  `spim demo | wc` counts only the program's bytes.
2. `__start` always issued `syscall 10`, throwing away main's `$v0`.
   Now does `move $a0, $v0; li $v0, 17; syscall`, so `return N`
   from main becomes shell `$?`.
3. Parser errors, missing files, and undefined symbols now exit
   2 / 2 / 1 instead of 0.  Spim no longer drops into REPL on
   load failure.
4. Runtime exceptions (alignment, address, overflow, etc.) now
   exit `128 + ExcCode` instead of 0.  Convention mirrors
   shell's `128 + signum`.

### Curriculum follow-up (this session)

Defect 2 surfaced that ~22 .asm demos `jr $ra` from main with
whatever syscall number was last loaded into `$v0`.  Swept all
of them to insert `li $v0, 0` immediately before the
main-return `jr $ra`:

  06, 07, 08, 09, 10, 12, 18, 19, 20, 21, 22, 25, 26, 27, 28,
  30, 32, 34, 35, 36, 37, 39

Verified post-sweep that the only non-zero exits are intentional:

- 17-nologin (1): `/etc/nologin.txt` doesn't exist in container,
  demo correctly takes its error path.
- 24-get-char-from-user-1 (133 = 128+5): intentional alignment-
  fault teaching demo.
- 42-subrountines-1 (155 = 128+27): intentional "read past
  frame" teaching bug.
- 43-testStringsForEquality-1 (1): intentional teaching bug.

The C side already returns correctly via `crt0.h` propagating
`my_main`'s return value through `exit`.

### Two new plan docs written this evening (not started)

- [`PLAN-asm-listings.md`](PLAN-asm-listings.md) — keep
  compiler-generated `.s` on disk for every C demo so students
  can study the native translation alongside the hand-written
  asm.  Recommendation: Option A (`-save-temps=obj`), a
  one-line meson change.  Bill's request.
- [`PLAN-container-cross-env.md`](PLAN-container-cross-env.md)
  — bake clang + lld + qemu-user-static into `/examples/Dockerfile`
  so the multiarch shim is verifiable in-container.
  Recommendation: Option A (clang+lld, no GNU cross gcc),
  ~150 MB.  Unblocks the multiarch-shim verification step and
  later PLAN-build-matrix.  Bill's request.

### Multiarch shim — status check

The shim itself (`src/crt0.h`) is fully written for x86_64,
i386, arm, aarch64, mips, and rolled out across 30+ argv-using
demos.  Only the x86_64 branch is exercised today.  Verifying
the other four needs the container cross-env above; until that
lands, the non-x86_64 branches are "written, plausible, not yet
run."

### Demo 00 — the canonical "set status, exit" lesson (landed)

Added `src/00-exit/00-exit.{c,asm}` as Part 0 of the curriculum.
This is the PGU exit.s pattern retargeted to MIPS+spim — the
smallest possible program (no output, just a status code) — and
serves as the canonical reference for the syscall mechanism and
the `$v0`-before-`jr $ra` discipline introduced by the
Unix-process fixes.

The .asm header is the teaching block; PLAN-asm-comments.md got
a "this lesson lives at 00-exit" pointer so future demos can
assume the reader has seen it and avoid restating.  meson.build,
READING-ORDER.md (new Part 0 section + 2 concept-table rows)
updated.

---

## Update — 2026-05-19 (Tier 1+2 landed, hardcodes removed, spim octal fix)

Three plans closed out this session.  Snapshot for the next session:

### `PLAN-tier1-tier2-tools.md` — landed

12 new Unix-tool demos at slots 28-39: `seq`, `touch`, `factor`,
`cp`, `uniq`, `nl`, `cut`, `od`, `tac`, `tail`, `comm`, `base64`.
Each has paired C + asm; smoke-tested with C-vs-spim diff and,
where applicable, against the real system tool (`factor`, `od -c`,
`base64`, `comm`).  See [`PLAN-tier1-tier2-tools.md`](PLAN-tier1-tier2-tools.md).

Two non-obvious defects fixed during smoke-test:

- **35-od** hit a real spim bug in `.asciiz` octal-escape
  decoding (see below).  Fixed in spim; `35-od.asm` keeps the
  `\134` form, which now decodes correctly.
- **39-base64**'s `emit_char` clobbered `$t0`, while the caller
  held `b0` in `$t0` across the four emit_char calls per triple.
  Scratch moved to `$t8`; header comment names the constraint.

### `PLAN-remove-hardcodes.md` — landed

Six demos had hardcoded inputs replaced with argv / stdin:
06-fizzbuzz (`N` from argv), 07-bubble-sort (ints from stdin
+ `$a3` EOF), 08-pascals-triangle (`rows` from argv),
09-sieve (`LIMIT` from argv, **sbrk-allocated** bit array),
22-binary-search (target from argv, sorted ints from stdin),
27-queens (generalized to N-queens from argv, cap N=12).

### `READING-ORDER.md` — added Part 7

Slot 28-39 is now a named section: "Unix toolchest (12 demos)"
— read after Part 6 in any order, no new register-class concept
introduced but each demo lands a specific asm pattern.  Concept
index gained 9 new rows.

### Spim `.asciiz` octal-escape bug — fixed

`scanner.l` `copy_str` shifted the high octal digit by 3 bits
instead of 6, so `\abc` decoded to `(a+b)*8+c` rather than
`a*64+b*8+c`.  `\134` (intended `\`) silently produced `$`
(0x24).  Only escapes with high digit ≥ 1 were affected; the
`\033` form (high digit 0) was self-healing because the bad
shift on `0` produces `0` either way — so existing 04-clear
and others were untouched.

Audit: only two `.asciiz` octal escapes anywhere in the trees
(`\033` in 04-clear, `\134` in 35-od); no workarounds had been
added to compensate.  See
[`/spimulator/tasks/octal-escape-fix.md`](../spimulator/tasks/octal-escape-fix.md)
and ChangeLog 2026-05-19 entry.

### Files touched this session

- `/examples/src/{28..39}-*/*.{c,asm}` — 12 new demo pairs
- `/examples/src/meson.build` — 12 new build entries
- `/examples/src/{06,07,08,09,22,27}-*/*.{c,asm}` — argv/stdin
- `/examples/src/{read-int.c, io.h}` — `read_int_from_stdin` helper
- `/examples/READING-ORDER.md` — Part 7 + concept index
- `/examples/PLAN-tier1-tier2-tools.md`, `PLAN-remove-hardcodes.md` — marked landed
- `/spimulator/src/scanner.l:493` — octal-escape fix
- `/spimulator/tests/tt.octal_escape.s` — regression test
- `/spimulator/tasks/octal-escape-fix.md` — bug writeup
- `/spimulator/ChangeLog` — 2026-05-19 entry

### Open queue (priority order, surfaced for next pick)

**/examples plans not yet landed:**
1. `PLAN-multiarch-shim.md` — extend `_start` shim to i386, arm,
   aarch64, mips (currently x86_64-only).  Gates the build matrix.
2. `PLAN-build-matrix.md` — Dockerfile cross-compiles every C
   demo to 5 ISAs at image build, ships `.s` files side-by-side.
   **Blocked on the shim plan.**
3. `PLAN-symbol-tables.md` — replace `#VARIABLES` block with a
   C-var → register/stack-offset table; recursive demos get an
   ASCII stack-frame diagram.  Highest pedagogical value per LoC.
4. `PLAN-asm-comments.md` — L1-L4 sweep applied to 01-17; demos
   18-39 still on older comment shape.
5. `PLAN-cs-demos.md`, `PLAN-unix-tools.md` — open-ended Tier 3+
   wishlist; pick from when more demos are wanted.

**/spimulator open tasks (memory says: surface, don't auto-begin;
only the parser-migration Phase 0 is pre-authorized):**

- `tasks/handwritten-parser-migration.md` Phase 0 — written
  inventory of flex+bison, no parser change.  **Pre-authorized.**
  (Was originally `antlr-migration.md`; rewritten 2026-05-19
  to target hand-written recursive descent in pure C instead.)
- `tasks/program-listing-at-start.md` — full pre-execution listing.
- `tasks/software-alu.md` — bit-level ALU per H&P.
- `tasks/repl-args-command.md` — gdb-style `args` REPL command.
- `tasks/NEXT-SESSION.md` follow-ups — ABI role annotations,
  syscall string dump, stack-frame intent hint, golden expected
  output for `tt.explain.s`.

---

## Update — 2026-05-18 morning (argv unblocked + Phase A/B done)

It turned out spim DOES pass argv — `exceptions.s` already calls
`main` with `$a0=argc, $a1=argv`, and `initialize_run_stack()`
lays argv on the stack the standard way.  What was actually
broken was spim's **command-line parser**: non-dash tokens after
`-file foo.asm` re-entered the file-name branch and overwrote
`program_argc/argv` down to argc=1.

Spimulator-side work (all done, all committed in `/spimulator`):

- **`tasks/argv-command-line-handling.md`** — task doc.
- **`src/spim.c`** — 3-line patch: `if (assembly_file_loaded)
  continue;` at the top of the `-file` branch, `break;` after
  the file is captured.  Stops the parser from stomping
  `program_argc/argv` on each subsequent arg.
- **`tests/tt.argv.s`** — regression test.  Invoked as
  `spimulator -f tt.argv.s alpha beta gamma`, verifies argc=4
  and argv[1..3][0]='a'/'b'/'g'.  Fails on unpatched code,
  passes after.
- **`Dockerfile`** — added the tt.argv.s test to the existing
  test block.  Also simplified `-ef ../src/exceptions.s -file`
  → `-f` everywhere (works because `meson install` runs first
  and puts exceptions.s at the compiled-in default path).
- **`tasks/repl-args-command.md`** — written, not yet
  implemented.  Plan for a gdb-style `args N M ...` REPL
  command so students can iterate on numeric inputs without
  restarting spim.

Examples-side work (all done, all committed in `/examples`):

- **Phase A foundation** — `src/string-to-int.c` (the
  `parse_int` C helper, mirrors `integer-to-string.c`);
  declared in `src/io.h`, added to `examples_io` via
  `src/meson.build`.  The asm side replicates a small `atoi:`
  subroutine per demo (pgu pattern of self-contained .asm
  files).
- **19-echo** — first argv demo.  C side uses an inline-asm
  `_start` crt0 shim (x86_64 Linux) that pulls argc/argv off
  the kernel-supplied stack and calls `my_main(int, char**)`;
  the shim is the same one
  `/pgu/src/c/toupper-nomm-simplified.c` uses.  Spim side
  reads `$a0`/`$a1` at main entry (no syscall needed).
- **20-factorial** — first numeric-argv CS demo.  Iterative
  `mult`/`mflo` loop.  Spim version has its own `atoi:`
  subroutine.  Verified end-to-end matches between C and spim
  including the silent overflow at `factorial 13` →
  1932053504.

Syntax simplification:

- After `meson install`, spim's default exception-handler path
  (`/usr/local/share/spimulator/exceptions.s`) resolves, so
  `spimulator -f foo.asm args...` Just Works — no `-ef ...`
  needed.  Updated all .asm and .c file invocation comments,
  plan docs, the Dockerfile, and the SESSION_NOTES "how to
  resume" section.  Long form is still the dev-tree fallback
  when spim hasn't been installed.

What's open after this round (priority order):

1. **repl `args` command**
   (`/spimulator/tasks/repl-args-command.md`) — natural next
   /spimulator task; small; workflow win for every CS demo we're
   about to write.
2. **Phase C unix tools** (`PLAN-unix-tools.md`): `cat <file>`,
   `gcd a b`, `head -n N <file>`, `tee <file...>`.
3. **CS demos** (`PLAN-cs-demos.md`): 21-fibonacci (introduces
   nested-call stack frames), 22-binary-search, etc.
4. Sphinx book chapters (still untouched beyond ch01).

## Update — overnight 2026-05-17/18 (Phase 5 started: 18-cksum)

Bill asked to start on "the last porting of sbase ubase code"
while away.  Phase 4 (argv-needing) WAS blocked on (we thought)
spimulator, but a new **Phase 5** of stdin-only *algorithmic*
sbase tools is doable and pedagogically distinct.

**18-cksum** done in both C and spim asm.  Verified byte-for-byte
against system `cksum`:

| input | output |
|---|---|
| empty | `4294967295 0` |
| `a` | `1220704766 1` |
| `hello world` | `1135714720 11` |

New pedagogical territory this demo introduces:

- **Bitwise ops** — `srl`, `sll`, `xor`, `nor`, `andi`.  No
  previous demo had touched these.
- **256-entry lookup table in `.data`** via `.word` directives.
- **Indexed memory access** — compute the table address as
  `crctab + (idx << 2)`, then `lw`.
- **A private subroutine called from a stdin-only `main`** —
  `print_uint`.  This is the first demo where main needs to save
  `$ra` across a `jal` *and* doesn't already have a stack frame
  for a different reason.  Solved by parking the caller's `$ra`
  in `$s0` at entry, restoring before the final `jr $ra`.  Hit a
  fun bug along the way: the original asm forgot the save, so
  spim looped forever after the answer line was already printed.
- **Unsigned 32-bit decimal output** — both via a new
  `print-uint.c` helper added to the io library (the C side) and
  a hand-rolled `divu`/`mfhi`/`mflo` digit loop in the asm
  (since spim's syscall 1 prints `%d`, not `%u`).

Side effects:

- `src/print-uint.c` — new io helper.  `src/io.h` declares it,
  `src/meson.build` includes it in `examples_io`.
- `PLAN-unix-tools.md` — new "Phase 5" section listing what's
  done and candidate next demos (`strings`, `tail -n N`, `od`,
  potentially a hash demo).

Stopped after one Phase-5 demo because Bill said "start" — the
foundation is in place for more, and the picks are a choice
point for the morning.

## Update — late 2026-05-17 (unix-tool ports + meson + spim gotchas)

Three landed in the same window:

**Phases 1, 2, and 3 of `PLAN-unix-tools.md`** — nine new demos
appended after 08:

- **04-clear** (`ubase/clear`): write ANSI ESC `[2J [H`.  Octal
  `\033` used in both .c and .asm because spim's `.asciiz`
  accepts `\X` (uppercase) and `\NNN` but NOT lowercase `\x`.
- **05-yes** (`sbase/yes`): infinite `print_string("y\n")`.  spim
  asm sets `$v0=4, $a0=&str` ONCE outside the loop; inner body
  is just `syscall; j forever`.
- **16-cat** (stdin only): first demo with block I/O (syscalls
  14/15 in spim, `os_read`/`os_write` in C).  EOF via return
  value, no sentinel needed.
- **10-wc**: byte + line counters, same shape as 06 with one
  extra counter.  spim still uses 'z' as sentinel because
  syscall 12 (read_char) has no EOF.
- **11-head**: hardcoded N=10, early loop exit at the 10th
  newline.
- **12-rev**: 256-byte fixed line buffer, reverse and emit on
  '\n'.  Uses `$t9` as scratch (NOT `$at` — see gotchas).
- **17-nologin**: open + read/write loop + close.  First demo
  that needs a non-zero exit status, which forced us to
  discover the second gotcha below.
- **13-tr** (`sbase/tr 'a-z' 'A-Z'`): byte-level conditional
  transformation, hardcoded mapping.  Sentinel changed from
  `z` (used by 06/12/13/14) to `~` because `z` collides with
  the a..z upcase range.  Same reasoning applied to 17.
- **15-expand** (`sbase/expand`): tabs → spaces with a `col`
  counter maintained across the stream.  Hardcoded tab width
  of 8.  Variable number of output bytes per input byte —
  the new pattern this demo introduces.

Append, not renumber — directory order is not pedagogical order;
the eventual Sphinx book can reorder.

**Build system: CMake → meson**, mirroring `/spimulator`.  New
`src/meson.build`, updated `src/buildDebug.sh` (meson setup +
compile + compile_commands.json symlink), `codeblocksbuildDebug.sh`
and the four `template/*.sh|.bat` cmake scripts deleted (meson has
no Code::Blocks backend).  `src/CMakeLists.txt` was never
restored — meson is the only build system now.

**Vendored `src/deps/musl/` removed.**  Was needed when the C
demos linked with musl-gcc; the freestanding pgu-style C uses
`os.h`'s inline syscalls and needs no libc.

**Two spim gotchas discovered and folded into
`REFERENCE-encodings.md`** (new section at the bottom):

1. **`$at` is reserved by the assembler.**  Explicit `slti $at,
   ...` fails with "Register 1 is reserved for assembler"; the
   syntax error cascades and labels later in the file appear
   undefined.  Use `$t9` or save/restore an `$s` register for
   extra scratch.
2. **`jr $ra` from main always exits 0.**  `exceptions.s` runs
   `li $v0, 10; syscall` after main returns, and syscall 10's
   handler ignores `$v0` and sets exit status to 0.  To exit
   non-zero, issue syscall **17** (exit2) yourself with the
   status in `$a0`.  17-nologin's `#NOTES` block has the
   pattern.

## Where we are (end of 2026-05-17)

The .asm pedagogy for demos 01–08 is **complete**:

- **C side** rewritten in pgu-style freestanding C (no libc,
  `_start`, inline-asm syscalls via `src/os.h`).  All 8 demos
  smoke-tested in their stripped form.
- **Asm side** carries the four/five-layer annotation described
  in `PLAN-asm-comments.md`:
  - L1 (C source block), L2 (`#PURPOSE`/`#VARIABLES`), L3 (block
    C-statement comments), L4 (right-side inline mechanic
    comments) on all 10 .asm files.
  - L5 (PC + encoding + real-form prefix from `spimulator
    -explain=4`) on 05, 06, 07-1, 07-2, 08-1 — the files where
    pseudo expansion or `jal` mechanics make the encoding column
    pedagogically necessary.
- **Stack-frame pacing** stripped per pgu principle — 01–03, 05,
  06 are register-only.  04-1/04-2 keep their frames because the
  alignment lesson IS the demo.  07/08 keep theirs because `jal`
  requires saving `$ra`.
- **Register naming** consistent: every `.asm` uses ABI names
  (`$fp`, not `$s8`; `$0`, not `$r0`) — even in L5 prefix lines
  where the disassembler emits the numbered form.  The numbered
  alias only appears in `src/REFERENCE-encodings.md`.

What's not yet underway:

- `PLAN-unix-tools.md`'s sbase/ubase ports (Phase 1: `clear`,
  `yes`; later phases add `cat`, `wc`, `head`, `rev`, `nologin`,
  `tr`).  Open decision: renumber existing demos vs append.
- Sphinx book chapters beyond ch01.
- Re-running `spimulator -explain=4` on 01–06 to refresh the
  `src/*/NN.txt` traces — those were captured BEFORE the pgu
  strip and are stale for 01–06.  03.txt is doubly stale (it was
  generated against the now-deleted 03-increment-ints-2.asm).

## Updates this session (in order)

### A. C demos rewritten in pgu-style freestanding C

- Source of inspiration: `/pgu/src/c/` (read its `README.md` and
  `os.h`).
- Each demo's `NN-name-1.c` is now `_start`-based, `-nostdlib`,
  talks to the kernel through inline-asm syscalls in `src/os.h`.
- The `operatingsystemfunctions/` shim and the `-2.c` / `-3.c`
  intermediate iterations were deleted.  Only `-1.c` per demo
  remains.
- IO helpers under `src/`: `count-chars.c`, `integer-to-string.c`,
  `print-string.c`, `print-int.c`, `print-char.c`, `read-char.c`,
  plus `io.h` declaring them.
- Build: `src/CMakeLists.txt` switched to `-O0
  -fomit-frame-pointer -fno-asynchronous-unwind-tables
  -fno-unwind-tables -fno-stack-protector` with `-nostdlib`.

### B. Dockerfile upgraded to Fedora 44

Was Debian trixie + apt + texlive.  Now Fedora 44 + dnf in the
/spimulator style.  Package names ported (`texlive-collection-
latexextra`, `python3-sphinx_rtd_theme`).  Build not yet verified
on Bill's host — `podman build` failed in this sandbox for
rootless-namespace reasons unrelated to package names.

### C. ASM comment style — final state

Iteration history (so future sessions don't accidentally undo):

1. Replicated Bill's per-instruction state-diagram style (the
   original 03-increment-ints-2.asm reference).
2. Bill redirected to "C-construct → asm-construct mapping" —
   quote the C source, block comments above each chunk.
3. Started swapping to /pgu's pure inline style; Bill stopped me
   and asked for **both** styles layered together (L1+L2+L3+L4).
4. Bill called out that the encoding info (`-explain=4`) should
   only appear where it teaches something.  Layer 5 deferred to
   05 first.
5. Bill called out that 01–06 were carrying stack frames they
   didn't need.  pgu strip applied to 01, 02, 03-1, 05, 06.
   04-1/04-2 kept frames (alignment lesson).
6. Layer 5 rolled out on 05, 06, 07-1, 07-2, 08-1.
7. `$s8 → $fp` and `$r0 → $0` normalized across L5 prefix lines.
8. 03-increment-ints-2.asm deleted (byte-identical duplicate of
   -1 after the strip).

### D. New files added

- `src/REFERENCE-encodings.md` — bit-encoding cheat sheet (R/I/J
  formats, opcode table, register-number ↔ ABI-name mapping).
- `PLAN-asm-comments.md` — the layered-comment plan.
- `PLAN-unix-tools.md` — sbase/ubase port plan.

(`FIXES-pending.md` was created and then deleted once §1's
decision was folded into `PLAN-asm-comments.md`.)

### E. Sphinx `ch01.rst` updated

Now references `include` and `main` doc-regions only (the old
three-region structure with `os functions` is gone since the
freestanding C only has the `io.h` include + `_start`).

## Things that look like bugs but are intentional teaching artifacts

Each is called out in the relevant .asm's `#NOTES` block.  Do NOT
silently fix them.

- `24-get-char-from-user-1.asm` — frame at `0..4($fp)` is not
  word-aligned for the int32_t (THE alignment lesson); also
  compares against `la $t0, a` (string *address*) instead of the
  byte `'a'`; also reads `8($fp)` past the 5-byte frame.  `-2.asm`
  fixes the alignment.
- `24-get-char-from-user-2.asm` — inside the loop body the two
  syscall selectors for "print the char" and "print the int" are
  swapped relative to the C source.  Program still completes; the
  per-iteration output is in the opposite order.
- `42-subrountines-1.asm`, `43-testStringsForEquality-1.asm` —
  `lw $v0, N($fp)` at the end reads past the frame after teardown.
  SPIM surfaces 0 here.

## How to resume

1. `cd /examples` and `git status` / `git log -10` to see what's
   landed since this note.
2. Skim `PLAN-asm-comments.md` (the "Status" line at top tells
   you whether the asm-side work is still done).
3. Skim `PLAN-unix-tools.md` for the next batch of work.
4. Read one .asm file end-to-end (e.g. `src/40-print-out-ascii/05-
   print-out-ascii-1.asm`) to see the current layered comment
   style in action — especially L5 prefix conventions.
5. Once spimulator has been installed (`meson install -C
   builddir` from `/spimulator`), the simple form works:

   ```
   spimulator -noexplain -quiet -f <demo>.asm [args...]
   ```

   Default exception-handler path resolves automatically.  In a
   build tree that hasn't been installed yet, fall back to:

   ```
   /spimulator/builddir/spimulator \
       -ef /spimulator/src/exceptions.s \
       -noexplain -quiet \
       -f <demo>.asm [args...]
   ```

   Replace `-noexplain` with `-explain 4` to capture a trace.
