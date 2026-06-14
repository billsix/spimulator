# Documentation porting QA — RST vs upstream DocBook XML

Compares `docs/source/*.rst` (the reStructuredText port) against
`upstreamSource/*.xml` (the original DocBook). Findings from a
chapter-by-chapter review of all 23 ported chapters / appendices
plus the four XML files with no RST counterpart.

Citations are `file:line` on both sides where useful.

> **Status (May 2026):** Almost everything in this document has
> been resolved.  See the archived session notes
> [`tasks/archive/2026/05/24/SESSION_NOTES.md`](../../tasks/archive/2026/05/24/SESSION_NOTES.md)
> for the cumulative status block.
>
> | Section | Status |
> |---|---|
> | 🔴 Critical (5 items) | ✅ Done — `396fcb5`, `44a415d`, `9543d28` |
> | 🟠 High — placeholder leaks (~25) | ✅ Done — `323201b` (Phase 1) |
> | 🟡 Medium — RST quality | ✅ Done — `285e10e` (`:ref:` cleanup), `2c739c6` (rename), `44a415d` (asciiap row 56, guidelines), toctree adds in subsequent commits |
> | 🟢 Low — `fdlap.rst` GFDL polish | 🟡 Open (smart quotes, missing `©`) |
> | 📦 Structural gaps — Dedication / History | ✅ Done — `9543d28` (Phase 5) |
> | 📦 Structural gap — `MemoryAdvCh.xml` | ✅ Confirmed not a port bug (upstream-superseded) |
> | Bonus — LLDB Quick-Reference added | ✅ Done — `3319dab` |
> | Build infrastructure (Makefile, entrypoint scripts, conf.py) | ✅ Done — `a290195`, `9341396`, `6288f93`, `d5725a8` |
> | Phase 8 — inline-asm fidelity sweep (9 substantive bugs in inline `::` snippets vs. `src/*.s`) | ✅ Done — Phase 8 commit |
>
> Findings below are preserved as historical record.  Anything
> still actionable is noted with **🟡 OPEN** inline.

---

## 🔴 Critical — actual data loss / wrong technical content

### `syscallap.rst` — entire syscall table body lost

`docs/source/syscallap.rst:25-57` collapses what should be a
6-column table (eax / Name / ebx / ecx / edx / Notes) to a
1-column table containing only the eax numbers. All syscall
names (exit, read, write, open, close, chdir, lseek, getpid,
mkdir, rmdir, dup, pipe, brk, ioctl) and all argument
descriptions are gone. Source: `upstreamSource/SyscallAp.xml:34-168`.

This is the single biggest data-loss item — ~116 lines of
content. Also the file is **orphaned from `index.rst`'s toctree**
(see Medium below).

### `gdpapp.rst` — GDB quick-reference table truncated

`docs/source/gdpapp.rst:261-275` ends at "Miscellaneous" with
3 rows. The XML at `upstreamSource/GDBAp.xml:310-388` has 5
more category sub-tables: Running the Program, Using
Breakpoints, Stepping, Examining Registers/Memory, Examining
the Call Stack — about 35 GDB commands missing.

### `files.rst:221` — corrupted constant

Renders as `.equ LINUX_SYSCALL, 0x800x80`. The XML
(`FilesCh.xml:307`) has the correct `0x80`. Caused by an
`<indexterm>` strip that concatenated indexterm content with
the value. Readers will copy the wrong constant.

### `linking.rst:200, 205` — wrong path

Both lines say `/libc.so.6` (or `=> /libc.so.6`); should be
`/lib/libc.so.6`. The `lib/` prefix was lost.
`upstreamSource/LinkingCh.xml:226, 233` is correct.

### `instructionsap.rst:233` — wrong `divl` operand

The cell describing `divl` says it puts the result in `%edx`.
The XML had `%edx:%eax` (the 64-bit divide pair). One half of
the operand pair was lost when the indexterm placeholder was
stripped.

---

## 🟠 High — placeholder/entity leakage from XML→RST conversion

The porter's macro stripper had bugs around `<indexterm>` and
ampersand entities (`FIXMEAMP...;` placeholders). Most of these
are mechanical regex-able fixes, **but be careful** — eyeball
each one because the surrounding text was also damaged in some
cases.

### `AT&T` mangled to `&T`

* `instructionsap.rst:611` — `*&T* syntax`
* `instructionsap.rst:662` — `its &T counterpart`
* `instructionsap.rst:672` — `not the &T syntax`

Source XML had `ATFIXMEAMPamp;T` — the `AT` prefix was
discarded along with the entity.

### Stray `;` after register names (`%cl;`, `%edx;` etc.)

* `instructionsap.rst` — 8 occurrences around lines 306, 319,
  324, 336, 341, 357, 372, 385 (`I/%cl;, R/M`)
* `files.rst:352, 362, 370` — `%cl;`
* `ctranslationap.rst:215-217, 443` — `&ecx`, `ecx-indexed;`,
  `&esp-indexed;`

### `linking.rst` cluster

* `:88` — caption typo `hellowworld-nolib.s` (extra `w`)
* `:121` — `-dynamic-linker-dynamic-linker /lib/ld-linux.so.2`
  (the flag string was duplicated by indexterm bleed)
* `:236, 339` — `**` rendered where source had `*` (single
  asterisk pointer marker)
* `:255` — `......` rendered where source had `...`
* `:496` — orphan bare line `LD_LIBRARY_PATH` floating in
  prose flow

### `firstprog.rst` cluster

* `:619` — orphan bare line `movl` between a code block and
  bullet list (was an `<indexterm>`)
* `:335` — `echo$?` (missing space; XML had `echo $?`)
* `:1076` — over-escaped footnote: `` ``%edi`` \ \ \ \*4 ``

### `counting.rst:172, 290, 674, 676`

Backslash-escaped asterisks (`\*`) rendering literally. Line
290 also lost italics on "binary" and has an extra space
before the period.

---

## 🟡 Medium — RST quality / structure

### Cross-references left as raw HTML anchors

These were `<xref linkend="..."/>` in the XML and should be
`:ref:`-style references in RST. Currently they're unportable
HTML-style anchors that won't resolve in Sphinx:

* `firstprog.rst:35-36` — `\`Outline of an Assembly Language Program <#assemblyoutline>\`__`
* `firstprog.rst:643, 652` — `\`Addressing Modes <#movaddrmodes>\`__`
* `files.rst:60` — `\`Buffers and <#buffersbss>\`__`
  (also note the truncated link text — `.bss` is missing)
* `memoryint.rst:343` — `\`Interpreting Memory <#interpretingmemory>\`__`

Should all become `:ref:\`assemblyoutline\`` etc.

### `asciiap.rst:29` — broken table row

Row 56 has only 7 visible columns instead of 9 (`<`, `=`, `>`,
`?` are misaligned with the column grid). Will produce a
docutils error or render garbled.

### `guidelines.rst:10` — malformed heading

The chapter title `Basic Guidelines for Software Development`
sits under a `..` comment block with 3 leading spaces; the
`===` underline below has no leading indent. Sphinx will treat
the indented title as a quoted block, breaking the heading.
Also still has a `FIXME` marker on line 13. Source XML
(`GuidelinesCh.xml`) is essentially empty (`<para></para>`),
so this is an upstream stub — but the RST should at least
render without an error.

### `index.rst` toctree

* `syscallap.rst` is **not in the toctree** despite existing —
  orphan page.
* `gdpapp.rst` is in the toctree but with the typo'd name.
* No entries exist for `dedicationap.rst` / `historyap.rst`
  because no such files were created (see Structural gaps
  below).

### Filename typo: `gdpapp.rst` → `gdbap.rst`

Should be GDB (the debugger), not GDP. Internal label
`_gdbappendix:` is fine — only the filename and one toctree
entry need updating.

* Rename: `docs/source/gdpapp.rst` → `docs/source/gdbap.rst`
* Update: `docs/source/index.rst:27`

---

## 🟢 Low — `fdlap.rst` is not verbatim for legal text **🟡 OPEN**

The GFDL is canonically distributed with straight ASCII quotes;
the RST has substituted smart curly quotes throughout (lines
9, 16, 33, 35, 41, 51, 55, 68, 80, 83, 95, …). This is a
paraphrase risk under the license's own "verbatim copying"
clauses.

`fdlap.rst:416` also reads `Copyright YEAR YOUR NAME.` — the
`©` symbol is missing (XML had `&copy;`).

If you don't care about verbatim status: leave it. If you do:
re-import the canonical GFDL 1.3 text from gnu.org rather than
relying on the XML round-trip.

---

## 📦 Structural gaps — XML files with no RST counterpart

| XML file | Status | Recommendation |
|---|---|---|
| `MemoryAdvCh.xml` (878 lines) | **Not a port bug.** Upstream-superseded earlier draft of `MemoryIntCh.xml`; content lives in `memoryint.rst`. The "Instruction Pointer" subsection was already commented out in the XML before the port (`<!-- SECTION REMOVED - DON'T KNOW WHY IT WAS HERE`). | No action. |
| `DedicationAp.xml` (83 lines) | Genuine content (acknowledgements, dedication), referenced by the master `ProgrammingGroundUp.xml:257` as `&dedication-appendix;`. Not folded anywhere. | **Port it** to `dedicationap.rst` and add to `index.rst` toctree. |
| `HistoryAp.xml` (27 lines) | Genuine content (8 entries of version history, 1.0–1.1). Not folded anywhere. | **Port it** to `historyap.rst` (only ~10 lines of actual content) and add to toctree. |
| `ProgrammingGroundUp.xml` (261 lines) | This is the master DocBook document — `<!ENTITY ...>` declarations + chapter inclusions. The RST equivalent is the `index.rst` toctree, which is correct in shape (just incomplete in content as flagged above). | No action. |

---

## ✅ Chapters with no significant issues

`robust.rst`, `memory.rst`, `optimization.rst`, `otherlang.rst`,
`wherenext.rst`, `records.rst`, `cch.rst`, `guiap.rst`. (The
`asciiap.rst` ASCII table is fine *except* for row 56.)

---

## Pre-existing upstream issues, faithfully preserved

These look like bugs but are present in the XML — not regressions
introduced by the port. Listed for awareness; you may want to
fix them in the RST anyway:

* `functions.rst:389` — truncated sentence "Note that in Linux
  assembly language, functions are" (XML `FunctionsCh.xml:526-528`
  has the same dangling clause).
* ~~`memoryint.rst` `BRK`/`SYS_BRK` inconsistency~~ — fixed in
  Phase 8 (`memoryint.rst:438` now declares `SYS_BRK` matching
  the references at lines 454, 545 and `src/alloc.s`).

---

## Intentional modernizations (not bugs)

The porter updated `as` / `ld` invocations across multiple
chapters with `--32` and `-m elf_i386` so the commands work on
modern 64-bit Linux hosts. Examples: `firstprog.rst:59, 74,
479, 480`, `functions.rst:573-574`, `records.rst:176-178`,
`gdpapp.rst:57`, `guiap.rst:74`. The XML originals predate the
need for these flags. Worth noting in case you wanted to keep
the text matching the printed book exactly.

---

## Suggested order of attack

1. **`syscallap.rst`** — restore the 6-column table from
   `SyscallAp.xml:34-168`. Biggest user-visible damage; also
   wire it into `index.rst`'s toctree.
2. **`gdpapp.rst`** — restore the missing GDB tables from
   `GDBAp.xml:310-388`; rename file to `gdbap.rst`; update
   `index.rst:27`.
3. **Run a sweep** for the two recurring placeholder leak
   patterns. Suggested regexes:
   * `%[a-z][a-z]?;` to find stray-semicolon-after-register
     leaks (`%cl;`, `%edx;`, etc.)
   * `&[a-z]+(-indexed)?;?` to find unresolved entity refs
     (`&ecx`, `ecx-indexed;`, `&copy`, `&T`)
   * `\\\*` to find escaped asterisks rendering literal
   One pass would mop up most of the High-priority list.
4. **Fix the four hard technical errors:**
   * `files.rst:221` — `0x800x80` → `0x80`
   * `linking.rst:200, 205` — add `lib/` prefix
   * `linking.rst:121` — de-duplicate `-dynamic-linker`
   * `instructionsap.rst:233` — `%edx` → `%edx:%eax`
5. **Replace raw `<#anchor>` cross-refs with `:ref:`** at the
   five locations listed above.
6. **Decide on `DedicationAp` and `HistoryAp`.** Both are
   short, both have real content, both are referenced from the
   master XML. Dropping them is a defensible choice, but if
   you're going to drop them, do it deliberately.
7. **Fix `guidelines.rst`** — make it a valid (if mostly
   empty) page.
8. **`fdlap.rst`** — only if you care about license-text
   verbatim status; otherwise leave.

Steps 1–4 cover everything that's technically wrong. Steps 5–8
are quality / completeness.
