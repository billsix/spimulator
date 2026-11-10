# Repository review — 2026-11-10 (simulator core + full git history)

**What this is.** A deep review, using the method in the repo-review-conventions.md reference doc
from github.com/billsix/runClaudeInContainer, scoped by the maintainer to the **simulator core**
(`src/`, `include/`, `tests/`, `meson.build`, `meson_options.txt`, `Documentation/spim.1`) and the
**entire git history** (1161 commits; 752 before the maintainer's first commit `e2d4f30`). Not
`examples/`, `pgu/`, `tree-sitter/`, or the task backlog/archive. It reads the core through two
lenses:

- **C23 lens** — what the core could still do more cleanly in C23 (struct initialization first).
  Feeds `tasks/c23-modernization-pass3.md` (§4).
- **Port lens** — every user-observable behavior a reimplementation of the engine in another language
  must match, and which oracle to trust. Feeds `tasks/engine-port-language-investigation.md` (§5, §6, §7).

**Requested by** William Emerison Six <billsix@gmail.com>, 2026-11-10 (`tasks/source-and-history-deep-audit.md`).

**Snapshot warning.** This record is frozen as of commit `0391ff3`. Line numbers rot; re-check a
finding against the tree before acting on it. Pass 3 (`tasks/c23-modernization-pass3.md`) starts
changing `src/` the same day.

**Baseline:** none — first review of this repo.

**Labels.** VERIFIED = checked by reading code or running a command (the main thread re-checked every
item marked MAIN-VERIFIED, by running the command shown); LEAD = plausible, not confirmed. Severity:
HIGH = wrong value / memory error / a gate that silently does not test what it claims; MEDIUM =
latent or documentation-misleading; LOW = cosmetic.

## Coverage

| Dimension | What was read | By |
|---|---|---|
| 1. Docs ↔ docs | `CLAUDE.md`, core-relevant `tasks/reference/*` (parser-ast-emit, simulator-integer-types, mips-fpu-and-float-demos, opcodes-x-macro, inherited-idiosyncrasies) — sampled for claims about the core | main thread + readers |
| 2. Docs ↔ code | `Documentation/spim.1`, `src/README`, usage text, help text, in-code comments | reader (driver) |
| 3. Product content | N/A for this scope (the core's "content" is its output formats, covered in §5) | — |
| 4. Code quality | `src/*.c`, `include/*.h`, `src/exceptions.s` — **read in full** (~17K lines) by four subsystem readers; `opcodes.h` read by structure + grep | 4 readers, main thread verified |
| 5. Generators and tools | `src/dump-opcodes.c`, `tests/gen-parity-golden.sh` | readers |
| 6. Tests | `tests/run-test.sh`, `meson.build` test inventory in full; every `tests/*.s` header + sampled bodies; `examples/tests/run-demo.sh` registration | reader (tests), main thread verified |
| 7. Proofs | N/A (none) | — |
| 8. Build | `meson.build`, `meson_options.txt`, `Dockerfile` (test/sanitizer stages only) | main thread + reader |
| 9. Open-task backlog | **not covered** (out of scope) except `tasks/codebase-cleanup-plan.md` reconciliation | — |
| 10. Archive hygiene | **not covered**; archive docs read only as history sources | — |
| History | upstream era (to `6cb6a08`, SPIM 9.1.24) and maintainer era (`e2d4f30..HEAD`, 120 core-touching commits) via `git log --stat/-S/-G`, targeted `git show` | 2 history readers, main thread verified |

## 1. History — what the tree descends from

### 1.1 The upstream baseline is `6cb6a08`, not `e2d4f30^` (VERIFIED, MAIN-VERIFIED)

`git merge-base github/upstream HEAD` = `6cb6a08` (2023-08-04, "Mac packaging cleanups"),
`CPU/version.h` = `"Version 9.1.24 of August 1, 2023 (final)"`. The fork merged the upstream branch
twice (`9f09939` 2023-07-02, `c50600f` 2023-08-23), so it already contains final upstream SPIM.
Upstream commits after the fork point: `7e74d28` (version bump), `3d581e8` (bad_mem_write NULL fix),
`21b2c69`/`2928817`/`e915fe2`/`75d116a` (clang-format reformat + a backout), `61bd2a3` (release
cleanup). **Use `6cb6a08` as the upstream oracle.** There is no `v9.1.24` tag; the last tag is
`v9.1.21` (`2317242`).

### 1.2 Upstream layout and what the fork removed (VERIFIED)

At `79ee3a7`: `CPU/ spim/ xspim/ QtSpim/ PCSpim/ Documentation/ Setup/ bin/ Tests/` + ChangeLog,
README, README.rtf, helloworld.s. `e2d4f30` removed xspim, QtSpim, PCSpim, Setup, bin; `4340e91`
moved `CPU/` + `spim/` (C++ files) to `src/` as C; `30efcf3` renamed `Tests/`→`tests/`; `ba262d9`
removed README.rtf. The exception handler descends from `trap.handler` (`5b89d94`, 2004) →
`CPU/exceptions.s`; `52a61aa` (2005) masks the exception code with 4 bits.

### 1.3 Licensing (VERIFIED)

From `c75531a` (2010-01-06) upstream is **3-clause BSD**, "Copyright (c) 1990-2021, James R. Larus"
(text in `79ee3a7:README` and every `CPU/*.cpp` header). Before that, a custom "All Rights Reserved"
notice. A port translated from the C is a derivative work: keep the BSD notice and the no-endorsement
clause; derive only from post-2010 code.

### 1.4 The old flex/bison grammar (VERIFIED, MAIN-VERIFIED)

| Version | Restore with | Lines |
|---|---|---|
| Upstream-pure | `git show 6cb6a08:CPU/parser.y`, `git show 6cb6a08:CPU/scanner.l` | 2967 / 733 |
| Last fork version (octal-escape fix + clang-tidy edits) | `git show 6bafa31^:src/parser.y`, `git show 6bafa31^:src/scanner.l` | 2987 / 742 |

Deleted in `6bafa31` (2026-05-18, "ported from flex/bison to claude generated hand parser"); last
present in `cda1886`. Paths over time: `SPIM/src/` (1998–2005) → `src/` → `CPU/` → (fork) `src/`.
That one squashed commit also deleted `tests/parity-harness.sh`; the bison-vs-hand parity result
(19/19 `tt` files, 44/44 examples byte-identical) survives only in
`tasks/archive/2026/05/23/handwritten-parser-migration.md`. The AST parity goldens
(`tests/golden.ast_parity*.txt`, `dd04f26`) were captured from the syntax-directed path before it was
removed (`f57352e`/`dd04f26`) and are now the assembler oracle.

Upstream grammar commits that changed behavior: `9635967` (2011, expressions in constant
definitions), `cd4e140` (2013, escaped-char scanning), `9b0892d`/`8141259` (2015, FP decimal comma; a
digit must follow the separator), `b35de8f` (2020, missing nops in pseudo expansions under delayed
branches), `2d506c1` (2021, `.align`).

### 1.5 Historical tests recoverable from git (VERIFIED, MAIN-VERIFIED for the byte-identity)

- All 12 files of upstream's `Tests/` at `6cb6a08` are **byte-identical** to today's `tests/`
  (`read.s time.s timer.s tt.alu.bare.s tt.bare.s tt.be.s tt.core.s tt.dir.s tt.fpu.bare.s tt.in
  tt.io.s tt.le.s`). Mainline never had expected-output files: `6cb6a08:spim/Makefile` (lines 132–175)
  ran each test and printed `tail -2`, i.e. tests are self-checking. Upstream flags per test:
  `tt.bare.s -delayed_branches -delayed_loads -noexception`; `tt.core.s -ef CPU/exceptions.s < tt.in`;
  `tt.le.s`, `tt.dir.s -ef`; `tt.alu.bare.s`, `tt.fpu.bare.s -bare -noexception` (little-endian
  only); `tt.io.s -mapped_io`.
- **Expected-output `.OK` transcripts exist only in the deleted `BoneYard/`** (removed `f570a87`,
  2015). Restore with `git show f570a87^:<path>`:
  - `BoneYard/spim-6.4/Tests/tt.{alu.bare,bare,be,fpu.bare,le}.OK`
  - `BoneYard/spim-6.5/Tests/tt.OK` (+ `tt.s`) and the same five
  - `BoneYard/spiminst/dos/build/Tests/{allinsts.s,allinsts.in,allinsts.ok,alubare.ok,bare.ok,fpubare.ok,Tests.txt}`
  These are SPIM 6.5 (2003) transcripts: banners and handler messages will not match 9.1.24, so use
  them for instruction/exception semantics only. `allinsts.s` is not in today's tree (LEAD: a further
  oracle).
- Maintainer-era tests added: `tt.argv.s`, `tt.args-cmd.s` (`b00abdd`); `tt.read_char_eof.s`,
  `tt.read_int_eof.s` (`f0b80dd`); octal_escape, missing_main, parse_error, return_value,
  stderr_split, unaligned (`cda1886`); the meson suite, `run-test.sh`, `tt.divide_by_zero.s`,
  `tt.explain.expected` (`969d133`); `tt.listing.s` (`cff81dd`); `tt.pseudo.s` (`5c03489`);
  `tt.multifile*.s` (`1991422`); `tt.read_int_space_sep.s` (`838f1ee`); disasm (`0cb3614`); the AST
  goldens + `gen-parity-golden.sh` (`dd04f26`). `tt.explain.expected` was re-pinned 11 times
  (`18004dc` … `0cb3614`) — all fork-invented output. The AST goldens have not been re-pinned since
  `dd04f26`.

### 1.6 Upstream behavior history a port author should know (VERIFIED by the history reader)

Defaults (`6cb6a08:spim/spim.cpp` ~150): bare=false, delayed_branches=false, pseudo=true, quiet=false,
mapped_io=false; `-bare` implies delayed branches; `SPIM_EXCEPTION_HANDLER` env (`405026b`, 2006),
`;`-separated list (`fc5ebc1`). Memory layout stable since at least 7.4 (TEXT_BOT 0x400000, DATA_BOT
0x10000000, STACK_TOP 0x80000000, K_TEXT 0x80000000, K_DATA 0x90000000, MMIO 0xffff0000; text/data
256K, kernel 64K). Output-changing fixes, newest first: `3d581e8` (2022, NULL write to empty text),
`79ee3a7` (2022, argv parsing), `d857e85` (2021, `-quiet` hides load message),
`eca0a41`/`1a102ea`/`ac627d3` (2020, FP cc beyond cc 0; pseudo FP registers removed from the register
dump), `8cdd4c6`, `0f0ed14`, `0df1e2a` (2019, `j` target mask), `d48acbb`, `013c778`,
`895a88c`/`d3b64e1` (`-dump`), `2230df7`, `5643311` (MOVF/MOVT), `a4d55c8` (FP compare cc),
`b2ce5c5`/`343856b`/`789f4c9` (data-segment print format), `75366f2`, `2ba524d` (`.space` growth),
`c25eed3`, `96618b6`, `2e49a94` (mapped IO), `46410c9`, `a2e0499`, `d6a5cbe` (argv stack alignment),
2009-09-22 exit-syscall value returned (ChangeLog only), `9ce3f96`, `5379830` (MSUB borrow),
`bbc9677` (MUL a real instruction), `04865ac`.

## 2. Docs ↔ code

| # | Sev | Location | Finding | Status |
|---|---|---|---|---|
| 2.1 | MEDIUM | `Documentation/spim.1:24` | documents `-execute file`, which does not exist | MAIN-VERIFIED |
| 2.2 | MEDIUM | `Documentation/spim.1` | `-quiet`/`-noquiet` descriptions inverted; exception address 0x80000080 (code: 0x80000180, `include/spim.h:129`); names the binary `spim` (it is `spimulator`); SEE ALSO cites xspim/qtspim; omits `-exception_file`, `-trap*`, `-pseudo/-nopseudo`, `-assemble`, `-dump/-full_dump`, every short alias, the positional-file form, exit codes, REPL commands, `SPIM_EXCEPTION_HANDLER`, the history file | VERIFIED |
| 2.3 | LOW | `src/spim.c` usage + help text | usage omits size flags, `-pseudo`, `-trap*`, short forms; help shows `l(oad)` but `load` needs two characters (`l` → "Unknown spim command") | VERIFIED |
| 2.4 | LOW | `src/README` | cites a Makefile that no longer exists; copyright 1990-2010 vs banner 1990-2023 | VERIFIED |
| 2.5 | LOW | comments | `src/spim.c:770,799` mention flex; `include/spim.h:162,185` xspim/X; `src/spim.c:1739` says data.asm for text.asm; `explain.c:1700-1703,2495` "subsequent times get the short form" (no such state); `include/explain.h:9-14` documents levels 0–3 (no L4); `instruction.c:708-711` "ENCODING is always 0" (filled at :183); `instruction.c:543`, `opcodes.h:29` "380-row" (381 rows); `ast.c:563-568` JSON comment says `{"value":…}`, code emits `{"offset","symbol","pc_relative"}`; `registers.h`/`run.c` "Can't be register" K&R notes at LWL/LWR/SWL/SWR; `spim.h:121-130` claims a macro is needed for `#ifdef MIPS1` (a `constexpr` inside `#ifdef` works) | VERIFIED |
| 2.6 | LOW | `CLAUDE.md:61` | "Fedora-44"; `Dockerfile:1` is `fedora:45` | MAIN-VERIFIED |
| 2.7 | MEDIUM | `tasks/reference/mips-fpu-and-float-demos.md:39` | says `cvt.w.d` "honors the rounding mode, default round-to-nearest". True of real MIPS; spim's `cvt.w.*` **truncates** (`src/run.c:1268`, `(int32_t)FPR_D(...)`). The demo advice (use `trunc.w.d`) still holds | MAIN-VERIFIED |
| 2.8 | LEAD | `tasks/reference/mips-fpu-and-float-demos.md:112-120` | "asm error-exit status is not propagated" looks stale: since `cda1886` `main` returns `spim_return_value` and the exit-demo/atexit-demo tests pin a non-zero asm status | LEAD |
| 2.9 | LOW | `tasks/codebase-cleanup-plan.md` §A4 | says `include/spim-syscall.h` lacks include guards; it has them, and is included nowhere (§4.9) | VERIFIED |
| 2.10 | LOW | test file comments | `tests/tt.listing.s:2-3` cites `tt.listing.expected_re` (does not exist); `tests/tt.args-cmd.s:2` cites `tasks/repl-args-command.md` (now `tasks/archive/2026/07/07/repl-args-command.md`) | VERIFIED |

## 3. Correctness bugs found in the core (not C23 work; for follow-up tasks)

Ordered by severity. "Inherited" = present in upstream `6cb6a08`.

| # | Sev | Location | Bug (failing input) | Status |
|---|---|---|---|---|
| 3.1 | HIGH | `src/spim.c:1389-1394` | `print_reg_from_string`: copy loop stops at `s1 - s < 100`, then `*s1 = '\0'` writes `s[100]` — one byte past the stack array. Input: REPL `print <100+ char identifier>` | MAIN-VERIFIED |
| 3.2 | HIGH | `src/spim.c:758-783` | `top_level`: `repl_line`, `repl_buf`, `repl_fp`, `scanner_pushed`, `redo` are non-`volatile` automatics modified after `setjmp` and read in the landing pad — indeterminate after a Ctrl-C `longjmp` (C23 §7.13.2.1) | MAIN-VERIFIED |
| 3.3 | HIGH | `src/memory.c:238-240` | `expand_k_data` zeroes bytes `[old/4, new/4)` through an `int8_t*` — wipes live kernel-data bytes and leaves the new tail uninitialized (`expand_data` does it right) | MAIN-VERIFIED |
| 3.4 | HIGH | `src/run.c:405-415` | `divu` reuses `div`'s signed INT_MIN/-1 guard: `divu 0x80000000, 0xffffffff` leaves HI/LO unchanged instead of LO=0, HI=0x80000000 | MAIN-VERIFIED |
| 3.5 | HIGH | `src/symbol-table.c:227` | forward data references write `sym->addr` as a full word: forward `.word lab+4` drops the `+4`; forward `.byte lab`/`.half lab` overwrites 4 bytes. Backward references are correct | MAIN-VERIFIED (read) |
| 3.6 | HIGH | `src/explain.c:422-447` | explain-mode memory peeks restore only `exception_occurred` and `CP0_BadVAddr`; a peek of an unaligned address (`explain_before` :1680-1682 does `peek_word` for `sb $zero,1($t0)`) raises AdEL and leaves `first_bad_exception`, EPC, Cause, EXL set — exit 132 for a program that is correct at `-explain=0`. A second cause beside bug 1 of `tasks/explain-mode-correctness-bugs.md` | VERIFIED (trace) |
| 3.7 | HIGH | `src/instruction.c:1352-1356,1367-1369` | inherited: decoding c.cond.fmt and movf/movt/movn/movz.fmt passes fd into the RD slot, but `FS()` aliases `RD()` (`instruction.h:73`) — the fs register is lost. Reached via `memory.c:458` (any word stored into text). `.word 0x4604103c` decodes and executes as `c.lt.s $f0,$f4` (encoding says `$f2`) | VERIFIED (trace) |
| 3.8 | MEDIUM | `src/instruction.c:1224-1226,763-765` | `movn.s`/`movz.s` encode `REGS(CC(inst),18)` with `CC = RT>>2`: `movn.s $f0,$f2,$t1` encodes rt=8 and prints `2` | VERIFIED (trace) |
| 3.9 | MEDIUM | `include/opcodes.h` | inherited: six duplicate encodings (swxc1/round.l.s 0x46000008, sdxc1/trunc.l.s 0x46000009, suxc1/trunc.w.s 0x4600000d, prefx/floor.w.s 0x4600000f, lwxc1/madd.s 0x4c000000, ldxc1/madd.d 0x4c000001); `bsearch` (since `c77862d`) returns an unspecified one, so disassembly of those words is libc-dependent; COP1X decode (`instruction.c:1282-1284`) never ORs in funct. Fix needs the `BUILD_TREE_SITTER=1` gate | MAIN-VERIFIED (0x4600000d, 0x4600000f rows) |
| 3.10 | MEDIUM | `include/instruction.h:97` | `BRANCH_OFFSET` = `sign_ex((IOFFSET<<2))` wraps at 16 bits: a decoded imm with \|imm\| ≥ 0x2000 gives the wrong target (upstream's `IDISP`, `6cb6a08:CPU/inst.h:116`, gave +65536 for 0x4000; real MIPS too). Changed in `a1f0fe3`. The assembler only emits \|imm\| < 0x2000 because it also truncates the byte offset to 16 bits (`symbol-table.c:243`, inherited from `6cb6a08:CPU/sym-tbl.cpp:262`), so a branch > 32 KB away assembles silently wrong | MAIN-VERIFIED |
| 3.11 | MEDIUM | `src/run.c:86,82` | with `-delayed_branches`, an exception in a delay slot never reaches the handler (JUMP_INST overwrites PC after `handle_exception`), and an exit syscall in a delay slot is ignored | VERIFIED (read) |
| 3.12 | MEDIUM | `src/run.c` | `run_error` from inside a delay slot leaves `running_in_delay_slot = 1`; later exceptions get EPC-4 + BD | LEAD |
| 3.13 | MEDIUM | `src/run.c:1619-1620`; `src/memory.c:428-443` | signed-overflow UB: `signed_multiply` `LO += 1` on int32 (`mult 0x80000000, 1`); `0xff << 24` / `0xffff << 16` on `int` for `sb`/`sh` into text | VERIFIED (read) |
| 3.14 | MEDIUM | `src/instruction.c:100,104`; `src/data.c:76` | `1u << alignment` unbounded: `.align 32` / `.align -1` is UB (would trip the UBSan-trap gate) | VERIFIED (read) |
| 3.15 | MEDIUM | `src/spim.c:981,987,1198-1211` | REPL redo: blank line after `print $ra` calls `print_reg(32)` (reads `gpr[32]`); FP redo goes past `$f31`; breakpoint/read redo read `prev_token`, set only by PRINT; `breakpoint` with no argument still calls `add_breakpoint` | VERIFIED (read) |
| 3.16 | MEDIUM | `src/spim.c:492-506` | a size flag as the last argument does `atoi(argv[argc])` = `atoi(NULL)`; `-exception_file` last means "no handler" | VERIFIED (read) |
| 3.17 | MEDIUM | `src/spim-utils.c:256,262` | `addrs[10000]` overflows when environ + argc exceed 10000 entries | VERIFIED (read) |
| 3.18 | MEDIUM | `src/spim.c:1632` | `read_int_input` `v*10 + digit` overflows `long` on ≥ 20 digits | VERIFIED (read) |
| 3.19 | MEDIUM | `src/syscall.c:78-79,127,137` | `read_string` bounds-checks only the start address; `a1+a2-1` is signed addition and the ends may be in different segments | VERIFIED (78-79), LEAD (rest) |
| 3.20 | MEDIUM | `src/scanner.c:191` | `,` accepted as an FP decimal separator (upstream `9b0892d`), so `.word 1,2` (no space) lexes as FP 1.2 → "Expected expression". Untested | MAIN-VERIFIED (code) |
| 3.21 | MEDIUM | `src/symbol-table.c:345` | `find_symbol_address` calls `lookup_label`, which *inserts* the name; REPL queries create bogus undefined symbols; the `l == nullptr` check is dead | MAIN-VERIFIED |
| 3.22 | MEDIUM | `src/run.c` FPU | inherited: `round.w.*` is `(int32_t)(val+0.5)` (wrong for negatives and ties); out-of-range/NaN FP→int is C UB (x86 gives 0x80000000; MIPS 0x7fffffff); `ctc1` to FIR overwrites it; FCSR bad bits written before FPE (`run.c:1215`); `-delayed_loads` `ldc1` loses its first word (`run.c:1312-1316`); `ldc1 $f31` writes `fp_single_view[32]` past the allocation (LEAD) | VERIFIED (read) |
| 3.23 | LOW | `src/spim.c:1731,1756`; `src/spim.c:1691` | `fopen` of data.asm/text.asm not null-checked; `get_console_char` ignores `read`'s return | VERIFIED (read) |
| 3.24 | LOW | `src/symbol-table.c:362` | `%08x` with `long l->addr` | MAIN-VERIFIED |
| 3.25 | LOW | `src/instruction.c:72-97` | `enable_text_auto_alignment` is written (4 sites) and never read, so `.align 0` in text probably does not disable auto-alignment (`db67bc9`) | MAIN-VERIFIED (no reader) |
| 3.26 | LOW | `src/explain.c:1675-1686` | only sw/sh/sb snapshot memory: swl/swr/sc never print "memory at"/"Wrote to memory" | VERIFIED (read) |
| 3.27 | LEAD | `src/spim.c` | `run_error` longjmps to an unset `spim_top_level_env` during `-f` load, `-dump`, `-assemble`; SIGINT uses `setjmp`/`longjmp` (not `sigsetjmp`) from a handler that calls stdio | LEAD |

## 4. C23 lens — the pass-3 worklist

Already done by pass 1 (`tasks/archive/2026/05/22/c23-modernization.md`) and pass 2
(`tasks/archive/2026/10/16/c23-modernization-pass2.md`): `nullptr` (3 `NULL` left are in comments or
strings), ~85 `constexpr`, `enum : T` on every named enum, `[[nodiscard]]` on 41 functions,
`[[noreturn]]`, `static_assert` (1), `<stdbit.h>`, `<stdckdint.h>`, `#embed`, `typeof` macros, `= {}`
(2). Pass 2 **declined** `unreachable()` ("core `default:` cases are reachable on bad binary input")
and marked designated initializers "n/a" (pass2 doc line 70: "core zero-inits are dynamic
(xmalloc+memset)"). That second call is what this pass revisits: the dynamic sites are exactly where
`*p = (T){…}` applies. There is no `for (;;)` in the core.

### 4.1 Struct initialization (the maintainer's priority) — ~110 sites

| Area | Sites | Proposed form |
|---|---|---|
| `src/ast.c` | `new_node` (memset + field stores + redundant `n->next = nullptr` :42) and 20 constructors; redundant zero stores :105, :228, :246 | `*n = (ast_node){.kind=…, .source_line=…, .src_text=…}` in `new_node`; constructors assign `.u.x = (T){…}` |
| `src/symbol-table.c` | `lookup_label` :116-125 (six stores; `next_local` never initialized); `label_use` :168-179, :186-191 | compound literals |
| `src/instruction.c` | 9 `zmalloc`+field constructors (`i_type_inst` :202-208, `j_type_inst` :343-349, `make_r_type_inst` :360-368, `mk_r_inst`/`mk_co_r_inst`/`mk_i_inst`/`mk_j_inst` :1382-1434); `make_imm_expr` :969-979; `make_addr_expr` :1081-1099 | compound literals. Hazard: the `SET_*` accessor macros apply `(int16_t)`/`(uint8_t)` casts the literal needs explicitly under `-Wconversion`; one union member only; `r_cond_type_inst` sets COND after the constructor |
| `src/scanner.c` | 17 token fills `out->type=…; out->val.i=…; out->present=true;` (223 … 686); `tok_buf[i]` reset :120; push/pop snapshot :771-781/:794-804 | `*out = (scan_token){…}`; snapshot: hazard — arrays inside, keep `memcpy` unless the state is regrouped |
| `src/assembler-event.c` | 11 `asm_event e; fill_header(&e,…); e.u.x = …` | `asm_event e = {.kind=…, .source_line=…, .u.label_def={…}}` |
| `src/string-stream.c` | `ss_lazy_init`/`ss_init` field resets | `*ss = (str_stream){}` (keep the `open_memstream` call) |
| `src/spim-utils.c` | `add_breakpoint` :348-353 | compound literal |
| `src/spim.c` | `struct termios params; … params = saved_console_state;` :1645; `struct timeval timeout; …tv_sec=0; …tv_usec=0;` :1675; `main` re-assigning globals to their static defaults :412-423 | init at declaration; `= {}`; move defaults to the definitions (:287-300). Opt-out: `console_out.f = stdout` must stay a runtime assignment |
| `src/run.c` | `struct itimerval time;` :175 read even if `getitimer` fails; :1540-1550 four field stores | `= {}`; compound literal (address-taken: keep adjacent to the call) |

Opt-outs: `memset` of heap segments after `malloc`/`realloc` (`memory.c:101-203`) and of the global
register arrays on reinit (`spim-utils.c:144,148`) — arrays cannot be reassigned with `= {}`.

### 4.2 Initialize at declaration / declare at first use — ~60 sites

`src/symbol-table.c` ten top-of-function blocks (45, 67-70, 96, 108, 199, 319, 357, 370, 382, 396);
`src/scanner.c:442`; `src/parser.c:853, 861, 1294, 1391, 1404, 1752, 1964`; `src/pseudo-op.c:197`;
`src/instruction.c:262-265, 602-605, 623, 948, 1135-1145, 1265-1291`; `src/explain.c:495, 721`;
`src/run.c:130-133` (keep loop-carried `step_size`), LWL/LWR/SWL/SWR temporaries (511/515, 563/567,
920/924, 972/976), MADD/MSUB 619/667, `unsigned_multiply` 1573-1574; `src/memory.c:166, 181, 192,
363, 613`; `src/spim.c:892, 953-956, 1227-1233, 1356, 1437-1441, 1528-1532, 1560-1569, 1755`;
`src/spim-utils.c:102, 253, 367, 389, 401`; `src/display-utils.c:27, 39-42, 61-64, 104-107` (comma
expression if/else → ternary), 144, 197.

**Opt-outs (setjmp):** `main` and `top_level` in `src/spim.c` (the only `setjmp` users; :689, :768,
:841); keep their locals where they are (3.2 is the hazard). Also keep: assignments inside a
condition (`instruction.c:254, 305, 1083`), the `inst_to_string` order (`exception_occurred = 0`
first), address-taken out-params (`explain.c:1964-1966`).

### 4.3 `bool` for 0/1 ints — ~25 sites

`scanner.c:65` `allow_pseudo_ops`; `symbol-table.h` `resolve_uses`, `issue_undef_warnings`, the label
`unsigned x:1` flags; `data.c:27`, `instruction.c:55` `bool in_kernel = 0`; `instruction.c`
`value_known` params (:34, :45, :251, :326); `exception_occurred` (`instruction.h:118`, saved copies
in explain.c); `run.c:49` `running_in_delay_slot`, :1601 `neg_sign`, :1624 `set_fpu_cc` params;
`do_syscall` return (`syscall.h:6`); `spim.c:306, 407, 1046, 1346, 1529, 1561, 1609`;
`format_registers` flags (`spim-utils.h:234`); `spim-utils.c:368` `deleted_one`; `print_fp_reg`
always returns 1 → `void`.

### 4.4 `constexpr` / enums for remaining `#define` constants

`scanner.c:79` LINE_BUF_MAX, :747 SCANNER_STACK_DEPTH; `symbol-table.c:34,36`; `spim-utils.c:213`
MAX_ARGS (and the independent magic `10000` at :256); `display-utils.c:191` BYTES_PER_LINE;
`explain.c:189` MAX_SUGGESTIONS; `include/syscall.h:9-31` 17 syscall numbers → `enum spim_syscall :
int32_t`; `include/spim.h:119` `2147483647` → `INT_MAX`; `include/spim.h:112,116` string macros.
`include/spim-syscall.h` duplicates syscall.h and is included nowhere (`grep -rn spim-syscall src
include meson.build` → nothing).

### 4.5 Attributes and const-correct format strings

`[[noreturn]]` missing on the first declarations of `control_c_seen` (`spim.c:257`) and `top_level`
(`spim.c:276`) — C23 requires it on the first declaration (MAIN-VERIFIED). `[[nodiscard]]` candidates:
`write_assembled_code`, `read_input`, `read_int_input`, `do_syscall`, `mem_read_*`, `mem_reference`,
`inst_op_name`, `op_token_name`. `[[maybe_unused]]` for `(void)x` on callback parameters
(`spim.c:164, 195, 852` — libedit/signal signatures are externally defined: keep names).
`ss_printf`, `error`, `run_error`, `fatal_error`, `write_output` take `char* fmt` → `const char*`
with `[[gnu::format(printf, …)]]`.

### 4.6 Small syntax residue

`while (1)` → `while (true)` (`spim.c:766`); empty-body `while (…);` → `{}` (`spim.c:1279, 1371`);
`f != 0` / `== 0` on pointers → `nullptr` (`spim.c:1539`, `spim-utils.c:447, 456`); redundant
`(void*)malloc` cast (`spim-utils.c:445`); `return (x);` (symbol-table.c ×8, data.c:102,
instruction.c); stray `};` (`symbol-table.c:420`); `parser.h` included twice (`parser.c:16,20`);
block-scope prototypes in `parser.c` (24-29, 38-39, 504, 749, 902, 1299, 1356, 1376, 1801, 1866-1867),
`pseudo-op.c:20-21, 89-90`, `spim.c:265-270` → the headers (`scanner_advance`/`push_source`/`pop_source`
are missing from `scanner.h`); `return (0); /* Not reached */` after a `[[noreturn]]` call
(`instruction.c:1236`).

### 4.7 Function-like macros → `static inline`

Small and local: `SYMBOL_IS_DEFINED` (`symbol-table.h:31`), `DATA_PC` (`data.c:29`), `SIGN_BIT`
(`run.c:45`), `NaN(X)` → `isnan` (`run.c:6`), the five copy-pasted hex-digit decodes (`scanner.c:242,
286, 292, 371, 378`), `SET_FCC` (`registers.h:324`, an unguarded `if/else` — dangling-else hazard).
Large: the instruction-field accessors and `SET_*` (only one lvalue use, `SHAMT(i) = 0` at
`instruction.c:368`), `FPR_D`/`SET_FPR_*`, `LOAD_INST`/`JUMP_INST`/`BRANCH_INST`. Keep
`RAISE_EXCEPTION` a macro (its `MISC` argument injects `break`; recorded in pass 1).

### 4.8 Type punning → `memcpy` (strict aliasing)

`display-utils.c:72-89, 115-124` `*(int*)&fp_double_view[i]`; `parser.c:1422, 1441`; `data.c:235-257`;
`run.c` MFC1 1329, MTC1 1405, SDC1 1447/1454, SWC1 1478, `(reg_word*)&FPR_S` 1312/1314/1321;
`memory.c:29/34/40` `short*` views → `int16_t*`. The build has no `-fno-strict-aliasing`. The FP
register file overlays `float[32]`/`double[16]`/`int[32]` on one buffer (`registers.h:269-271`):
add `static_assert(sizeof(float) == 4 && sizeof(double) == 8)`, type `fp_int_view` as `int32_t*`.

### 4.9 Dead code (proof commands)

`grep -rnw <name> src include` shows only the definition (+ prototype) for: `extern_directive`,
`lcomm_directive` (data.c), `ast_make_error`, `scanner_next`, `asm_fire_align`,
`asm_fire_seg_change`, `initialize_stack` (spim-utils.c:217), `DIRECT_MAPPED`/`TWO_WAY_SET`
(spim.h:135-136), `HI_present`/`LO_present` (memory.c:17), `nPC` (registers.h:22), `TEXT_CHUNK_SIZE`;
`TEST_ASM` is never defined (`test_assembly`, `inst_cmp`); `MIPS1` never defined;
`src/dump-opcodes.c` is not in `meson.build` and does not compile (`mips_instruction` undefined, no
`<stdlib.h>`); the `shamt` else-branches after `& 0x1f` (`run.c:810, 865, 887`) and `addr <=
MM_IO_TOP` on a `uint32` cannot be false. `name_tbl`/`i_opcode_tbl` `qsort` calls
(`instruction.c:554, 1127`) are no-ops because tokens.h numbers TOK_* in opcodes.h row order (LEAD).

### 4.10 Declined again (with the reason)

`unreachable()` in `default:` — pass 2's reason still holds for decode switches on binary input; the
`switch (byte)` on `addr & 3` in run.c (536, 591, 944, 996) is the one place it is provably
unreachable. `auto`, `_BitInt`, digit separators, `gnu23` → `c23`: as pass 1. The designated
opcode-indexed table (`[s - TOK_ALIAS_DIRECTIVE] = {…}`) replacing `bsearch`: a structural change
to the X-macro consumer, better done with `tasks/opcodes-x-macro-redesign.md`.

## 5. Port lens — user-visible behavior a reimplementation must match

### 5.1 Command line (`src/spim.c:428-606`)

Long/short pairs: asm/a, bare/b, delayed_branches/db, delayed_loads/dl, exception/e,
noexception/ne, exception_file/ef, trap/t, notrap/nt, trap_file/tf, mapped_io/mio, nomapped_io/nmio,
pseudo/p, nopseudo/np, quiet/q, noquiet/nq, noexplain/nx, explain/x (also `=N`), stext/st, sdata/sd,
ldata/ld, sstack/ss, lstack/ls, sktext/skt, skdata/skd, lkdata/lkd, file/f; long-only `-assemble`,
`-print-ast`, `-print-ast-json`, `-show-expansion`, `-listing <file>`, `-dump`, `-full_dump`.
Semantics: `-bare` = bare + both delays + quiet (pseudo-ops rejected in `scanner.c:415`; mapped IO off
under bare, `run.c:136`); `-asm` resets bare + delays but not quiet; `-exception_file` takes a
`;`-list, `SPIM_EXCEPTION_HANDLER` read first, CLI wins; `-explain N` consumes the next argument only
if it is one digit 0–4 (`-explain 5` → level 3 and "5" is the program file); `-explain=7` prints an
error + usage and continues; size flags are `atoi`; an unknown flag prints `Unknown argument: X
(ignored)` + usage and continues; the first non-`-` argument is the program file **and** argv[0] and
stops flag parsing (everything after is program argv); with several `-f`, argv[0] is the last;
`-noexception` does not make `main` global (`spim-utils.c:125`).

### 5.2 Exit status (`cda1886`)

0 normal / REPL `exit`/`quit`/EOF / `-print-ast` / `-dump`; `exit2` → `$a0`; `__start` passes `main`'s
`$v0` to exit2 (a `main` that never sets `$v0` exits with junk); 1 undefined symbols (run skipped);
2 parse error or unopenable file; 128+ExcCode of the first fault (breakpoint/syscall/interrupt
excluded; an explicit non-zero exit2 wins); divide-by-zero raises Bp (9) → exits 0; 255
`fatal_error`.

### 5.3 Streams, banner, REPL

Program output on stdout; spim's messages, the REPL prompt (non-libedit), help and REPL output on
stderr. `Loaded: <embedded exceptions.s>` on stderr unless quiet (`#embed`, `2f9aef3`). The version
banner (`SPIM Version 9.1.24 of August 1, 2023 (final)` … `spim-utils.c:134-141`) prints only on
`reinitialize`. REPL minimum prefixes: 1 char print/step/help/continue/delete; 2 chars
exit/quit/run/args/read/load/breakpoint/list; `dis` disasm; `dump` exact; `dumpn`; `reinit`; `print_s`,
`print_a`; `?` and `.`. Blank line repeats the previous command (print advances 1 register / 2 FP / 4
bytes). Formats: `Reg %d = 0x%08x (%d)`; `FP reg %d = %g (double)` then `(single)`;
`PC|HI|LO|FCSR|Cause|EPC|Status|BadVAddr = 0x%08x (%d)`; `Breakpoint encountered at 0x%08x`;
`Dumped %d words starting at 0x%08x to file %s` (default `spim.dump`); register dump columns
`R%-2d (%2s) = %-10d`/`%08x`, FP `%#-13.6g` (`display-utils.c:26-137`). History file
`~/.spimulator_history`. LEAD: `li` and `break` tokenize as keywords, so they cannot be REPL
abbreviations.

### 5.4 Assembler

Lexing: `#` comment; `;` statement separator; `,` is whitespace (`add $t0 $t1 $t2` accepted);
integers decimal or `0x` hex only (no octal: `010` is 10), wrap mod 2^32 silently; `-` + digit is a
negative literal, so `8 -4` is INT INT and becomes a subtraction (`parser.c:492-497`); floats need
digits on both sides of `.`/`,`/`'` (3.20); an exponent may be empty; `.float`/`.double`/`li.s`/`li.d`
accept only FP literals (`.double 3` is an error); identifiers `[_.A-Za-z][_.A-Za-z0-9]*` cut to 255;
`$f<digits>` is not range-checked; `$32` falls back to an identifier; char escapes
`\a\b\f\n\r\t\\\"\'` and `\x`+2 hex, `'\0'` is an error; string escapes only `\n \t \" \NNN` (octal,
fixed by the fork — upstream decoded `\134` as `$`) and `\XHH`, `\\` yields two backslashes; one
label per line. Constants `X = expr` are substituted at scan time but recorded at emit time, so a
later `.space X` in the same file sees 0; `X = 0` counts as undefined. Expressions keep only the
first unresolved symbol; division by zero gives 0. Directives: auto-alignment (`.word`/`.float` 4,
`.half` 2, `.double` 8); `.align 0` disables data auto-align until the next `.data`/`.kdata`; user data
starts at DATA_BOT+64K with `$gp` at the midpoint (`data.c:51-60`); `.extern` allocates in normal
data (not the gp area); `.set` and `.file .loc .frame .mask .fmask .ent .end …` are swallowed;
`.lcomm .rdata .sdata .repeat .struct .verstamp .vreg .noalias` → "Directive not yet supported".
Pseudo-op expansions (`parser.c:1209-1707`, `pseudo-op.c`): `li` → `ori` (widened at emit to
`lui $at`+`ori` for forward symbols, single `lui` for a zero low half); `move` → `addu rd,$0,rs`;
`b`/`bal` → `bgez`/`bgezal $0`; `bgt`/`ble` imm use `slti $at,rs,imm+1`; 3-register `div`/`rem` insert
`bne rt,$0,+3; nop; break 0`; `ror`/`rol` imm pass a negative shamt; `ulw`/`ush`/`ulh`/`usw` emit
little-endian sequences only. Messages: `spim: (parser) %s on line %d of file %s`; range errors
`immediate value (%d) out of range (%d .. %d) on line %d` are non-fatal and the code is widened. MIPS32r2
forms (`madd.s`/`msub.s`/`nmadd.s`/`nmsub.s`, probably `ext`/`ins`) are a parse error in the hand
parser (bison accepted them with a warning) — LEAD. Symbol dump order is hash-bucket order
(`h = len; h = h*613 + (unsigned)c`, `& (2^30-1) % 8191`, LIFO chains): a port must reproduce the hash
to match `print_symbols`.

### 5.5 Execution engine

ALU: add/addi/sub trap Ov (`ckd_*`), addu/subu wrap; div/divu by zero (and div INT_MIN/-1, and divu's
bug 3.4) leave HI/LO unchanged, no trap; `mul` clobbers HI/LO; madd/msub 64-bit; shift amounts masked
to 5 bits; sra arithmetic; andi/ori/xori zero-extend, slti/sltiu and trap-immediates sign-extend.
Loads: lb/lh from **text** zero-extend (`memory.c:376,385`); on AdEL/DBE the destination register is
written 0 (not preserved); ldc1/sdc1 check 4-byte alignment only. Branches: in default (non-delayed)
mode the encoded offset is relative to the branch's own PC, not PC+4 (`symbol-table.c:246`,
`run.c:63-66`); likely-branches nullify even without `-delayed_branches`; `eret` has a delay slot
under `-delayed_branches` (`run.c:421`). FPU: host IEEE arithmetic; `cvt.w.*` truncates; NaN pattern
is x86's, so `print_float` of 0/0 prints `-nan`; odd double register → `run_error` (abort, not an
exception). Exceptions: C `handle_exception` prints `Exception occurred at PC=…` to stderr (typo
"Coprocessor unuable" is part of the output) and jumps to 0x80000180; EPC is set only when EXL=0
but ExcCode is overwritten by a nested exception; bad PC fetch raises IBE; `exceptions.s` tests
`Cause == 0x18` on the whole register; the interrupt path adds 4 to EPC (inherited). Interrupts are
polled per 100-instruction chunk (`IO_INTERVAL`) only with `-mapped_io`; CP0 Count advances on a
10 ms wall-clock timer (`run.c:1525`, `TIMER_TICK_MS`) — nondeterministic; MMIO only at word-exact
0xffff0000/04/08/0c, latency 100 chunks. Memory: text 0x400000 (256K), data 0x10000000 (min 64K,
limit 1MB), stack top 0x80000000 (64K, limit 256K, grows by doubling within 16MB of stack_bot),
kernel text 0x80000000, kernel data 0x90000000, kernel segments accessible from user mode; stores to
text re-decode the instruction; executing an empty text slot is a `run_error` ("Attempt to execute
non-instruction"). Byte order follows the host at compile time (`meson.build:45-51`); a port should
fix little-endian.

### 5.6 Syscalls (`src/syscall.c`)

1 print_int; 2 print_float `%.8f`; 3 print_double `%.18g`; 4 print_string (unbounded); 5 read_int
(skip whitespace incl. newlines, optional sign, digits into `long`, truncated to 32 bits, one token
per call, `$a3` = 1 on EOF or non-numeric — `838f1ee`); 6/7 read_float/read_double via `atof` (no
error detection); 8 read_string (`$a3` = 1 on EOF); 9 sbrk (returns old data_top rounded to 4; 8-byte
alignment not guaranteed); 10 exit (0); 11 print_char; 12 read_char (0..255 zero-extended, -1 at EOF —
upstream returned `'\n'` on an empty read); 13/14/15/16 open/read/write/close with **host** flag and
mode values passed through (close can close spim's own fds); 17 exit2. Unknown syscall → `run_error`.

### 5.7 Disassembly and listing text (`src/instruction.c:621-800`)

`[0x%08x]\t0x%08x  <mnemonic operands>`, padded to byte column 57 (the tab counts 1; a breakpoint `*`
prefix counts), then `; ` and `"%d: %s"` of the raw source line. Register 0 prints `$r0`, others ABI
names (`a35d8e7`, `display-utils.c:16`); FP `$f%d`. Quirks pinned by goldens: `bc1f0 11992` (no space,
no comma), `bgez $r0 8` (no comma), immediates as signed int16 decimal even for ori/andi/lui (0xffff →
`-1`), branches print the byte displacement, J-type prints `0x%08x` of `target<<2` without the PC's
top nibble, all-zero RD_RT_SHAMT prints `nop`. Symbol suffix `[label-0x<PC>]` for PC-relative;
offsets with \|off\| ≤ 10 are not printed, larger as `+N (0x%08x)`. Unknown words
`<unknown instruction %d>`; empty slot `<none>`. Explain mode (`explain.c:124-175`, L1–L4) is
fork-invented, golden-pinned only at `-explain=2`, and has known correctness bugs
(`tasks/explain-mode-correctness-bugs.md`, plus 3.6): a port should treat its output as a spec to
fix, not an oracle.

## 6. Tests

| # | Sev | Location | Finding | Status |
|---|---|---|---|---|
| 6.1 | HIGH | `tests/run-test.sh:167-184`, `meson.build:133-151` | `tt.alu.bare.s` and `tt.fpu.bare.s` (upstream's bare-machine ALU/FPU torture tests) are never executed: not in `regression_tests`, and the `ast_parity_all` loop that lists them silently skips any program that writes no dump (`… || true` :180, `[ -f … ] \|\| continue` :184). `golden.ast_parity_all.txt` holds 17 programs, none of alu.bare/bare/fpu.bare | MAIN-VERIFIED |
| 6.2 | HIGH | `tests/run-test.sh:40` | the test named `bare` never passes `-bare`; bare-machine mode has no test | VERIFIED |
| 6.3 | MEDIUM | `tests/read.s`, `time.s`, `timer.s` | orphans (no reference in `run-test.sh`/`meson.build`); `read.s` is the only syscall-8 program; `tt.io.s` (`-mapped_io`) and `tt.be.s` (big-endian build) are only assembled | VERIFIED |
| 6.4 | MEDIUM | `tests/tt.core.s` | ~25 "Expect … exception message" lines (e.g. :420, :491, :1938-2093) and the float-print check (:1859) are never compared — sentinel tests check only the last line, and never the exit status | VERIFIED |
| 6.5 | MEDIUM | meson | the `#embed` default exception handler (the shipped path) is never tested: every run passes `-exception_file "$EF"` or `-noexception` | VERIFIED |
| 6.6 | MEDIUM | `explain_parity` | vacuous: it compares `-dump` output, and `-dump` never executes the program (`spim.c:679-684`), so explain mode cannot perturb what is compared | VERIFIED |
| 6.7 | LOW | `tests/run-test.sh:289-290` | `env -i "$SPIM" … \| grep -v` without `pipefail` masks spim's status (the diff still catches truncated output) | VERIFIED |
| 6.8 | LOW | stderr_split, exit-code tests | `$(...)` strips the newline the comment says is checked; return_value/unaligned/missing_main/parse_error check exit codes but no message text; `gen-parity-golden.sh:52` has the same silent skip and a hand-synced program list | VERIFIED |

**Coverage gaps, highest port risk first:** mapped IO and interrupts (no runtime test); `-bare`;
delayed branches/loads (only `bare`); syscalls 8 and 15 untested, 13/14/16 probably never executed
(the file-argument paths of cksum/head/wc are not the tested stdin paths — LEAD), 2/3 output
unchecked; flags never passed (`-bare -mapped_io -nopseudo -full_dump -print-ast-json -trap_file
-assemble -stext …`); REPL commands untested (`breakpoint continue delete step print print_all_regs
print_symbols dump dumpnative reinitialize list help`); explain levels 1, 3, 4; directives never
used (`.ascii .kdata .extern .comm .lcomm .set .rdata .sdata .repeat .struct .err`); no pinned
assembler diagnostics.

**Oracle quality.** Strong full-output oracles today: `explain` (full diff, deterministic only under
`env -i` because stack addresses depend on the environment), `ast_parity`/`ast_parity_all`
(assembled bytes; depend on loading the same `exceptions.s`), and the 34 examples demos (full
stdout+stderr diff of C and asm, inputs pinned) — the best differential oracle a port has. The
sentinel tests are self-checking semantics tests blind to formatting: capture their full output from
the C engine as new goldens before a port. `tt.core.s`'s timer test (:81-91) spins on CP0 Count, so a
port without a wall-clock timer hangs there; `time.s` is a benchmark.

## 7. Which oracle to trust (for a port)

- **Today's C:** exit codes, stdout/stderr split, octal escapes, read_int/read_char/EOF semantics,
  `fatal_error` text (upstream's corrupted its format string from `6f97322`, 2005, until `fb7338d`),
  the listing format, register names.
- **Neither (fix, then pin):** duplicate-encoding disassembly (3.9), FP→int edge cases and `round.w`
  (3.22), explain-mode exception handling (3.6), decode of c.cond/movf (3.7), divu (3.4), forward data
  references with addends (3.5), r2 FP instructions the hand parser drops (5.4).
- **Upstream `6cb6a08`:** the behaviors the fork did not touch; the grammar for any ambiguous syntax
  (§1.4).

## Suggested follow-ups

- [F1] **Required** — apply the §4 worklist in `tasks/c23-modernization-pass3.md` (struct init first;
  setjmp functions opted out) (§4.1–§4.8).
- [F2] **Required** — feed §5–§7 into `tasks/engine-port-language-investigation.md` (§5, §6, §7).
- [F3] **Recommended** — fix the memory-safety bugs: `print_reg_from_string` overflow, `expand_k_data`,
  REPL redo out-of-bounds, `addrs[10000]`, `atoi(NULL)`, `read_string` bounds, unchecked `fopen`,
  `.align` shift (§3.1, §3.3, §3.14–§3.17, §3.19, §3.23).
- [F4] **Recommended** — make `top_level`'s setjmp-live locals `volatile` (or `static`) and consider
  `sigsetjmp` (§3.2, §3.27).
- [F5] **Recommended** — fix wrong-value engine bugs, each with a regression test: divu guard,
  forward `.word lab+N`, c.cond/movf decode, movn.s/movz.s encoding, BRANCH_OFFSET reach, signed-UB
  in `signed_multiply` and text `sb`/`sh` (§3.4, §3.5, §3.7, §3.8, §3.10, §3.13).
- [F6] **Recommended** — explain-mode peeks must save/restore all exception state; fold into
  `tasks/explain-mode-correctness-bugs.md` (§3.6, §3.26).
- [F7] **Recommended** — wire `tt.alu.bare.s`/`tt.fpu.bare.s` as `-bare -noexception` tests, make the
  `bare` test pass `-bare`, make the parity loop fail on a missing dump, pin `tt.core.s`'s full output
  as a golden, add one default-handler (no `-ef`) test, fix or delete `explain_parity` (§6.1, §6.2,
  §6.4, §6.5, §6.6).
- [F8] **Recommended** — resolve the six duplicate encodings in `opcodes.h` (gate with
  `BUILD_TREE_SITTER=1`) (§3.9).
- [F9] **Optional** — rewrite `Documentation/spim.1` from the actual flag table; fix usage/help text,
  `src/README`, `CLAUDE.md` Fedora version, the FPU reference doc's two claims, and the stale
  comments (§2.1–§2.10).
- [F10] **Optional** — delete the dead code listed in §4.9 (and `src/dump-opcodes.c`, or fix and
  build it) (§4.9).
- [F11] **Optional** — wire the orphan tests where they can run deterministically (`read.s` with
  stdin; `tt.io.s` with `-mapped_io` and scripted input) and recover upstream `allinsts.s` from
  `f570a87^` as an extra oracle (§6.3, §1.5).
- [F12] **Optional** — decide `,`/`'` as FP decimal separators (`.word 1,2`) and the other assembler
  quirks a port would otherwise copy (§3.20, §3.21, §3.25, §5.4).

## Open questions for the maintainer

1. Should `,` stay an FP decimal separator (upstream `9b0892d`, 2015), given it makes `.word 1,2`
   (no space) a syntax error? Recommendation: drop `,` and `'`, keep `.` (§3.20).
2. Should the port match today's bug-compatible behaviors (divu guard, forward `.word lab+4`, branch
   reach) or should the C be fixed first so the oracle is correct? Recommendation: fix the C first
   (F5), then port against the fixed C (§7).
