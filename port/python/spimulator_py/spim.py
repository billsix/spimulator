"""The command line and the REPL (the C engine's spim.c).

``main`` reads the C engine's flags, loads the ``-f`` files, and then either
runs the program (exit status: the program's, or 128 + the first fault's
ExcCode, 1 for undefined symbols, 2 for a parse error), dumps it
(``-dump``), prints its tree (``-print-ast``), or, with no program, starts
the REPL, which reads one command per line from stdin.

The teaching mode (``-explain``, src/explain.c) is not part of this port:
given it, the port says so and exits with status 2.
"""

import enum
import io
import os
import signal
import sys
from typing import BinaryIO, assert_never

from . import (
    assembler_event,
    console,
    data,
    display,
    instruction,
    machine,
    memory,
    opcodes,
    options,
    parser,
    pseudo_op,
    scanner,
    spim_utils,
    symbol_table,
    traps,
)
from . import memory_layout as layout
from . import registers as regs
from .assembler_event import Event, Kind
from .cformat import c_format
from .registers import cpu

USAGE: str = (
    "Usage: spimulator [option ...] [-file] file [-file file ...] [program arguments]\n"
    "\t-asm, -a\t\tExtended machine (pseudo-ops, no delayed branches and loads) (default)\n"
    "\t-bare, -b\t\tBare machine (no pseudo-ops, delayed branches and loads)\n"
    "\t-delayed_branches, -db\tExecute delayed branches\n"
    "\t-delayed_loads, -dl\tExecute delayed loads\n"
    "\t-pseudo, -p\t\tAccept pseudo-instructions (default)\n"
    "\t-nopseudo, -np\t\tDo not accept pseudo-instructions\n"
    "\t-exception, -e\t\tLoad the exception handler (default); also -trap, -t\n"
    "\t-noexception, -ne\tDo not load the exception handler; also -notrap, -nt\n"
    "\t-exception_file <file>, -ef <file>\n"
    "\t\t\t\tLoad <file> as the exception handler; also -trap_file, -tf\n"
    "\t-quiet, -q\t\tDo not print warnings\n"
    "\t-noquiet, -nq\t\tPrint warnings (default)\n"
    "\t-explain [N], -x [N]\tNarrate every instruction (teaching mode).\n"
    "\t\t\t\tLevel N selects detail: 1 = step header, 2 = + narration,\n"
    "\t\t\t\t3 = + bit-layout (default when N omitted),\n"
    "\t\t\t\t4 = decoder walkthrough in place of the bit-layout.\n"
    "\t\t\t\tAlso accepts -explain=N.\n"
    "\t-noexplain, -nx\t\tDisable teaching mode (default)\n"
    "\t-mapped_io, -mio\tEnable memory-mapped IO\n"
    "\t-nomapped_io, -nmio\tDo not enable memory-mapped IO (default)\n"
    "\t-stext, -st <size>\tInitial text segment size, in bytes\n"
    "\t-sdata, -sd <size>\tInitial data segment size, in bytes\n"
    "\t-sstack, -ss <size>\tInitial stack segment size, in bytes\n"
    "\t-sktext, -skt <size>\tInitial kernel text segment size, in bytes\n"
    "\t-skdata, -skd <size>\tInitial kernel data segment size, in bytes\n"
    "\t-ldata, -ld <size>\tData segment size limit, in bytes\n"
    "\t-lstack, -ls <size>\tStack segment size limit, in bytes\n"
    "\t-lkdata, -lkd <size>\tKernel data segment size limit, in bytes\n"
    "\t-file <file>, -f <file>\tAssembly code file; repeat to load several into one\n"
    "\t\t\t\tsymbol table (forward references resolve across files).\n"
    "\t\t\t\tA bare argument is loaded as a file too; the arguments\n"
    "\t\t\t\tafter the last file are the program's arguments.\n"
    "\t-assemble\t\tWrite assembled code to <file>.out\n"
    "\t-listing <file>\t\tWrite assemble-time event trace to <file> (use - for stderr)\n"
    "\t-print-ast\t\tParse to AST, print it to stderr, and exit without emitting any code\n"
    "\t-print-ast-json\t\tParse to AST, dump JSON to stdout (for tooling), and exit\n"
    "\t-show-expansion\t\tParse to AST, print just the pseudo-op wrappers + their expansion, and exit\n"
    "\t-dump\t\t\tWrite user data and text segments into files\n"
    "\t-full_dump\t\tWrite user and kernel data and text into files.\n"
)

EXPLAIN_UNSUPPORTED: str = (
    "spimulator-python: the teaching mode (-explain) is not supported by this port\n"
)

_VALUE_FLAGS: frozenset[str] = frozenset(
    ("-stext", "-st", "-sdata", "-sd", "-ldata", "-ld", "-sstack", "-ss",
     "-lstack", "-ls", "-sktext", "-skt", "-skdata", "-skd", "-lkdata", "-lkd",
     "-exception_file", "-ef", "-trap_file", "-tf")
)  # fmt: skip
"""The flags that take the next argument as their value."""

exception_file_name: str = spim_utils.DEFAULT_EXCEPTIONS
load_exception_handler: bool = True
program_argv: list[str] = []
_listing: BinaryIO | None = None


def _exception_files() -> str | None:
    """The handler files initialize_world loads, or None (``-noexception``)."""
    return exception_file_name if load_exception_handler else None


# --- the -listing trace ---------------------------------------------------------


def _listing_observer(event: Event) -> None:
    """Write one assemble-time event to the listing file."""
    if _listing is None:
        return
    line: str = f"line {event.source_line:4d}  "
    match event.kind:
        case Kind.LABEL_DEF:
            line += (
                f"label  {event.name:<24s} defined at 0x{event.addr:08x}"
                f"{'  (global)' if event.flag else ''}\n"
            )
        case Kind.TEXT_INST:
            text: str = instruction.inst_to_string(event.addr)
            line += f"text   0x{event.addr:08x}  {text:<32s}  0x{event.value:08x}\n"
        case Kind.DATA_BYTE:
            line += f"data   0x{event.addr:08x}  byte    0x{event.value & 0xFF:02x}\n"
        case Kind.DATA_HALF:
            line += f"data   0x{event.addr:08x}  half    0x{event.value & 0xFFFF:04x}\n"
        case Kind.DATA_WORD:
            line += (
                f"data   0x{event.addr:08x}  word    0x{machine.u32(event.value):08x}\n"
            )
        case Kind.DATA_DOUBLE:
            line += f"data   0x{event.addr:08x}  double  {c_format('%.18g', event.double)}\n"
        case Kind.DATA_STRING:
            line += (
                f"data   0x{event.addr:08x}  string  len={event.value}"
                f"{' (+\\0)' if event.flag else ''}\n"
            )
        case Kind.FORWARD_REF:
            line += (
                f"fref   0x{event.addr:08x}  uses unresolved symbol '{event.name}'\n"
            )
        case Kind.FORWARD_RESOLVED:
            line += (
                f"fres   0x{event.addr:08x}  patched '{event.name}'"
                f" = 0x{machine.u32(event.value):08x}\n"
            )
        case _:
            assert_never(event.kind)
    if _listing is sys.stderr.buffer:
        console.error(line)
    else:
        _listing.write(console.to_bytes(line))


# --- the command line -----------------------------------------------------------


def _c_atoi(text: str | None) -> int:
    """C's ``atoi`` (0 for a missing argument, where C would crash)."""
    return scanner._c_atoi(text) if text is not None else 0


def main(arguments: list[str]) -> int:
    """Run the simulator with the C engine's command-line arguments.

    Args:
        arguments: the arguments after the program name, one char per byte.

    Returns:
        int: the exit status.
    """
    # Python ignores SIGPIPE; C does not, so `spimulator ... | head` ends the
    # simulator when head exits.  Restore the default.
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    # A delay slot runs through a recursive run_spim call, as in C, so a jump in
    # the delay slot of a jump nests one level deeper.  C nests about 10,000 levels
    # before its stack runs out; Python's default limit of 1,000 frames stopped
    # at about 330.  Allow several frames per level beyond C's depth.
    sys.setrecursionlimit(max(sys.getrecursionlimit(), 100_000))
    try:
        return _main(arguments)
    except console.RunError:
        # A run-time error before there is a top level to return to (while
        # the -f files load): the message is printed; the run ends with 1.
        return 1
    except SystemExit as stop:
        code: object = stop.code
        if code is None:
            return 0
        return code if isinstance(code, int) else 1


def _main(argv: list[str]) -> int:
    """The body of :func:`main`."""
    global exception_file_name, load_exception_handler, program_argv, _listing
    assembly_file_loaded: bool = False
    assembly_file_attempted: bool = False
    world_initialized: bool = False
    last_file_i: int = -1
    print_usage: bool = False
    assemble: bool = False
    dump_user: bool = False
    dump_all: bool = False
    args: list[str] = ["spimulator-python", *argv]
    argc: int = len(args)
    env_handler: bytes | None = os.environb.get(b"SPIM_EXCEPTION_HANDLER")
    if env_handler is not None:
        exception_file_name = console.from_bytes(env_handler)

    def next_arg(i: int) -> str | None:
        """argv[i], or None past the end (C would read NULL)."""
        return args[i] if i < argc else None

    i: int = 1
    while i < argc:
        arg: str = args[i]
        match arg:
            case _ if arg in _VALUE_FLAGS and i + 1 >= argc:
                # The flag's value is missing: report it, then the usage.
                console.error(f"spimulator: {arg} requires a value\n")
                print_usage = True
            case "-asm" | "-a":
                options.bare_machine = False
                options.delayed_branches = False
                options.delayed_loads = False
            case "-bare" | "-b":
                options.bare_machine = True
                options.delayed_branches = True
                options.delayed_loads = True
                options.quiet = True
            case "-delayed_branches" | "-db":
                options.delayed_branches = True
            case "-delayed_loads" | "-dl":
                options.delayed_loads = True
            case "-exception" | "-e" | "-trap" | "-t":
                load_exception_handler = True
            case "-noexception" | "-ne" | "-notrap" | "-nt":
                load_exception_handler = False
            case "-exception_file" | "-ef" | "-trap_file" | "-tf":
                i += 1
                exception_file_name = next_arg(i) or ""
                load_exception_handler = True
            case "-mapped_io" | "-mio":
                options.mapped_io = True
            case "-nomapped_io" | "-nmio":
                options.mapped_io = False
            case "-pseudo" | "-p":
                options.accept_pseudo_insts = True
            case "-nopseudo" | "-np":
                options.accept_pseudo_insts = False
            case "-quiet" | "-q":
                options.quiet = True
            case "-noquiet" | "-nq":
                options.quiet = False
            case "-noexplain" | "-nx":
                pass  # the teaching mode is off: there is none
            case _ if arg in ("-explain", "-x") or arg.startswith(("-explain=", "-x=")):
                console.error(EXPLAIN_UNSUPPORTED)
                return 2
            case "-stext" | "-st":
                i += 1
                options.initial_text_size = _c_atoi(next_arg(i))
            case "-sdata" | "-sd":
                i += 1
                options.initial_data_size = _c_atoi(next_arg(i))
            case "-ldata" | "-ld":
                i += 1
                options.initial_data_limit = machine.s32(_c_atoi(next_arg(i)))
            case "-sstack" | "-ss":
                i += 1
                options.initial_stack_size = _c_atoi(next_arg(i))
            case "-lstack" | "-ls":
                i += 1
                options.initial_stack_limit = machine.s32(_c_atoi(next_arg(i)))
            case "-sktext" | "-skt":
                i += 1
                options.initial_k_text_size = _c_atoi(next_arg(i))
            case "-skdata" | "-skd":
                i += 1
                options.initial_k_data_size = _c_atoi(next_arg(i))
            case "-lkdata" | "-lkd":
                i += 1
                options.initial_k_data_limit = machine.s32(_c_atoi(next_arg(i)))
            case _ if (arg in ("-file", "-f") and i + 1 < argc) or not arg.startswith(
                "-"
            ):
                # `-f FILE` (may repeat: the files share one symbol table), or a
                # bare FILE, which is also argv[0], followed by the program's args.
                is_explicit_f: bool = arg.startswith("-")
                if not is_explicit_f and assembly_file_loaded:
                    break
                file_i: int = i + 1 if is_explicit_f else i
                if not world_initialized:
                    spim_utils.initialize_world(_exception_files(), not options.quiet)
                    world_initialized = True
                assembly_file_attempted = True
                if not spim_utils.read_assembly_file(args[file_i]):
                    break
                assembly_file_loaded = True
                last_file_i = file_i
                if is_explicit_f:
                    i = file_i + 1
                    continue
                program_argv = args[file_i:]
                break
            case "-assemble":
                assemble = True
            case "-print-ast":
                parser.print_ast = True
                parser.print_ast_only = True
            case "-show-expansion":
                parser.show_expansion = True
                parser.print_ast_only = True
            case "-print-ast-json":
                parser.print_ast_json = True
                parser.print_ast_only = True
            case "-listing":
                if i + 1 >= argc:
                    console.error("\n-listing requires a filename argument\n")
                    print_usage = True
                    i += 1
                    continue
                i += 1
                path: str = args[i]
                if path == "-":
                    _listing = sys.stderr.buffer
                else:
                    try:
                        _listing = open(console.to_bytes(path), "wb")  # noqa: SIM115
                    except OSError:
                        console.error(f"\nCannot open listing file '{path}'\n")
                        print_usage = True
                        i += 1
                        continue
                assembler_event.observer = _listing_observer
            case "-dump":
                dump_user = True
            case "-full_dump":
                dump_all = True
            case _:
                console.error(f"\nUnknown argument: {arg} (ignored)\n")
                print_usage = True
        i += 1

    if assembly_file_loaded and not program_argv:
        program_argv = args[last_file_i:]
    if print_usage:
        console.error(USAGE)
    try:
        return _after_arguments(
            assembly_file_loaded, assembly_file_attempted, assemble, dump_user, dump_all
        )
    finally:
        if _listing is not None and _listing is not sys.stderr.buffer:
            _listing.close()


def _after_arguments(
    loaded: bool, attempted: bool, assemble: bool, dump_user: bool, dump_all: bool
) -> int:
    """Run, dump, or start the REPL, once the flags are read."""
    if not loaded:
        if attempted:
            # The file could not be opened: no REPL, which would wait on stdin.
            return options.spim_return_value or 2
        spim_utils.initialize_world(_exception_files(), not options.quiet)
        spim_utils.initialize_run_stack(program_argv)
        top_level()
    if pseudo_op.parse_errors_seen > 0:
        return options.spim_return_value or 2
    elif parser.print_ast_only:
        return 0
    elif assemble:
        return 1 if write_assembled_code(program_argv[0]) else 0
    else:
        if dump_user:
            _dump_data_seg(False)
            _dump_text_seg(False)
        elif dump_all:
            _dump_data_seg(True)
            _dump_text_seg(True)
        else:
            console.console_to_program()
            spim_utils.initialize_run_stack(program_argv)
            try:
                undefined: str | None = symbol_table.undefined_symbol_string()
                if undefined is not None:
                    console.message(
                        f"The following symbols are undefined:\n{undefined}\n"
                    )
                    console.console_to_spim()
                    return options.spim_return_value or 1
                spim_utils.run_program(
                    symbol_table.find_symbol_address(layout.DEFAULT_RUN_LOCATION),
                    layout.DEFAULT_RUN_STEPS,
                    False,
                    False,
                )
            except console.RunError:
                # run_error printed why the run stopped; exit 1 unless the
                # program already set its own status (C lands its longjmp here).
                if options.spim_return_value == 0:
                    options.spim_return_value = 1
            console.console_to_spim()
            if traps.first_bad_exception != -1 and options.spim_return_value == 0:
                # A fault the program did not survive cleanly: 128 + ExcCode, like
                # a shell's 128 + signal number.  An explicit exit2(N) wins.
                options.spim_return_value = 128 + traps.first_bad_exception
        return options.spim_return_value


def write_assembled_code(program_name: str) -> bool:
    """``-assemble``: write the code as ``.word`` lists to NAME.out.

    Returns:
        bool: True on failure.
    """
    if pseudo_op.parse_error_occurred:
        return True
    name: str = program_name + ".out"
    try:
        out: BinaryIO = open(console.to_bytes(name), "wb")  # noqa: SIM115
    except OSError as problem:
        console.error(f"{name}: {problem.strerror}\n")
        return True
    instruction.user_kernel_text_segment(False)
    start: int = symbol_table.find_symbol_address(layout.END_OF_TRAP_HANDLER_SYMBOL)
    end: int = instruction.current_text_pc()
    words: list[str] = [
        f"0x{machine.u32(instruction.inst_encode(memory.read_inst(addr))):x}"
        for addr in range(start, end, 4)
    ]
    text: str = f".text # 0x{start:x} .. 0x{end:x}\n.word " + ", ".join(words) + "\n"
    data.user_kernel_data_segment(False)
    start = 0 if options.bare_machine else layout.DATA_BOT
    end = data.current_data_pc()
    if end > start:
        values: list[str] = [
            f"0x{machine.u32(memory.read_word(addr)):x}"
            for addr in range(start, end, 4)
        ]
        text += f".data # 0x{start:x} .. 0x{end:x}\n.word " + ", ".join(values) + "\n"
    with out:
        out.write(console.to_bytes(text))
    return False


def _write_file(name: str, text: str) -> None:
    """Write text to the file name in the current directory."""
    with open(name, "wb") as out:
        out.write(console.to_bytes(text))


def _dump_data_seg(kernel_also: bool) -> None:
    """``-dump``/``-full_dump``: the data segment(s) to data.asm."""
    if kernel_also:
        _write_file("data.asm", display.format_data_segs())
    else:
        _write_file(
            "data.asm",
            "\tDATA\n" + display.format_mem(layout.DATA_BOT, memory.data_top),
        )


def _dump_text_seg(kernel_also: bool) -> None:
    """``-dump``/``-full_dump``: the text segment(s) to text.asm."""
    if kernel_also:
        text: str = (
            display.format_insts(layout.TEXT_BOT, memory.text_top)
            + "\n\tKERNEL\n"
            + display.format_insts(layout.K_TEXT_BOT, memory.k_text_top)
        )
    else:
        text = "\n\tUSER TEXT SEGMENT\n" + display.format_insts(
            layout.TEXT_BOT, memory.text_top
        )
    _write_file("text.asm", text)


# --- the REPL ---------------------------------------------------------------------


class _InterruptedError(Exception):
    """Ctrl-C: abandon the command (the C engine longjmps to the top level)."""


def _control_c_seen(signum: int, frame: object) -> None:
    """The SIGINT handler."""
    console.console_to_spim()
    console.message("\nExecution interrupted\n")
    raise _InterruptedError


def _read_line() -> bytes | None:
    """The next command line from stdin, without its newline (None at EOF).

    Reads one byte at a time, as the C engine's line editor does when stdin
    is not a terminal, so a running program can read the rest of stdin.
    """
    if os.isatty(console.STDIN):
        try:
            return console.to_bytes(input("(spim) "))
        except EOFError:
            return None
    line: bytearray = bytearray()
    while True:
        byte: bytes = console.read_byte()
        if not byte:
            return bytes(line) if line else None
        elif byte == b"\n":
            return bytes(line)
        else:
            line += byte


def top_level() -> None:
    """Read and execute REPL commands until ``exit`` or end of input.

    Raises:
        SystemExit: always, at the end.
    """
    signal.signal(signal.SIGINT, _control_c_seen)
    scanner.init(None)
    parser.input_file_name = "<standard input>"
    redo: bool = False
    while True:
        pushed: bool = False
        try:
            if not redo:
                line: bytes | None = _read_line()
                if line is None:
                    console.message("\n")
                    console.console_to_spim()
                    raise SystemExit(0)
                scanner.push_source(io.BytesIO(line + b"\n"))
                pushed = True
            redo = parse_spim_command(redo)
        except (console.RunError, _InterruptedError):
            redo = False
        if pushed:
            scanner.pop_source()


# The commands, and the state that a blank line ("do it again") reuses.
class Command(enum.IntEnum):
    """A REPL command, numbered as the C engine's spim.c numbers them.

    The values mirror the C enum so the two sources can be read side by side;
    nothing outside the program sees them.
    """

    UNKNOWN = 0
    EXIT = 1
    READ = 2
    RUN = 3
    STEP = 4
    PRINT = 5
    PRINT_SYM = 6
    PRINT_ALL_REGS = 7
    REINITIALIZE = 8
    ASM = 9
    REDO = 10
    NOP = 11
    HELP = 12
    CONTINUE = 13
    SET_BKPT = 14
    DELETE_BKPT = 15
    LIST_BKPT = 16
    DUMPNATIVE = 17
    DUMP = 18
    DISASM = 19
    ARGS = 20


_prev_cmd: Command = Command.NOP
_prev_token: int = 0
_run_addr: int = 0
_step_count: int = 0
_print_loc: int = 0
_bkpt_addr: int = 0


def _read_token() -> int:
    """The next token of the command line; end of input exits (C read_token).

    Raises:
        SystemExit: at end of input.
    """
    token: int = scanner.advance()
    if token == opcodes.TOK_EOF:
        console.console_to_spim()
        raise SystemExit(0)
    return token


def _flush_to_newline() -> None:
    """Skip the rest of the command line, including its newline."""
    while _read_token() != opcodes.TOK_NL:
        pass


def _get_opt_int() -> int:
    """An optional integer argument (0 if there is none); flushes the line."""
    token: int = _read_token()
    if token == opcodes.TOK_INT:
        _flush_to_newline()
        return _value_int()
    if token != opcodes.TOK_NL:
        _flush_to_newline()
    return 0


def _value_int() -> int:
    """The scanner's last value, as an int."""
    value: int | float | str = scanner.scan_value
    return value if isinstance(value, int) else 0


def _value_str() -> str:
    """The scanner's last value, as a string."""
    value: int | float | str = scanner.scan_value
    return value if isinstance(value, str) else ""


def _str_prefix(text: str, command: str, min_match: int) -> bool:
    """True if text is a prefix of command at least min_match long."""
    return command.startswith(text) and len(text) >= min_match


_COMMANDS: list[tuple[str, int, Command]] = [
    ("exit", 2, Command.EXIT),
    ("quit", 2, Command.EXIT),
    ("print", 1, Command.PRINT),
    ("print_symbols", 7, Command.PRINT_SYM),
    ("print_all_regs", 7, Command.PRINT_ALL_REGS),
    ("run", 2, Command.RUN),
    ("args", 2, Command.ARGS),
    ("read", 2, Command.READ),
    ("load", 2, Command.READ),
    ("reinitialize", 6, Command.REINITIALIZE),
    ("step", 1, Command.STEP),
    ("help", 1, Command.HELP),
    ("continue", 1, Command.CONTINUE),
    ("breakpoint", 2, Command.SET_BKPT),
    ("delete", 1, Command.DELETE_BKPT),
    ("list", 2, Command.LIST_BKPT),
    ("dumpnative", 5, Command.DUMPNATIVE),
    ("dump", 4, Command.DUMP),
    ("disasm", 3, Command.DISASM),  # "di" is the MIPS32r2 instruction, a keyword
]


def _read_assembly_command() -> Command:
    """Read a command word; the first matching (prefix, min) entry wins."""
    token: int = _read_token()
    if token == opcodes.TOK_NL:
        return Command.REDO
    elif token != opcodes.TOK_ID:
        return Command.UNKNOWN
    else:
        word: str = _value_str()
        for command, min_match, number in _COMMANDS:
            if _str_prefix(word, command, min_match):
                return number
        if word.startswith("?"):
            return Command.HELP
        elif word.startswith("."):
            return Command.ASM
        else:
            return Command.UNKNOWN


HELP_TEXT: str = (
    "\nSPIM is a MIPS32 simulator.\n"
    "Its top-level commands are:\n"
    "exit  -- Exit the simulator\n"
    "quit  -- Exit the simulator\n"
    'read "FILE" -- Read FILE containing assembly code into memory\n'
    'load "FILE" -- Same as read\n'
    "run <ADDR> -- Start the program at (optional) ADDRESS\n"
    "args <ARG ...> -- Set the args the next `run` will pass to the program;\n"
    "                  argv[0] stays as the loaded program path.  `args`\n"
    "                  with no operands clears the args.\n"
    "step <N> -- Step the program for N instructions (default 1)\n"
    "continue -- Continue program execution without stepping\n"
    "print $N -- Print register N\n"
    "print $fN -- Print floating point register N\n"
    "print ADDR -- Print contents of memory at ADDRESS\n"
    "print_symbols -- Print all global symbols\n"
    "print_all_regs -- Print all MIPS registers\n"
    "print_all_regs hex -- Print all MIPS registers in hex\n"
    "reinitialize -- Clear the memory and registers\n"
    "breakpoint <ADDR> -- Set a breakpoint at address ADDR\n"
    "delete <ADDR> -- Delete breakpoint at address ADDR\n"
    "list -- List all breakpoints\n"
    'dump [ "FILE" ] -- Dump binary code to spim.dump or FILE in network byte order\n'
    'dumpnative [ "FILE" ] -- Dump binary code to spim.dump or FILE in host byte order\n'
    "disasm -- Print a disassembly listing of the loaded program's text segment\n"
    ". -- Rest of line is assembly instruction to execute\n"
    "<cr> -- Newline reexecutes previous command\n"
    "? -- Print this message\n"
    "\nMost commands can be abbreviated to their unique prefix\n"
    "e.g., ex(it), re(ad), l(oad), ru(n), s(tep), p(rint)\n\n"
)


def _report_breakpoint(stopped: bool) -> None:
    """Say so if the run stopped at a breakpoint."""
    if stopped:
        console.message(f"Breakpoint encountered at 0x{cpu.pc:08x}\n")


def parse_spim_command(redo: bool) -> bool:
    """Execute one REPL command (or, with redo, the previous one again).

    Returns:
        bool: True if the command was a blank line (redo the previous one).

    Raises:
        SystemExit: ``exit``/``quit``, or end of input.
    """
    global _prev_cmd, _prev_token, _run_addr, _step_count, _print_loc, _bkpt_addr
    global program_argv
    cmd: Command = _prev_cmd if redo else _read_assembly_command()
    if cmd == Command.EXIT:
        console.console_to_spim()
        raise SystemExit(0)
    match cmd:
        case Command.READ:
            token: int = _prev_token if redo else _read_token()
            if not redo:
                _flush_to_newline()
            if token == opcodes.TOK_STR:
                spim_utils.read_assembly_file(_value_str())
                scanner.pop_source()
            else:
                console.error("Must supply a filename to read\n")
            _prev_cmd = Command.READ
        case Command.RUN:
            _run_addr = _run_addr if redo else machine.u32(_get_opt_int())
            if _run_addr == 0:
                _run_addr = spim_utils.starting_address()
            spim_utils.initialize_run_stack(program_argv)
            console.console_to_program()
            if _run_addr != 0:
                undefined: str | None = symbol_table.undefined_symbol_string()
                if undefined is not None:
                    console.message(
                        f"The following symbols are undefined:\n{undefined}\n"
                    )
                _report_breakpoint(
                    spim_utils.run_program(
                        _run_addr, layout.DEFAULT_RUN_STEPS, False, False
                    )
                )
            console.console_to_spim()
            _prev_cmd = Command.RUN
        case Command.CONTINUE:
            if cpu.pc != 0:
                console.console_to_program()
                _report_breakpoint(
                    spim_utils.run_program(
                        cpu.pc, layout.DEFAULT_RUN_STEPS, False, True
                    )
                )
                console.console_to_spim()
            _prev_cmd = Command.CONTINUE
        case Command.STEP:
            _step_count = _step_count if redo else _get_opt_int()
            addr: int = spim_utils.starting_address() if cpu.pc == 0 else cpu.pc
            if _step_count == 0:
                _step_count = 1
            if addr != 0:
                console.console_to_program()
                _report_breakpoint(
                    spim_utils.run_program(addr, _step_count, True, True)
                )
                console.console_to_spim()
            _prev_cmd = Command.STEP
        case Command.PRINT:
            token = _prev_token if redo else _read_token()
            consumed_nl: bool = False
            match token:
                case opcodes.TOK_REG:
                    _print_loc = _print_loc + 1 if redo else _value_int()
                    _print_reg(_print_loc)
                case opcodes.TOK_FP_REG:
                    _print_loc = _print_loc + 2 if redo else _value_int()
                    _print_fp_reg(_print_loc)
                case opcodes.TOK_INT:
                    if redo:
                        _print_loc = machine.s32(_print_loc + 4)
                        memory.print_mem(machine.u32(_print_loc))
                    else:
                        consumed_nl = _print_address(_value_int())
                case opcodes.TOK_ID:
                    name: str = _value_str()
                    if not _print_reg_from_string(name):
                        _print_loc = (
                            machine.s32(_print_loc + 4)
                            if redo
                            else machine.s32(symbol_table.find_symbol_address(name))
                        )
                        if _print_loc != 0:
                            memory.print_mem(machine.u32(_print_loc))
                        else:
                            console.error(f"Unknown label: {name}\n")
                case _:
                    console.error("Print what?\n")
            if not redo and not consumed_nl:
                _flush_to_newline()
            _prev_cmd = Command.PRINT
            _prev_token = token
        case Command.PRINT_SYM:
            symbol_table.print_symbols()
            if not redo:
                _flush_to_newline()
            _prev_cmd = Command.NOP
        case Command.PRINT_ALL_REGS:
            token = _prev_token if redo else _read_token()
            hex_flag: bool = token == opcodes.TOK_ID and _value_str() == "hex"
            console.message(display.format_registers(hex_flag, hex_flag) + "\n")
            if (
                not redo and token != opcodes.TOK_NL
            ):  # a bare newline ends the line already
                _flush_to_newline()
            _prev_cmd = Command.NOP
        case Command.REINITIALIZE:
            _flush_to_newline()
            spim_utils.initialize_world(_exception_files(), not options.quiet)
            spim_utils.initialize_run_stack(program_argv)
            spim_utils.write_startup_message()
            _prev_cmd = Command.NOP
        case Command.ASM:
            parser.parse_file()
            _prev_cmd = Command.ASM
        case Command.REDO:
            return True
        case Command.NOP:
            _prev_cmd = Command.NOP
        case Command.ARGS:
            argv0: str = program_argv[0] if program_argv else "<repl>"
            words: list[str] = []
            while (token := _read_token()) not in (opcodes.TOK_NL, 0):
                if token in (opcodes.TOK_STR, opcodes.TOK_ID):
                    words.append(_value_str())
                elif token == opcodes.TOK_INT:
                    words.append(str(_value_int()))
                else:
                    pass  # any other token is not an argument word
            program_argv = [argv0, *words]
            _prev_cmd = Command.ARGS
        case Command.HELP:
            if not redo:
                _flush_to_newline()
            console.message(HELP_TEXT)
            _prev_cmd = Command.HELP
        case Command.SET_BKPT | Command.DELETE_BKPT:
            token = _prev_token if redo else _read_token()
            if (
                not redo and token != opcodes.TOK_NL
            ):  # a bare newline ends the line already
                _flush_to_newline()
            if token == opcodes.TOK_INT:
                _bkpt_addr = (
                    machine.u32(_bkpt_addr + 4) if redo else machine.u32(_value_int())
                )
            elif token == opcodes.TOK_ID:
                _bkpt_addr = (
                    machine.u32(_bkpt_addr + 4)
                    if redo
                    else symbol_table.find_symbol_address(_value_str())
                )
            else:
                console.error("Must supply an address for breakpoint\n")
                return False
            if cmd == Command.SET_BKPT:
                spim_utils.add_breakpoint(_bkpt_addr)
            else:
                spim_utils.delete_breakpoint(_bkpt_addr)
            _prev_cmd = cmd
        case Command.LIST_BKPT:
            if not redo:
                _flush_to_newline()
            spim_utils.list_breakpoints()
            _prev_cmd = Command.LIST_BKPT
        case Command.DUMP | Command.DUMPNATIVE:
            _dump_command(cmd, _prev_token if redo else _read_token())
        case Command.DISASM:
            display.print_text_listing()
            if not redo:
                _flush_to_newline()
            _prev_cmd = Command.NOP
        case _:
            while _read_token() != opcodes.TOK_NL:
                pass
            console.error("Unknown spim command\n")
    return False


def _print_address(address: int) -> bool:
    """``print ADDR`` or ``print N($reg)``.

    Returns:
        bool: True if the command line's newline was already read.
    """
    global _print_loc
    following: int = _read_token()
    if following == ord("("):
        reg_token: int = _read_token()
        reg: int = _value_int() if reg_token == opcodes.TOK_REG else -1
        close: int = _read_token() if reg_token == opcodes.TOK_REG else 0
        if reg < 0 or close != ord(")"):
            console.error("expected $reg) after '(' in print N($reg) form\n")
        else:
            _print_loc = machine.s32(cpu.gpr[reg] + address)
            memory.print_mem(machine.u32(_print_loc))
        return opcodes.TOK_NL in (reg_token, close)
    _print_loc = address
    memory.print_mem(machine.u32(_print_loc))
    return following == opcodes.TOK_NL


def _dump_command(cmd: Command, token: int) -> None:
    """``dump``/``dumpnative ["FILE"]``: the user code as raw words."""
    global _prev_cmd
    filename: str
    if token == opcodes.TOK_STR:
        filename = _value_str()
    elif token == opcodes.TOK_NL:
        filename = "spim.dump"
    else:
        console.error(
            f'usage: {"dump" if cmd == Command.DUMP else "dumpnative"} [ "filename" ]\n'
        )
        return
    try:
        out: BinaryIO = open(console.to_bytes(filename), "wb")  # noqa: SIM115
    except OSError as problem:
        console.error(f"{filename}: {problem.strerror}\n")
        return
    instruction.user_kernel_text_segment(False)
    start: int = symbol_table.find_symbol_address(layout.END_OF_TRAP_HANDLER_SYMBOL)
    end: int = instruction.current_text_pc()
    byte_order: str = (
        "big" if cmd == Command.DUMP else "little"
    )  # network order for dump
    words: int = 0
    with out:
        for addr in range(start, end, 4):
            code: int = machine.u32(instruction.inst_encode(memory.read_inst(addr)))
            out.write(code.to_bytes(4, byte_order))
            words += 1
    console.error(
        f"Dumped {words} words starting at 0x{start:08x} to file {filename}\n"
    )
    _prev_cmd = cmd


def _print_reg(number: int) -> None:
    """``print $N``."""
    value: int = cpu.gpr[number] if 0 <= number < 32 else 0  # C reads past gpr[]
    console.message(f"Reg {number} = 0x{machine.u32(value):08x} ({value})\n")


def _print_fp_reg(number: int) -> None:
    """``print $fN``: as a double too when N is even."""
    number &= 31  # C would read past the register file
    if number & 1 == 0:
        console.message(
            f"FP reg {number} = {c_format('%g', regs.fpr_d(number))} (double)\n"
        )
    console.message(
        f"FP reg {number} = {c_format('%g', regs.fpr_s(number))} (single)\n"
    )


def _print_reg_from_string(name: str) -> bool:
    """``print pc`` and the other named special registers; False if none."""
    # C's tolower in the "C" locale changes only ASCII letters.
    lowered: str = console.from_bytes(
        console.to_bytes(name[:100]).lower()
    ).removeprefix("$")
    cp0: list[int] = cpu.cpr[0]
    special: dict[str, tuple[str, int]] = {
        "pc": ("PC", cpu.pc),
        "hi": ("HI", cpu.hi),
        "lo": ("LO", cpu.lo),
        "fpcond": ("FCSR", cpu.cpr[1][regs.FCSR_REG]),
        "cause": ("Cause", cp0[regs.CP0_CAUSE]),
        "epc": ("EPC", cp0[regs.CP0_EPC]),
        "status": ("Status", cp0[regs.CP0_STATUS]),
        "badvaddr": ("BadVAddr", cp0[regs.CP0_BADVADDR]),
    }
    if lowered not in special:
        return False
    label: str
    value: int
    label, value = special[lowered]
    console.message(f"{label} = 0x{machine.u32(value):08x} ({machine.s32(value)})\n")
    return True
