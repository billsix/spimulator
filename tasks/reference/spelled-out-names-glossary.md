# Spelled-out names: the C core's glossary

**Reference document.** Read it before naming anything new in `src/` or `include/`, and before
the port tasks that carry the same names into `port/python`, `port/haskell` and `port/rust`
(`tasks/archive/2026/11/14/spelled-out-names-python-port.md`,
`tasks/archive/2026/11/14/spelled-out-names-haskell-port.md`,
`tasks/spelled-out-names-rust-port.md`). Update it in place when a name is added or a decision
changes. Written 2026-11-14 (William Emerison Six <billsix@gmail.com>) by the rename recorded in
`tasks/archive/2026/11/14/spelled-out-names-c-core.md`.

## The rule

A name in the simulator core is written out in full, so a new student can read the code without
a decoder ring: `general_purpose_registers`, not `gpr`; `immediate_expression`, not `imm_expr`.
Short names came from an era when every name was typed by hand. Autocomplete and LLMs make long
names cheap to write, and they are always cheap to read. The C coding standard's "Naming and
comments" section is the general form of the rule; this doc is the core's vocabulary.

## What stays short, and why

- **What the student types in assembly.** Instruction mnemonics and directives stay as they are
  inside a name: `TOKEN_ADDIU_OPCODE`, `TOKEN_ASCIIZ_DIRECTIVE`. Only the `TOK` prefix was spelled
  out. Text the program prints or reads is never renamed: messages, teaching-mode output, `$sp`.
- **Names defined outside the core**: `main`, `argc`, `argv`, `environ`, `fd_set`, libc and
  libedit functions, the syscall names (`PRINT_INT_SYSCALL` mirrors SPIM's `print_int`).
- **Familiar acronyms** (decision 3): `AST`, `ASCII`, `ASAN`, `EOF`, `IO`, `JSON`, `MIPS`, `SPIM`,
  `REPL`, `ms`, `syscall`. Also `MDMX`, the extension's own name.
- **Tight-scope names**: loop indices and other one-letter locals (`i`, `n`, `s`, `p`).
- **Math-style names** (decision 5): the long-hand multiply in `unsigned_multiply` (`src/run.c`)
  keeps `a`, `b`, `c`, `d`, `ac`, `ad`, `cb`, `bd` and `mid`.
- **`pseudo-op`**: the assembler's own term, which teaching mode prints, so `PSEUDO_OP` stays.
- **English words used as names**: `at`, `in_kernel`, `text_word_at`.

## Conventions the rename set

- **Architecture terms are spelled out by meaning, with the manual's name in a comment** (the
  maintainer's `gpr` example, decisions 1 and 2): `coprocessor_0_status /* CP0 Status */`,
  `stack_pointer_register /* $sp */`.
- **Coprocessor 0 and the floating-point control registers**: a register accessor reads like a
  variable, so it is lower case (`coprocessor_0_cause`, `floating_point_control_status_register`).
  Register numbers, masks and bit fields are upper-case constants
  (`COPROCESSOR_0_CAUSE_REGISTER_NUMBER`, `COPROCESSOR_0_STATUS_EXCEPTION_LEVEL`). Field constants
  drop a trailing "register" (`FLOATING_POINT_CONTROL_STATUS_CAUSE_INEXACT`).
- **Exception codes** are `EXCEPTION_CODE_<meaning>`: `EXCEPTION_CODE_ADDRESS_ERROR_LOAD` (AdEL),
  `EXCEPTION_CODE_ARITHMETIC_OVERFLOW` (Ov).
- **The instruction fields** follow the manual's roles: `rs` is `source_register`, `rt`
  `target_register`, `rd` `destination_register`, `shamt` `shift_amount`, `funct`
  `function_field`; for floating point `fs`, `ft`, `fd` and `fr` are the source, target,
  destination and addend.
- **The operand-shape tags** spell the operand list left to right
  (`TARGET_REGISTER_SOURCE_REGISTER_IMMEDIATE_INSTRUCTION` is `addiu rt, rs, imm16`). The
  floating-point tags say `FLOATING_POINT` once and then the field roles
  (`FLOATING_POINT_DESTINATION_SOURCE_TARGET_INSTRUCTION`). The tree-sitter grammar keeps the short
  category names in `include/opcode-types.tsv`'s fourth column.
- **The assembler's location counters are `..._location`, not `..._program_counter`**
  (`current_data_location`, `next_text_location`). They are the address where the next datum or
  instruction is placed, not the CPU's program counter, which is `PROGRAM_COUNTER`; naming both
  "program counter" would invite a student to confuse them (William Emerison Six
  <billsix@gmail.com>, 2026-11-14).
- **The allocation wrappers** are `allocate_or_exit`, `reallocate_or_exit` and
  `allocate_zeroed_or_exit` (decision 4), with the GNU-style name (`xmalloc`) in a comment beside
  each definition.
- **A word with two meanings is decided per name**: `fp` is floating point, but a `FILE*` named
  `fp` became `file`; `op` is `opcode`, except the operator characters of the constant-expression
  parser, which are `operator`; `hi`/`lo` are `high`/`low`, except the explain snapshot's copies of
  HI and LO, which are `multiply_divide_high`/`multiply_divide_low`.

## In the ports

The ports carry the same names in their own casing (`general_purpose_registers` in Python,
`GENERAL_PURPOSE_REGISTER_COUNT` as a constant). What each port added or kept differently:

- **Python** (`tasks/archive/2026/11/14/spelled-out-names-python-port.md`):
  - `cpr` and `ccr` are `coprocessor_registers` and `coprocessor_control_registers`;
  - `cpu`/`CPU` (the register-file object) keep their names;
  - the `instruction` module is imported as `instruction_module`;
  - `console.py`'s `fd` is a file descriptor and stays;
  - `Kind` members print the C engine's spelling through `printed_name`.
- **Haskell** (`tasks/archive/2026/11/14/spelled-out-names-haskell-port.md`):
  - the record prefixes are spelled out by their record: `scanState` (not C's string stream),
    `immediateExpression`, `instruction`, `label`, `option`, `Event` and `Value`;
  - `len` and `err` are `byteLength` and `errorOccurred`, since `length` and `error` are
    Prelude's;
  - `Output.hs`'s `fd` is a file descriptor and stays.

The decisions are in the archived task's Decisions section. The full per-name table is
`tasks/adhoc/spelled-out-names-c-core/data/renames.tsv` until the port tasks are done.

## The words

Each abbreviated word the core used, and how it is spelled now. Meanings come from the code and
its comments; the CP0 and FPU field meanings were checked against NetBSD's `mipsNN.h` and the
MIPS32 manuals (sources in the archived task).

| Word | Meaning | Spelled as |
| --- | --- | --- |
| `abs` | absolute value | `absolute` |
| `absoff` | absolute value of an offset | `absolute_offset` |
| `addl` | additional | `additional` |
| `addr` | address | `address` |
| `addrexpr` | address expression | `address_expression` |
| `addrs` | addresses | `addresses` |
| `ae` | assembler event | `assembler_event` |
| `ar` | architecture revision (Config bits 12:10) | `architecture_revision` |
| `arg` | argument | `argument` |
| `args` | arguments | `arguments` |
| `arith` | arithmetic | `arithmetic` |
| `arr` | array | `array` |
| `asm` | assembly / assembler | `assembler or assembly per name` |
| `at` | AT: Config architecture type (bits 14:13) | `architecture_type` |
| `be` | BE: Config big-endian bit (bit 15) | `big_endian` |
| `bin` | binary (radix 2) | `binary` |
| `bkpt` | breakpoint | `breakpoint` |
| `bkptrec` | breakpoint record | `breakpoint_record` |
| `bkpts` | breakpoints | `breakpoints` |
| `bot` | bottom (lowest address of a segment) | `bottom` |
| `bp` | Bp: breakpoint exception code | `breakpoint` |
| `buf` | buffer | `buffer` |
| `cat` | category | `category` |
| `cc` | floating-point condition code (the cc field) | `condition_code` |
| `ccfp` | the cc field of a floating-point branch or compare | `floating_point_condition_code` |
| `cmd` | command | `command` |
| `cmds` | commands | `commands` |
| `cmp` | compare / comparison | `compare or comparison per name` |
| `co` | coprocessor | `coprocessor` |
| `cond` | condition | `condition` |
| `cons` | prepend to a list (Lisp cons) | `prepend` |
| `const` | constant | `constant` |
| `cont` | continue | `continue` |
| `copreg` | coprocessor register | `coprocessor_register` |
| `coproc` | coprocessor | `coprocessor` |
| `cp` | CP0/CP1: coprocessor 0/1 | `coprocessor_0, coprocessor_1` |
| `cpu` | CpU: coprocessor-unusable exception code | `coprocessor_unusable` |
| `ctrl` | control | `control` |
| `ctx` | context | `context` |
| `cu` | CU: Status coprocessor-usable bits | `coprocessor_usable` |
| `cur` | current | `current` |
| `dasm` | disassembly | `disassembly` |
| `dbe` | DBE: bus error on a data reference | `data_bus_error` |
| `def` | defined / definition | `definition` |
| `dir` | directive | `directive` |
| `dis` | disassembly | `disassembly` |
| `disasm` | disassembly / disassemble | `disassembly` |
| `dist` | distance | `distance` |
| `div` | divide | `divide` |
| `dn` | length of the disassembly string | `disassembly_length` |
| `dst` | destination | `destination` |
| `dt` | directive token | `directive_token` |
| `dumpnative` | dump in native (host) byte order | `dump_native` |
| `dup` | duplicate | `duplicate` |
| `dv` | the double-precision value of an operand | `double_value` |
| `ea` | effective address | `effective_address` |
| `el` | AdEL: address error on a load or fetch | `address_error_load` |
| `enc` | encoding (the 32-bit instruction word) | `encoding` |
| `env` | environment | `environment` |
| `epc` | EPC: exception program counter | `exception_program_counter` |
| `eq` | equal | `equal` |
| `err` | error | `error` |
| `es` | AdES: address error on a store | `address_error_store` |
| `eval` | evaluate | `evaluate` |
| `exc` | exception | `exception` |
| `excode` | exception code (Cause bits 6:2) | `exception_code` |
| `exl` | EXL: Status exception-level bit | `exception_level` |
| `expr` | expression | `expression` |
| `exprs` | expressions | `expressions` |
| `ext` | extension | `extension` |
| `fa` | $fa0: the first floating-point argument register (convention) | `floating_point_argument_register` |
| `fcc` | floating-point condition code(s) in FCSR | `floating_point_condition_code` |
| `fcr` | FCR: floating-point control register | `floating_point_control_register` |
| `fcsr` | FCSR: floating-point control and status register | `floating_point_control_status_register` |
| `fgr` | floating-point general register | `floating_point_general_register` |
| `fir` | FIR: floating-point implementation register | `floating_point_implementation_register` |
| `fld` | field | `field` |
| `fmt` | format | `format` |
| `fn` | function field (funct) | `function_field` |
| `fname` | file name | `file_name` |
| `fpe` | FPE: floating-point exception code | `floating_point_exception` |
| `fpfill` | padding after a floating-point register in the register display | `floating_point_padding` |
| `fpr` | floating-point register | `floating_point_register` |
| `fpstr` | format string for a floating-point register line | `floating_point_format` |
| `fpu` | floating-point unit | `floating_point_unit` |
| `fr` | fr: the extra source register field of madd.fmt | `floating_point_addend` |
| `fres` | the floating-point result register ($f0) | `floating_point_result_register` |
| `fs` | fs: floating-point source register field | `floating_point_source` |
| `ft` | ft: floating-point target (second source) register field | `floating_point_target` |
| `funct` | funct: the function field of an R-type instruction | `function_field` |
| `fval` | float value | `float_value` |
| `ge` | greater than or equal | `greater_or_equal` |
| `gp` | (1) $gp the global pointer; (2) "global pointer" area | `global_pointer` |
| `gpr` | general-purpose register(s) | `general_purpose_registers` |
| `grfill` | padding after a general register in the register display | `general_register_padding` |
| `grstr` | format string for a general register line | `general_register_format` |
| `gt` | greater than | `greater_than` |
| `hashbits` | number of hash bits | `hash_bits` |
| `hi` | HI: the multiply/divide high-result register | `multiply_divide_high` |
| `hr` | horizontal rule (a separator line) | `horizontal_rule` |
| `ibe` | IBE: bus error on an instruction fetch | `instruction_bus_error` |
| `id` | identifier | `identifier` |
| `idlen` | identifier length | `identifier_length` |
| `idx` | index | `index` |
| `ie` | IE: Status interrupt-enable bit | `interrupt_enable` |
| `im` | IM: Status interrupt-mask bits | `interrupt_mask` |
| `imm` | immediate (operand) | `immediate` |
| `immexpr` | immediate expression | `immediate_expression` |
| `inc` | increment | `increment` |
| `incr` | increment | `increment` |
| `init` | initialize | `initialize` |
| `inst` | instruction | `instruction` |
| `insts` | instructions | `instructions` |
| `intptr` | integer or pointer | `integer_or_pointer` |
| `ioffset` | immediate offset field (the 16-bit offset of a load/store) | `immediate_offset` |
| `ip` | IP: Cause interrupts-pending bits | `interrupts_pending` |
| `le` | less than or equal | `less_or_equal` |
| `len` | length | `length` |
| `littlendian` | little-endian | `little_endian` |
| `lo` | LO: the multiply/divide low-result register | `multiply_divide_low` |
| `lt` | less than | `less_than` |
| `mdmx` | MDMX: MIPS Digital Media eXtension exception code | `keep` |
| `mem` | memory | `memory` |
| `mk` | make | `make` |
| `mm` | memory-mapped | `memory_mapped` |
| `mods` | modifiers | `modifiers` |
| `movc` | movt/movf (move on a condition code) | `move_conditional` |
| `msg` | message | `message` |
| `mt` | MT: Config MMU type (bits 9:7) | `mmu_type` |
| `mul` | multiply | `multiply` |
| `mult` | multiply | `multiply` |
| `nd` | ND: nullify-delay bit of a "likely" floating-point branch | `nullify_delay` |
| `ndfp` | ND bit of a floating-point branch | `floating_point_nullify_delay` |
| `neq` | not equal | `not_equal` |
| `nl` | newline | `newline` |
| `noarg` | no argument | `no_argument` |
| `num` | number | `number` |
| `obs` | observer | `observer` |
| `off` | offset | `offset` |
| `offs` | offset | `offset` |
| `op` | (1) opcode; (2) operation | `opcode or operation per name` |
| `ops` | operations / opcodes | `per name` |
| `opt` | optional | `optional` |
| `orig` | original | `original` |
| `ov` | Ov: arithmetic-overflow exception code | `arithmetic_overflow` |
| `params` | parameters | `parameters` |
| `pc` | PC: program counter | `program_counter` |
| `pn` | new-segment pointer when growing the stack | `new_pointer` |
| `po` | old-segment pointer when growing the stack | `old_pointer` |
| `pos` | position | `position` |
| `pp` | pointer to a pointer (a char** cursor) | `cursor` |
| `prev` | previous | `previous` |
| `ptr` | pointer | `pointer` |
| `rd` | rd: the destination register field | `destination_register` |
| `rec` | record | `record` |
| `recv` | receive / receiver | `receiver` |
| `reg` | register | `register` |
| `regimm` | REGIMM: the opcode class whose rt field selects the instruction | `register_immediate` |
| `reglen` | register-name length | `register_name_length` |
| `regs` | registers | `registers` |
| `res` | result | `result_register` |
| `ri` | RI: reserved-instruction exception code | `reserved_instruction` |
| `rs` | rs: the source register field | `source_register` |
| `rt` | rt: the target (second source, or load destination) register field | `target_register` |
| `rtname` | name of the instruction selected by the rt field | `rt_instruction_name` |
| `seg` | segment | `segment` |
| `segs` | segments | `segments` |
| `sep` | separator | `separator` |
| `sh` | shift amount field | `shift_amount` |
| `shamt` | shamt: the shift-amount field | `shift_amount` |
| `snap` | snapshot | `snapshot` |
| `sp` | $sp: the stack pointer | `stack_pointer` |
| `src` | source | `source` |
| `ss` | string stream | `stream` |
| `stm` | stream | `stream` |
| `stmt` | statement | `statement` |
| `str` | string | `string` |
| `streq` | strings equal | `strings_equal` |
| `strs` | strings | `strings` |
| `sub` | subtract | `subtract` |
| `subst` | substitute | `substitute` |
| `sym` | symbol | `symbol` |
| `sys` | Sys: system-call exception code | `system_call` |
| `sz` | size | `size` |
| `tbl` | table | `table` |
| `tf` | TF: true/false bit of a floating-point branch or conditional move | `true_false` |
| `tff` | TF bit of a floating-point instruction | `floating_point_true_false` |
| `tg` | target field (the 26-bit jump target) | `target` |
| `tlbl` | TLBL: TLB refill on a load or fetch | `tlb_load` |
| `tlbs` | TLBS: TLB refill on a store | `tlb_store` |
| `tmp` | temporary | `temporary` |
| `tok` | token | `token` |
| `tpl` | template (explain-mode output template) | `template` |
| `tr` | Tr: trap exception code | `trap` |
| `trans` | transmit / transmitter | `transmitter` |
| `uimm` | unsigned immediate | `unsigned_immediate` |
| `uk` | unknown | `unknown` |
| `um` | UM: Status user-mode bit | `user_mode` |
| `un` | unordered (floating-point compare) | `unordered` |
| `uncond` | unconditional | `unconditional` |
| `undef` | undefined | `undefined` |
| `undefs` | undefined labels | `undefined_labels` |
| `val` | value | `value` |
| `xmalloc` | malloc that exits on failure | `allocate_or_exit` |
| `xrealloc` | realloc that exits on failure | `reallocate_or_exit` |
| `zmalloc` | zero-filling malloc that exits on failure | `allocate_zeroed_or_exit` |

## The words that were kept

| Word | Meaning | Why it stays |
| --- | --- | --- |
| `ac` | high*high partial product (a*c) in the long-hand 32x32 multiply | math: src/run.c unsigned_multiply: a,b,c,d are the 16-bit halves and ac/ad/cb/bd their products; renaming the halves (high1, low1, ...) is the alternative (decision 5) |
| `ad` | (1) high*low partial product a*d; (2) AdEL/AdES: "address error" | math/architecture: src/run.c; include/instruction.h |
| `al` | "and link" suffix of a branch mnemonic (bgezal) | keyword: src/explain.c; the name already spells it out after the suffix |
| `argc` | argument count | external: main's parameter; C convention |
| `argv` | argument vector | external: main's parameter; program_argv and owned_argv keep the argv word |
| `asan` | AddressSanitizer | familiar: SPIM_ASAN_BUILD; the tool's own name (decision 3) |
| `ascii` | ASCII | familiar: also the .ascii directive |
| `asciiz` | the .asciiz directive | keyword |
| `ast` | abstract syntax tree | familiar: ast_node, AST_* (decision 3) |
| `bd` | (1) BD: Cause "branch delay" bit; (2) low*low partial product b*d | architecture/math: CP0_Cause_BD; src/run.c |
| `breakpoint` | breakpoint | not-abbreviation: hunspell lacks it |
| `cb` | (1) callback; (2) c*b partial product | spell-out/math: include/symbol-table.h for_each_label; src/run.c |
| `cfc` | cfc0/cfc1/cfc2 mnemonics | keyword |
| `comm` | the .comm directive | keyword: AST_DIR_COMM |
| `config` | Config: the CP0 configuration register | familiar: CP0_Config; "config" is common English now |
| `continuable` | able to continue | not-abbreviation |
| `cop` | COP0/COP2 mnemonic | keyword |
| `coprocessor` | coprocessor | not-abbreviation: hunspell lacks it |
| `cvt` | cvt.* mnemonics | keyword |
| `environ` | the POSIX environment array | external |
| `eof` | end of file | familiar: TOK_EOF, at_eof (decision 3) |
| `extern` | the .extern directive | keyword |
| `fd` | (1) fd: floating-point destination register field; (2) a POSIX file descriptor | architecture/external: FP_FD_FS_INST, FD(INST); fd in the syscall code — split per name |
| `fdset` | an fd_set (POSIX select) | external: src/spim.c |
| `fl` | the "likely" bc1fl/bc2fl mnemonics | keyword |
| `fp` | (1) floating point; (2) $fp the frame pointer; (3) a FILE* (repl_fp) | spell-out/keyword: FP_* instruction kinds, fp_double_view; split per name |
| `globl` | the .globl directive | keyword |
| `halfword` | half-word | not-abbreviation: the manual's term |
| `int` | (1) Int: interrupt exception code; (2) integer | architecture/spell-out: ExcCode_Int; int_reg_names, TOK_INT |
| `io` | input/output | familiar: mapped_io, MM_IO_BOT (decision 3) |
| `json` | JSON | familiar |
| `kdata` | the .kdata segment/directive | keyword |
| `kilo` | 1024 | not-abbreviation: include/spim.h |
| `ktext` | the .ktext segment/directive | keyword |
| `lcomm` | the .lcomm directive | keyword |
| `loc` | (1) location; (2) the .loc directive | spell-out/keyword |
| `mid` | middle partial product | math: src/run.c unsigned_multiply |
| `mips` | MIPS | familiar |
| `mod` | (1) Mod: TLB-modified exception; (2) modifier | architecture/spell-out: ExcCode_Mod; inst_modifier |
| `ms` | milliseconds | familiar: TIMER_TICK_MS |
| `nop` | nop mnemonic / no-operation | keyword: nop_inst, NOP_CMD |
| `ok` | OK | familiar |
| `printf` | the printf family | external: ss_printf mirrors printf |
| `repl` | read-eval-print loop | familiar: repl_line, repl_buf (decision 3) |
| `sbrk` | the sbrk syscall | external: SBRK_SYSCALL; the syscall's own name |
| `spim` | SPIM, the simulator | familiar |
| `syscall` | system call | familiar: *_SYSCALL constants; also the syscall mnemonic |
