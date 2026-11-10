# C23 modernization pass 3 — make the core as clean as C23 allows before any port

**Priority:** 3
**Difficulty:** 6
**Created:** 2026-11-10 (William Emerison Six <billsix@gmail.com>)
**Completed:** 2026-11-10
**Branch:** `c23-pass3-and-port-study`
**Source:** tasks/review/2026/11/10/repo-review.md §4.1, §4.2, §4.3, §4.4, §4.5, §4.6, §4.7, §4.8 F1

## Outcome

`src/` + `include/` took the audit record's C23 worklist (§4) in seven commits, one idiom group each,
struct initialization first. Behavior is unchanged: every commit passed the image's build, the
regression and examples suites, and the UBSan-trap and ASan regression runs, on clang (the image
compiler) and gcc, both `-Werror` under `-Wconversion -Wsign-conversion`. `opcodes.h` and
`opcode-types.h` were not touched, so the `BUILD_TREE_SITTER=1` rule did not apply.

| # | Idiom group | What changed |
|---|---|---|
| 1 | Struct initialization | `ast.c` constructors build the node as one compound literal passed to `new_node` (which stamps the source position; the `memset` is gone); `assembler-event.c`'s 11 fire helpers build an `asm_event` literal (`fill_header` became `fire`); `lookup_label`, `label_use` records, `make_imm_expr`, `make_addr_expr`, `add_breakpoint`, the scanner's 17 token fills, `str_stream` resets, `itimerval`/`timeval`/`termios` locals; flag defaults moved from `main` to their definitions |
| 2 | Initialize at declaration / declare at first use | ~60 sites across `symbol-table.c` (ten top-of-function blocks), `parser.c` (`copreg` ×4, `parse_i2`), `instruction.c`, `run.c` (`next_step`, the LWL/LWR/SWL/SWR words, MADD/MSUB, `unsigned_multiply` as `const` limbs), `memory.c`, `spim.c` (incl. `write_output`'s duplicated stdout branch), `spim-utils.c`, `display-utils.c` |
| 3 | `bool` for 0/1 ints | `exception_occurred`, `do_syscall`, `record_label`/`flush_local_labels` flags, the label bitfields, `check_keyword`, `value_known`, `running_in_delay_slot`, `neg_sign`, console-state flags, `format_registers`, `str_prefix`, `deleted_one`; `print_fp_reg` → `void` |
| 4 | `constexpr` / enums | syscall numbers → `enum spim_syscall : int32_t`; `EXCEPTION_ADDR` → `constexpr` inside its `#ifdef`; `INT_MAX`; `MAX_ARGS`, `HASHBITS`, `LABEL_HASH_TABLE_SIZE`, `LINE_BUF_MAX`, `SCANNER_STACK_DEPTH`, `BYTES_PER_LINE`, `MAX_SUGGESTIONS`, the explain `UK*` strings |
| 5 | Attributes, const format strings, residue | `const char* fmt` + `[[gnu::format(printf, …)]]` on `error`/`fatal_error`/`run_error`/`write_output`/`ss_printf`; `[[noreturn]]` on the first declarations of `control_c_seen`/`top_level`; `[[nodiscard]]` on `do_syscall`, `read_int_input`, `write_assembled_code`, `inst_op_name`, `op_token_name`; `[[maybe_unused]]` for callback parameters; `while (true)`, `{}` loop bodies, `nullptr` comparisons; block-scope and duplicated prototypes moved into `scanner.h`/`parser.h`; the duplicate `parser.h` include |
| 6 | Macros → `static inline`; type punning → `memcpy` | `NaN` → `isnan`; `SIGN_BIT`, `DATA_PC`, `CC_mask`/`FCC`/`SET_FCC` (the last was an unguarded `if/else` macro), a `hex_digit_value` helper for five copies; `memcpy` bit casts for MFC1/MTC1/SDC1/SWC1, `.double`/`.float` storage, `li.d`/`li.s`, the FP register display; `fp_int_view` is `int32_t*`; `static_assert` on the FP views' sizes |
| 7 | setjmp hazard | `top_level`'s locals that change after `setjmp` and are read in its landing pad are `volatile` (audit §3.2); both setjmp functions carry an in-code note that their locals are deliberately left in place |

The `[[gnu::format]]` attributes surfaced six call sites that were type-mismatched or passed a
non-literal format with no arguments; each was fixed behavior-preservingly (`"%s"` for the
undefined-symbol list and the register-display fill strings, `(char*)` casts for `void*` string
arguments, an explicit `(unsigned)` for `long l->addr` printed with `%08x`).

## Opted out (with the reason)

- **`mips_instruction` decode constructors** (`mk_r_inst` and siblings, `i_type_inst`, `j_type_inst`):
  kept `zmalloc` + the `SET_*` accessors. The accessors are the one place that maps MIPS field names
  onto the struct's overlapping unions (FS lives in the rd slot, FD in shamt), so a designated
  initializer would restate that mapping at every site. Noted in-code above `mk_r_inst`.
- **`FPR_S`/`FPR_D`/`SET_FPR_*` stay macros:** `LOAD_INST((reg_word*)&FPR_S(...))` takes the address
  of an `FPR_S` for the delayed-load slot, so it cannot be a function; the `SET_*` brace-block macros
  carry an implicit conversion a function signature would change.
- **The instruction-field accessors** (`RS()`, `SET_RD()`, …): left as macros; turning ~40 accessor
  pairs into functions is a large mechanical change better done with `tasks/opcodes-x-macro-redesign.md`.
- **Scanner push/pop snapshot:** still field-by-field; the state is separate file-scope globals
  including arrays, so a compound literal does not apply without regrouping the state.
- **`memset` of heap segments and the global register arrays**: arrays cannot be reassigned with `= {}`.
- **`expand_k_data`**: untouched, because it carries audit bug §3.3 for its own fix.
- **`main`'s locals**: untouched (setjmp function).
- **String macros** `DEFAULT_RUN_LOCATION`, `END_OF_TRAP_HANDLER_SYMBOL`: a `constexpr char[]` would
  be `const` and the consumers take `char*`; needs const-correct `find_symbol_address` first.
- **`unreachable()`, `auto`, `_BitInt`, digit separators, `gnu23` → `c23`**: declined as in pass 1/2
  (audit §4.10).
- **`read_input` `[[nodiscard]]`**: two syscall paths ignore its result on purpose.
- **explain.c if-chains** (`say_wrote_mem` etc.): already read better than a nested ternary would.

## Verification

- Each group: local clang + gcc builds and the 59-test `meson test`; then the image's stages — build,
  install, both meson suites, the UBSan-trap and ASan regression runs, and `make -C examples/src all
  debug` — run by `podman run` against the `make image`-built image, on a snapshot of exactly the
  committed tree (`git checkout-index`). Running the stages in a container instead of a fresh
  `podman build` per group follows the nested-run-and-gates.md reference doc from
  github.com/billsix/runClaudeInContainer: nested layer commits under fuse-overlayfs took minutes each.
  Group 1's snapshot also passed a full `make image` (59 tests; 25 regression tests under each
  sanitizer), as did the pre-pass tree (the baseline).
- Not covered beyond the suites: mapped IO, `-bare`, delayed branches outside `tt.bare.s`, and the
  REPL commands the audit lists as untested (§6). The changes there are mechanical; no runtime claim
  is made for those paths.
- No `CHANGELOG` entry: the repo's `ChangeLog` has not been kept since 2026-05-25 and none of the
  C23 passes changed behavior.

## Decisions

- The C-standard items of `tasks/codebase-cleanup-plan.md` move into this task; its non-C23 refactors stay there (William Emerison Six <billsix@gmail.com>, 2026-11-10).
- The `volatile` fix in `top_level` was included here, not deferred to a bug task: it is the hazard
  the setjmp opt-out rule exists for, and it changes no output.
