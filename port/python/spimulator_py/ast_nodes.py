"""The abstract syntax tree the parser builds, and its printers (ast.c).

The parser turns a file into a tree of :class:`Node` s: one per instruction,
directive and label, with a pseudo-instruction (``li``, ``bge``, ...) as a
``PSEUDO`` node whose children are the real instructions it expands to.
``parser.emit_ast`` then walks the tree and stores each node into memory.

Three teaching views print the tree: ``-print-ast`` (indented text),
``-print-ast-json`` (one JSON line) and ``-show-expansion`` (just the
pseudo-instructions).  Their output matches the C engine byte for byte: the
text dumps name each instruction's mnemonic (``op=addu``), the JSON dump its
opcode token number.

Like the C ``ast_node`` (a tagged union), one class holds every kind's
fields; each kind uses only some of them.
"""

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Any, assert_never

from . import opcodes, scanner
from .cformat import c_format
from .inst import ImmExpr


class Kind(Enum):
    """The kinds of node."""

    INST_R = auto()
    INST_R_SHIFT = auto()
    INST_I = auto()
    INST_J = auto()
    INST_FP_R = auto()
    INST_FP_COMPARE = auto()
    PSEUDO = auto()
    DATA_BYTE = auto()
    DATA_HALF = auto()
    DATA_WORD = auto()
    DATA_FLOAT = auto()
    DATA_DOUBLE = auto()
    DATA_STRING = auto()
    DIR_TEXT = auto()
    DIR_DATA = auto()
    DIR_KTEXT = auto()
    DIR_KDATA = auto()
    DIR_ALIGN = auto()
    DIR_SPACE = auto()
    DIR_GLOBL = auto()
    DIR_EXTERN = auto()
    DIR_COMM = auto()
    LABEL_DEF = auto()
    FILE = auto()


@dataclass(eq=False)
class Node:
    """One node of the tree.

    Attributes:
        kind: what the node is.
        source_line: the line number when the node was built.
        src_text: the source line ("NNN: text") when the node was built;
            emit_ast gives it to the instructions the node stores.
        op, rd, rs, rt, shamt, fd, fs, ft, cc: an instruction's opcode token
            and fields.
        imm: an I-type immediate or a J-type target.
        children: a FILE's statements or a PSEUDO's expansion.
        mnemonic: a PSEUDO's name.
        exprs: a .byte/.half/.word list.
        doubles: a .float/.double list.
        string: a .ascii/.asciiz string's bytes.
        null_terminate: the string is .asciiz.
        has_start_addr, start_addr: a segment directive's address.
        number: .align's power, .space's or .extern/.comm's size, a
            constant label's value.
        name: a label, .globl, .extern or .comm name; a FILE's source.
        is_constant: a LABEL_DEF is ``NAME = EXPR``, not ``NAME:``.
    """

    kind: Kind
    source_line: int = 0
    src_text: str | None = None
    op: int = 0
    rd: int = 0
    rs: int = 0
    rt: int = 0
    shamt: int = 0
    fd: int = 0
    fs: int = 0
    ft: int = 0
    cc: int = 0
    imm: ImmExpr | None = None
    children: list["Node"] = field(default_factory=list)
    mnemonic: str = ""
    exprs: list[ImmExpr] = field(default_factory=list)
    doubles: list[float] = field(default_factory=list)
    string: bytes = b""
    null_terminate: bool = False
    has_start_addr: bool = False
    start_addr: int = 0
    number: int = 0
    name: str | None = None
    is_constant: bool = False


def make(kind: Kind, **fields: Any) -> Node:
    """A new node, stamped with the scanner's current line (C ``new_node``)."""
    node: Node = Node(kind, **fields)
    node.source_line = scanner.line_no
    node.src_text = scanner.source_line()
    return node


# --- the indented text printer (-print-ast, -show-expansion) -----------------


def _imm_text(expr: ImmExpr | None) -> str:
    """An immediate as ``-print-ast`` shows it."""
    if expr is None:
        return "(null)"
    elif expr.symbol is None:
        return str(expr.offset)
    else:
        text: str = expr.symbol.name
        if expr.offset > 0:
            text += f"+{expr.offset}"
        elif expr.offset < 0:
            text += str(expr.offset)
        else:
            pass  # a zero offset prints as the bare name
        return text + (" (pc-rel)" if expr.pc_relative else "")


def _string_text(data: bytes) -> str:
    """A string's bytes with C-style escapes, as ``-print-ast`` shows them."""
    out: str = ""
    for byte in data:
        if byte == 10:
            out += "\\n"
        elif byte == 9:
            out += "\\t"
        elif byte in b'\\"':
            out += "\\" + chr(byte)
        elif 0x20 <= byte < 0x7F:
            out += chr(byte)
        else:
            out += f"\\x{byte:02x}"
    return out


_SEGMENT_TAGS: dict[Kind, str] = {
    Kind.DIR_TEXT: "DIR_TEXT",
    Kind.DIR_DATA: "DIR_DATA",
    Kind.DIR_KTEXT: "DIR_KTEXT",
    Kind.DIR_KDATA: "DIR_KDATA",
}


def _describe(node: Node) -> str:
    """The text after ``[line N] `` for one node, without its children."""
    match node.kind:
        case Kind.FILE:
            return f"FILE source={node.name if node.name is not None else '(stdin)'}"
        case Kind.INST_R:
            return f"INST_R op={opcodes.name_of(node.op)} rd={node.rd} rs={node.rs} rt={node.rt}"
        case Kind.INST_R_SHIFT:
            return (
                f"INST_R_SHIFT op={opcodes.name_of(node.op)} rd={node.rd} rt={node.rt}"
                f" shamt={node.shamt}"
            )
        case Kind.INST_I:
            return (
                f"INST_I op={opcodes.name_of(node.op)} rt={node.rt} rs={node.rs}"
                f" imm={_imm_text(node.imm)}"
            )
        case Kind.INST_J:
            return f"INST_J op={opcodes.name_of(node.op)} target={_imm_text(node.imm)}"
        case Kind.INST_FP_R:
            return f"INST_FP_R op={opcodes.name_of(node.op)} fd={node.fd} fs={node.fs} ft={node.ft}"
        case Kind.INST_FP_COMPARE:
            return f"INST_FP_COMPARE op={opcodes.name_of(node.op)} fs={node.fs} ft={node.ft} cc={node.cc}"
        case Kind.PSEUDO:
            return f"PSEUDO mnemonic={node.mnemonic}"
        case Kind.DATA_BYTE | Kind.DATA_HALF | Kind.DATA_WORD:
            values: str = ", ".join(_imm_text(e) for e in node.exprs)
            return f"{node.kind.name} count={len(node.exprs)} values=[{values}]"
        case Kind.DATA_FLOAT | Kind.DATA_DOUBLE:
            numbers: str = ", ".join(c_format("%g", d) for d in node.doubles)
            return f"{node.kind.name} count={len(node.doubles)} values=[{numbers}]"
        case Kind.DATA_STRING:
            return (
                f"DATA_STRING len={len(node.string)}"
                f" null_term={'yes' if node.null_terminate else 'no'}"
                f' value="{_string_text(node.string)}"'
            )
        case Kind.DIR_TEXT | Kind.DIR_DATA | Kind.DIR_KTEXT | Kind.DIR_KDATA:
            tag: str = _SEGMENT_TAGS[node.kind]
            if node.has_start_addr:
                return f"{tag} start=0x{node.start_addr:08x}"
            return tag
        case Kind.DIR_ALIGN:
            return f"DIR_ALIGN n={node.number}"
        case Kind.DIR_SPACE:
            return f"DIR_SPACE size={node.number}"
        case Kind.DIR_GLOBL:
            return f"DIR_GLOBL name={node.name}"
        case Kind.DIR_EXTERN | Kind.DIR_COMM:
            return f"{node.kind.name} name={node.name} size={node.number}"
        case Kind.LABEL_DEF:
            if node.is_constant:
                return f"LABEL_DEF name={node.name} = {node.number} (constant)"
            return f"LABEL_DEF name={node.name} (placement)"
        case _:
            assert_never(node.kind)


def format_tree(node: Node, depth: int = 0) -> str:
    """The indented text dump of node and its descendants (C ``ast_print``)."""
    text: str = "  " * depth + f"[line {node.source_line}] {_describe(node)}\n"
    if node.kind in (Kind.FILE, Kind.PSEUDO):
        text += "".join(format_tree(child, depth + 1) for child in node.children)
    return text


# --- the JSON printer (-print-ast-json) -------------------------------------


def _json_string(text: bytes) -> str:
    """A JSON string literal of text, escaped as the C printer escapes it."""
    out: str = '"'
    escapes: dict[int, str] = {
        34: '\\"',
        92: "\\\\",
        10: "\\n",
        13: "\\r",
        9: "\\t",
        8: "\\b",
        12: "\\f",
    }
    for byte in text:
        if byte in escapes:
            out += escapes[byte]
        elif byte < 0x20:
            out += f"\\u{byte:04x}"
        else:
            out += chr(byte)
    return out + '"'


def _json_name(text: str) -> str:
    """A JSON string literal of a one-char-per-byte name."""
    return _json_string(text.encode("latin-1"))


def _json_imm(expr: ImmExpr | None) -> str:
    """An immediate as a JSON object."""
    if expr is None:
        return "null"
    symbol: str = _json_name(expr.symbol.name) if expr.symbol is not None else "null"
    pc_relative: str = "true" if expr.pc_relative else "false"
    return f'{{"offset":{expr.offset},"symbol":{symbol},"pc_relative":{pc_relative}}}'


def _json_fields(node: Node) -> str:
    """The JSON members after ``kind`` and ``source_line`` for one node."""
    mnemonic: str = _json_name(opcodes.name_of(node.op))
    match node.kind:
        case Kind.FILE:
            source: str = (
                f',"source_file":{_json_name(node.name)}'
                if node.name is not None
                else ""
            )
            return f'{source},"children":{_json_chain(node.children)}'
        case Kind.INST_R:
            return (
                f',"op":{node.op},"mnemonic":{mnemonic},'
                f'"rd":{node.rd},"rs":{node.rs},"rt":{node.rt}'
            )
        case Kind.INST_R_SHIFT:
            return (
                f',"op":{node.op},"mnemonic":{mnemonic},'
                f'"rd":{node.rd},"rt":{node.rt},"shamt":{node.shamt}'
            )
        case Kind.INST_I:
            return (
                f',"op":{node.op},"mnemonic":{mnemonic},'
                f'"rt":{node.rt},"rs":{node.rs},"imm":{_json_imm(node.imm)}'
            )
        case Kind.INST_J:
            return (
                f',"op":{node.op},"mnemonic":{mnemonic},"target":{_json_imm(node.imm)}'
            )
        case Kind.INST_FP_R:
            return (
                f',"op":{node.op},"mnemonic":{mnemonic},'
                f'"fd":{node.fd},"fs":{node.fs},"ft":{node.ft}'
            )
        case Kind.INST_FP_COMPARE:
            return (
                f',"op":{node.op},"mnemonic":{mnemonic},'
                f'"fs":{node.fs},"ft":{node.ft},"cc":{node.cc}'
            )
        case Kind.PSEUDO:
            return (
                f',"mnemonic":{_json_name(node.mnemonic)},'
                f'"children":{_json_chain(node.children)}'
            )
        case Kind.DATA_BYTE | Kind.DATA_HALF | Kind.DATA_WORD:
            values: str = ",".join(_json_imm(e) for e in node.exprs)
            return f',"count":{len(node.exprs)},"values":[{values}]'
        case Kind.DATA_FLOAT | Kind.DATA_DOUBLE:
            numbers: str = ",".join(c_format("%.17g", d) for d in node.doubles)
            return f',"count":{len(node.doubles)},"values":[{numbers}]'
        case Kind.DATA_STRING:
            null_terminate: str = "true" if node.null_terminate else "false"
            return (
                f',"length":{len(node.string)},"null_terminate":{null_terminate},'
                f'"value":{_json_string(node.string)}'
            )
        case Kind.DIR_TEXT | Kind.DIR_DATA | Kind.DIR_KTEXT | Kind.DIR_KDATA:
            return f',"start_addr":{node.start_addr}' if node.has_start_addr else ""
        case Kind.DIR_ALIGN:
            return f',"n":{node.number}'
        case Kind.DIR_SPACE:
            return f',"size":{node.number}'
        case Kind.DIR_GLOBL:
            return f',"name":{_json_name(node.name or "")}'
        case Kind.DIR_EXTERN | Kind.DIR_COMM:
            return f',"name":{_json_name(node.name or "")},"size":{node.number}'
        case Kind.LABEL_DEF:
            kind: str = "constant" if node.is_constant else "placement"
            value: str = f',"value":{node.number}' if node.is_constant else ""
            return f',"name":{_json_name(node.name or "")},"label_kind":"{kind}"{value}'
        case _:
            assert_never(node.kind)


def _json_chain(nodes: list[Node]) -> str:
    """A JSON array of nodes."""
    return "[" + ",".join(_json_node(n) for n in nodes) + "]"


def _json_node(node: Node) -> str:
    """One node as a JSON object."""
    return (
        f'{{"kind":"{node.kind.name}","source_line":{node.source_line}'
        f"{_json_fields(node)}}}"
    )


def format_json(node: Node) -> str:
    """The tree as one line of JSON (C ``ast_print_json``)."""
    return _json_node(node) + "\n"
