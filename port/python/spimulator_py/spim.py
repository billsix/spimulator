"""The command line and the REPL (the C engine's spim.c).

``main`` reads the C engine's flags, loads the ``-f`` files, and then either
runs the program (exit status: the program's, or 128 + the first fault's
ExceptionCode, 1 for undefined symbols, 2 for a parse error), dumps it
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
from . import (
    instruction as instruction_module,
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
        case Kind.LABEL_DEFINITION:
            line += (
                f"label  {event.name:<24s} defined at 0x{event.address:08x}"
                f"{'  (global)' if event.flag else ''}\n"
            )
        case Kind.TEXT_INSTRUCTION:
            text: str = instruction_module.instruction_to_string(event.address)
            line += f"text   0x{event.address:08x}  {text:<32s}  0x{event.value:08x}\n"
        case Kind.DATA_BYTE:
            line += (
                f"data   0x{event.address:08x}  byte    0x{event.value & 0xFF:02x}\n"
            )
        case Kind.DATA_HALF:
            line += (
                f"data   0x{event.address:08x}  half    0x{event.value & 0xFFFF:04x}\n"
            )
        case Kind.DATA_WORD:
            line += f"data   0x{event.address:08x}  word    0x{machine.u32(event.value):08x}\n"
        case Kind.DATA_DOUBLE:
            line += f"data   0x{event.address:08x}  double  {c_format('%.18g', event.double)}\n"
        case Kind.DATA_STRING:
            line += (
                f"data   0x{event.address:08x}  string  len={event.value}"
                f"{' (+\\0)' if event.flag else ''}\n"
            )
        case Kind.FORWARD_REF:
            line += (
                f"fref   0x{event.address:08x}  uses unresolved symbol '{event.name}'\n"
            )
        case Kind.FORWARD_RESOLVED:
            line += (
                f"fres   0x{event.address:08x}  patched '{event.name}'"
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
    last_file_index: int = -1
    print_usage: bool = False
    assemble: bool = False
    dump_user: bool = False
    dump_all: bool = False
    arguments: list[str] = ["spimulator-python", *argv]
    argc: int = len(arguments)
    environment_handler: bytes | None = os.environb.get(b"SPIM_EXCEPTION_HANDLER")
    if environment_handler is not None:
        exception_file_name = console.from_bytes(environment_handler)

    def next_argument(i: int) -> str | None:
        """argv[i], or None past the end (C would read NULL)."""
        return arguments[i] if i < argc else None

    i: int = 1
    while i < argc:
        argument: str = arguments[i]
        match argument:
            case _ if argument in _VALUE_FLAGS and i + 1 >= argc:
                # The flag's value is missing: report it, then the usage.
                console.error(f"spimulator: {argument} requires a value\n")
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
                exception_file_name = next_argument(i) or ""
                load_exception_handler = True
            case "-mapped_io" | "-mio":
                options.mapped_io = True
            case "-nomapped_io" | "-nmio":
                options.mapped_io = False
            case "-pseudo" | "-p":
                options.accept_pseudo_instructions = True
            case "-nopseudo" | "-np":
                options.accept_pseudo_instructions = False
            case "-quiet" | "-q":
                options.quiet = True
            case "-noquiet" | "-nq":
                options.quiet = False
            case "-noexplain" | "-nx":
                pass  # the teaching mode is off: there is none
            case _ if argument in ("-explain", "-x") or argument.startswith(
                ("-explain=", "-x=")
            ):
                console.error(EXPLAIN_UNSUPPORTED)
                return 2
            case "-stext" | "-st":
                i += 1
                options.initial_text_size = _c_atoi(next_argument(i))
            case "-sdata" | "-sd":
                i += 1
                options.initial_data_size = _c_atoi(next_argument(i))
            case "-ldata" | "-ld":
                i += 1
                options.initial_data_limit = machine.s32(_c_atoi(next_argument(i)))
            case "-sstack" | "-ss":
                i += 1
                options.initial_stack_size = _c_atoi(next_argument(i))
            case "-lstack" | "-ls":
                i += 1
                options.initial_stack_limit = machine.s32(_c_atoi(next_argument(i)))
            case "-sktext" | "-skt":
                i += 1
                options.initial_kernel_text_size = _c_atoi(next_argument(i))
            case "-skdata" | "-skd":
                i += 1
                options.initial_kernel_data_size = _c_atoi(next_argument(i))
            case "-lkdata" | "-lkd":
                i += 1
                options.initial_kernel_data_limit = machine.s32(
                    _c_atoi(next_argument(i))
                )
            case _ if (
                argument in ("-file", "-f") and i + 1 < argc
            ) or not argument.startswith("-"):
                # `-f FILE` (may repeat: the files share one symbol table), or a
                # bare FILE, which is also argv[0], followed by the program's args.
                is_explicit_file: bool = argument.startswith("-")
                if not is_explicit_file and assembly_file_loaded:
                    break
                file_index: int = i + 1 if is_explicit_file else i
                if not world_initialized:
                    spim_utils.initialize_world(_exception_files(), not options.quiet)
                    world_initialized = True
                assembly_file_attempted = True
                if not spim_utils.read_assembly_file(arguments[file_index]):
                    break
                assembly_file_loaded = True
                last_file_index = file_index
                if is_explicit_file:
                    i = file_index + 1
                    continue
                program_argv = arguments[file_index:]
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
                path: str = arguments[i]
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
                console.error(f"\nUnknown argument: {argument} (ignored)\n")
                print_usage = True
        i += 1

    if assembly_file_loaded and not program_argv:
        program_argv = arguments[last_file_index:]
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
            _dump_data_segment(False)
            _dump_text_segment(False)
        elif dump_all:
            _dump_data_segment(True)
            _dump_text_segment(True)
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
                # A fault the program did not survive cleanly: 128 + ExceptionCode, like
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
    instruction_module.user_kernel_text_segment(False)
    start: int = symbol_table.find_symbol_address(layout.END_OF_TRAP_HANDLER_SYMBOL)
    end: int = instruction_module.current_text_location()
    words: list[str] = [
        f"0x{machine.u32(instruction_module.instruction_encode(memory.read_instruction(address))):x}"
        for address in range(start, end, 4)
    ]
    text: str = f".text # 0x{start:x} .. 0x{end:x}\n.word " + ", ".join(words) + "\n"
    data.user_kernel_data_segment(False)
    start = 0 if options.bare_machine else layout.DATA_BOTTOM
    end = data.current_data_location()
    if end > start:
        values: list[str] = [
            f"0x{machine.u32(memory.read_word(address)):x}"
            for address in range(start, end, 4)
        ]
        text += f".data # 0x{start:x} .. 0x{end:x}\n.word " + ", ".join(values) + "\n"
    with out:
        out.write(console.to_bytes(text))
    return False


def _write_file(name: str, text: str) -> None:
    """Write text to the file name in the current directory."""
    with open(name, "wb") as out:
        out.write(console.to_bytes(text))


def _dump_data_segment(kernel_also: bool) -> None:
    """``-dump``/``-full_dump``: the data segment(s) to data.asm."""
    if kernel_also:
        _write_file("data.asm", display.format_data_segments())
    else:
        _write_file(
            "data.asm",
            "\tDATA\n" + display.format_memory(layout.DATA_BOTTOM, memory.data_top),
        )


def _dump_text_segment(kernel_also: bool) -> None:
    """``-dump``/``-full_dump``: the text segment(s) to text.asm."""
    if kernel_also:
        text: str = (
            display.format_instructions(layout.TEXT_BOTTOM, memory.text_top)
            + "\n\tKERNEL\n"
            + display.format_instructions(
                layout.KERNEL_TEXT_BOTTOM, memory.kernel_text_top
            )
        )
    else:
        text = "\n\tUSER TEXT SEGMENT\n" + display.format_instructions(
            layout.TEXT_BOTTOM, memory.text_top
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
    scanner.initialize(None)
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
    PRINT_SYMBOL = 6
    PRINT_ALL_REGISTERS = 7
    REINITIALIZE = 8
    ASM = 9
    REDO = 10
    NOP = 11
    HELP = 12
    CONTINUE = 13
    SET_BREAKPOINT = 14
    DELETE_BREAKPOINT = 15
    LIST_BREAKPOINT = 16
    DUMP_NATIVE = 17
    DUMP = 18
    DISASSEMBLY = 19
    ARGUMENTS = 20


_previous_command: Command = Command.NOP
_previous_token: int = 0
_run_address: int = 0
_step_count: int = 0
_print_location: int = 0
_breakpoint_address: int = 0


def _read_token() -> int:
    """The next token of the command line; end of input exits (C read_token).

    Raises:
        SystemExit: at end of input.
    """
    token: int = scanner.advance()
    if token == opcodes.TOKEN_EOF:
        console.console_to_spim()
        raise SystemExit(0)
    return token


def _flush_to_newline() -> None:
    """Skip the rest of the command line, including its newline."""
    while _read_token() != opcodes.TOKEN_NEWLINE:
        pass


def _get_optional_integer() -> int:
    """An optional integer argument (0 if there is none); flushes the line."""
    token: int = _read_token()
    if token == opcodes.TOKEN_INTEGER:
        _flush_to_newline()
        return _value_integer()
    if token != opcodes.TOKEN_NEWLINE:
        _flush_to_newline()
    return 0


def _value_integer() -> int:
    """The scanner's last value, as an int."""
    value: int | float | str = scanner.scan_value
    return value if isinstance(value, int) else 0


def _value_string() -> str:
    """The scanner's last value, as a string."""
    value: int | float | str = scanner.scan_value
    return value if isinstance(value, str) else ""


def _string_prefix(text: str, command: str, min_match: int) -> bool:
    """True if text is a prefix of command at least min_match long."""
    return command.startswith(text) and len(text) >= min_match


_COMMANDS: list[tuple[str, int, Command]] = [
    ("exit", 2, Command.EXIT),
    ("quit", 2, Command.EXIT),
    ("print", 1, Command.PRINT),
    ("print_symbols", 7, Command.PRINT_SYMBOL),
    ("print_all_regs", 7, Command.PRINT_ALL_REGISTERS),
    ("run", 2, Command.RUN),
    ("args", 2, Command.ARGUMENTS),
    ("read", 2, Command.READ),
    ("load", 2, Command.READ),
    ("reinitialize", 6, Command.REINITIALIZE),
    ("step", 1, Command.STEP),
    ("help", 1, Command.HELP),
    ("continue", 1, Command.CONTINUE),
    ("breakpoint", 2, Command.SET_BREAKPOINT),
    ("delete", 1, Command.DELETE_BREAKPOINT),
    ("list", 2, Command.LIST_BREAKPOINT),
    ("dumpnative", 5, Command.DUMP_NATIVE),
    ("dump", 4, Command.DUMP),
    ("disasm", 3, Command.DISASSEMBLY),  # "di" is the MIPS32r2 instruction, a keyword
]


def _read_assembly_command() -> Command:
    """Read a command word; the first matching (prefix, min) entry wins."""
    token: int = _read_token()
    if token == opcodes.TOKEN_NEWLINE:
        return Command.REDO
    elif token != opcodes.TOKEN_IDENTIFIER:
        return Command.UNKNOWN
    else:
        word: str = _value_string()
        for command, min_match, number in _COMMANDS:
            if _string_prefix(word, command, min_match):
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
        console.message(f"Breakpoint encountered at 0x{cpu.program_counter:08x}\n")


def parse_spim_command(redo: bool) -> bool:
    """Execute one REPL command (or, with redo, the previous one again).

    Returns:
        bool: True if the command was a blank line (redo the previous one).

    Raises:
        SystemExit: ``exit``/``quit``, or end of input.
    """
    global \
        _previous_command, \
        _previous_token, \
        _run_address, \
        _step_count, \
        _print_location, \
        _breakpoint_address
    global program_argv
    command: Command = _previous_command if redo else _read_assembly_command()
    if command == Command.EXIT:
        console.console_to_spim()
        raise SystemExit(0)
    match command:
        case Command.READ:
            token: int = _previous_token if redo else _read_token()
            if not redo:
                _flush_to_newline()
            if token == opcodes.TOKEN_STRING:
                spim_utils.read_assembly_file(_value_string())
                scanner.pop_source()
            else:
                console.error("Must supply a filename to read\n")
            _previous_command = Command.READ
        case Command.RUN:
            _run_address = (
                _run_address if redo else machine.u32(_get_optional_integer())
            )
            if _run_address == 0:
                _run_address = spim_utils.starting_address()
            spim_utils.initialize_run_stack(program_argv)
            console.console_to_program()
            if _run_address != 0:
                undefined: str | None = symbol_table.undefined_symbol_string()
                if undefined is not None:
                    console.message(
                        f"The following symbols are undefined:\n{undefined}\n"
                    )
                _report_breakpoint(
                    spim_utils.run_program(
                        _run_address, layout.DEFAULT_RUN_STEPS, False, False
                    )
                )
            console.console_to_spim()
            _previous_command = Command.RUN
        case Command.CONTINUE:
            if cpu.program_counter != 0:
                console.console_to_program()
                _report_breakpoint(
                    spim_utils.run_program(
                        cpu.program_counter, layout.DEFAULT_RUN_STEPS, False, True
                    )
                )
                console.console_to_spim()
            _previous_command = Command.CONTINUE
        case Command.STEP:
            _step_count = _step_count if redo else _get_optional_integer()
            address: int = (
                spim_utils.starting_address()
                if cpu.program_counter == 0
                else cpu.program_counter
            )
            if _step_count == 0:
                _step_count = 1
            if address != 0:
                console.console_to_program()
                _report_breakpoint(
                    spim_utils.run_program(address, _step_count, True, True)
                )
                console.console_to_spim()
            _previous_command = Command.STEP
        case Command.PRINT:
            token = _previous_token if redo else _read_token()
            consumed_newline: bool = False
            match token:
                case opcodes.TOKEN_REGISTER:
                    _print_location = _print_location + 1 if redo else _value_integer()
                    _print_register(_print_location)
                case opcodes.TOKEN_FLOATING_POINT_REGISTER:
                    _print_location = _print_location + 2 if redo else _value_integer()
                    _print_floating_point_register(_print_location)
                case opcodes.TOKEN_INTEGER:
                    if redo:
                        _print_location = machine.s32(_print_location + 4)
                        memory.print_memory(machine.u32(_print_location))
                    else:
                        consumed_newline = _print_address(_value_integer())
                case opcodes.TOKEN_IDENTIFIER:
                    name: str = _value_string()
                    if not _print_register_from_string(name):
                        _print_location = (
                            machine.s32(_print_location + 4)
                            if redo
                            else machine.s32(symbol_table.find_symbol_address(name))
                        )
                        if _print_location != 0:
                            memory.print_memory(machine.u32(_print_location))
                        else:
                            console.error(f"Unknown label: {name}\n")
                case _:
                    console.error("Print what?\n")
            if not redo and not consumed_newline:
                _flush_to_newline()
            _previous_command = Command.PRINT
            _previous_token = token
        case Command.PRINT_SYMBOL:
            symbol_table.print_symbols()
            if not redo:
                _flush_to_newline()
            _previous_command = Command.NOP
        case Command.PRINT_ALL_REGISTERS:
            token = _previous_token if redo else _read_token()
            hex_flag: bool = (
                token == opcodes.TOKEN_IDENTIFIER and _value_string() == "hex"
            )
            console.message(display.format_registers(hex_flag, hex_flag) + "\n")
            if (
                not redo and token != opcodes.TOKEN_NEWLINE
            ):  # a bare newline ends the line already
                _flush_to_newline()
            _previous_command = Command.NOP
        case Command.REINITIALIZE:
            _flush_to_newline()
            spim_utils.initialize_world(_exception_files(), not options.quiet)
            spim_utils.initialize_run_stack(program_argv)
            spim_utils.write_startup_message()
            _previous_command = Command.NOP
        case Command.ASM:
            parser.parse_file()
            _previous_command = Command.ASM
        case Command.REDO:
            return True
        case Command.NOP:
            _previous_command = Command.NOP
        case Command.ARGUMENTS:
            argv0: str = program_argv[0] if program_argv else "<repl>"
            words: list[str] = []
            while (token := _read_token()) not in (opcodes.TOKEN_NEWLINE, 0):
                if token in (opcodes.TOKEN_STRING, opcodes.TOKEN_IDENTIFIER):
                    words.append(_value_string())
                elif token == opcodes.TOKEN_INTEGER:
                    words.append(str(_value_integer()))
                else:
                    pass  # any other token is not an argument word
            program_argv = [argv0, *words]
            _previous_command = Command.ARGUMENTS
        case Command.HELP:
            if not redo:
                _flush_to_newline()
            console.message(HELP_TEXT)
            _previous_command = Command.HELP
        case Command.SET_BREAKPOINT | Command.DELETE_BREAKPOINT:
            token = _previous_token if redo else _read_token()
            if (
                not redo and token != opcodes.TOKEN_NEWLINE
            ):  # a bare newline ends the line already
                _flush_to_newline()
            if token == opcodes.TOKEN_INTEGER:
                _breakpoint_address = (
                    machine.u32(_breakpoint_address + 4)
                    if redo
                    else machine.u32(_value_integer())
                )
            elif token == opcodes.TOKEN_IDENTIFIER:
                _breakpoint_address = (
                    machine.u32(_breakpoint_address + 4)
                    if redo
                    else symbol_table.find_symbol_address(_value_string())
                )
            else:
                console.error("Must supply an address for breakpoint\n")
                return False
            if command == Command.SET_BREAKPOINT:
                spim_utils.add_breakpoint(_breakpoint_address)
            else:
                spim_utils.delete_breakpoint(_breakpoint_address)
            _previous_command = command
        case Command.LIST_BREAKPOINT:
            if not redo:
                _flush_to_newline()
            spim_utils.list_breakpoints()
            _previous_command = Command.LIST_BREAKPOINT
        case Command.DUMP | Command.DUMP_NATIVE:
            _dump_command(command, _previous_token if redo else _read_token())
        case Command.DISASSEMBLY:
            display.print_text_listing()
            if not redo:
                _flush_to_newline()
            _previous_command = Command.NOP
        case _:
            while _read_token() != opcodes.TOKEN_NEWLINE:
                pass
            console.error("Unknown spim command\n")
    return False


def _print_address(address: int) -> bool:
    """``print ADDR`` or ``print N($reg)``.

    Returns:
        bool: True if the command line's newline was already read.
    """
    global _print_location
    following: int = _read_token()
    if following == ord("("):
        register_token: int = _read_token()
        register_number: int = (
            _value_integer() if register_token == opcodes.TOKEN_REGISTER else -1
        )
        close: int = _read_token() if register_token == opcodes.TOKEN_REGISTER else 0
        if register_number < 0 or close != ord(")"):
            console.error("expected $reg) after '(' in print N($reg) form\n")
        else:
            _print_location = machine.s32(
                cpu.general_purpose_registers[register_number] + address
            )
            memory.print_memory(machine.u32(_print_location))
        return opcodes.TOKEN_NEWLINE in (register_token, close)
    _print_location = address
    memory.print_memory(machine.u32(_print_location))
    return following == opcodes.TOKEN_NEWLINE


def _dump_command(command: Command, token: int) -> None:
    """``dump``/``dumpnative ["FILE"]``: the user code as raw words."""
    global _previous_command
    filename: str
    if token == opcodes.TOKEN_STRING:
        filename = _value_string()
    elif token == opcodes.TOKEN_NEWLINE:
        filename = "spim.dump"
    else:
        console.error(
            f'usage: {"dump" if command == Command.DUMP else "dumpnative"} [ "filename" ]\n'
        )
        return
    try:
        out: BinaryIO = open(console.to_bytes(filename), "wb")  # noqa: SIM115
    except OSError as problem:
        console.error(f"{filename}: {problem.strerror}\n")
        return
    instruction_module.user_kernel_text_segment(False)
    start: int = symbol_table.find_symbol_address(layout.END_OF_TRAP_HANDLER_SYMBOL)
    end: int = instruction_module.current_text_location()
    byte_order: str = (
        "big" if command == Command.DUMP else "little"
    )  # network order for dump
    words: int = 0
    with out:
        for address in range(start, end, 4):
            code: int = machine.u32(
                instruction_module.instruction_encode(memory.read_instruction(address))
            )
            out.write(code.to_bytes(4, byte_order))
            words += 1
    console.error(
        f"Dumped {words} words starting at 0x{start:08x} to file {filename}\n"
    )
    _previous_command = command


def _print_register(number: int) -> None:
    """``print $N``."""
    value: int = (
        cpu.general_purpose_registers[number] if 0 <= number < 32 else 0
    )  # C reads past gpr[]
    console.message(f"Reg {number} = 0x{machine.u32(value):08x} ({value})\n")


def _print_floating_point_register(number: int) -> None:
    """``print $fN``: as a double too when N is even."""
    number &= 31  # C would read past the register file
    if number & 1 == 0:
        console.message(
            f"FP reg {number} = {c_format('%g', regs.floating_point_register_double(number))} (double)\n"
        )
    console.message(
        f"FP reg {number} = {c_format('%g', regs.floating_point_register_single(number))} (single)\n"
    )


def _print_register_from_string(name: str) -> bool:
    """``print pc`` and the other named special registers; False if none."""
    # C's tolower in the "C" locale changes only ASCII letters.
    lowered: str = console.from_bytes(
        console.to_bytes(name[:100]).lower()
    ).removeprefix("$")
    coprocessor_0: list[int] = cpu.coprocessor_registers[0]
    special: dict[str, tuple[str, int]] = {
        "pc": ("PC", cpu.program_counter),
        "hi": ("HI", cpu.multiply_divide_high),
        "lo": ("LO", cpu.multiply_divide_low),
        "fpcond": (
            "FCSR",
            cpu.coprocessor_registers[1][
                regs.FLOATING_POINT_CONTROL_STATUS_REGISTER_NUMBER
            ],
        ),
        "cause": ("Cause", coprocessor_0[regs.COPROCESSOR_0_CAUSE]),
        "epc": ("EPC", coprocessor_0[regs.COPROCESSOR_0_EXCEPTION_PROGRAM_COUNTER]),
        "status": ("Status", coprocessor_0[regs.COPROCESSOR_0_STATUS]),
        "badvaddr": ("BadVAddr", coprocessor_0[regs.COPROCESSOR_0_BAD_VIRTUAL_ADDRESS]),
    }
    if lowered not in special:
        return False
    label: str
    value: int
    label, value = special[lowered]
    console.message(f"{label} = 0x{machine.u32(value):08x} ({machine.s32(value)})\n")
    return True
