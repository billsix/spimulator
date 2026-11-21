#!/usr/bin/env python3
"""Print the built-in dictionary block of one rung of the Forth ladder.

The Forth ladder (tasks/reference/forth-incremental-versions.md) lays out
jonesforth's dictionary headers by hand in .data, one per built-in word, each
linked to the one before it.  From rung 4 on that block is sixty-odd headers
long, so this script writes it, and the block is pasted into the rung's .asm,
where it is then read and edited as ordinary source.  The script is an
authoring aid: nothing builds from it.

Usage:
    dictionary.py RUNG            # print rung RUNG's dictionary block
"""

import dataclasses
import sys

F_IMMED: int = 0x80


@dataclasses.dataclass(frozen=True)
class Entry:
    """One built-in word.

    Attributes:
        name: the Forth name, as FIND matches it.
        label: the asm label of the codeword cell (`name_<label>` heads it).
        first: the first rung that has the word.
        last: the last rung that has the word.
        immediate: whether the header carries F_IMMED.
        body: the cells after DOCOL, one .word line each, for a word written
            in Forth; empty for a primitive (codeword `code_<label>`).
    """

    name: str
    label: str
    first: int
    last: int = 9
    immediate: bool = False
    body: tuple[str, ...] = ()


def prim(
    name: str, label: str, first: int, last: int = 9, immediate: bool = False
) -> Entry:
    """A primitive: its codeword is the address of `code_<label>`."""
    return Entry(name=name, label=label, first=first, last=last, immediate=immediate)


INTERPRET_4: tuple[str, ...] = (
    "WORD, FIND              # cells 0-1: ( -- entry | 0 )",
    "QDUP, ZBRANCH, 16       # cells 2-4: not found -> cell 8",
    "TCFA, EXECUTE, EXIT     # cells 5-7: run the word",
    "LIT, 63, EMIT, EXIT     # cells 8-11: an unknown word prints ?",
)

INTERPRET_5: tuple[str, ...] = (
    "WORD, TWODUP, FIND      # cells 0-2: ( addr len entry|0 )",
    "QDUP, ZBRANCH, 24       # cells 3-5: not found -> cell 11",
    "NROT, TWODROP           # cells 6-7: ( entry )",
    "TCFA, EXECUTE, EXIT     # cells 8-10: run the word",
    "NUMBER                  # cell 11: ( addr len -- n unparsed )",
    "ZBRANCH, 20             # cells 12-13: a number -> cell 18",
    "DROP, LIT, 63, EMIT     # cells 14-17: neither: print ?",
    "EXIT                    # cell 18",
)

INTERPRET_6: tuple[str, ...] = (
    "WORD, TWODUP, FIND      # cells 0-2: ( addr len entry|0 )",
    "QDUP, ZBRANCH, 92       # cells 3-5: not found -> cell 28",
    "NROT, TWODROP           # cells 6-7: ( entry )",
    "DUP, INCR4, FETCHBYTE   # cells 8-10: the length/flags byte",
    "LIT, 128, AND           # cells 11-13: F_IMMED set?",
    "ZBRANCH, 16             # cells 14-15: no -> cell 19",
    "TCFA, EXECUTE, EXIT     # cells 16-18: immediate: run it now",
    "TCFA, STATE, FETCH      # cells 19-21: ( xt state )",
    "ZBRANCH, 12             # cells 22-23: interpreting -> cell 26",
    "COMMA, EXIT             # cells 24-25: compiling: append the xt",
    "EXECUTE, EXIT           # cells 26-27: interpreting: run it",
    "NUMBER                  # cell 28: ( addr len -- n unparsed )",
    "ZBRANCH, 24             # cells 29-30: a number -> cell 36",
    "DROP, LIT, 63, EMIT     # cells 31-34: neither: print ?",
    "EXIT                    # cell 35",
    "STATE, FETCH            # cells 36-37: compiling a number?",
    "ZBRANCH, 20             # cells 38-39: interpreting -> cell 44",
    "LIT, LIT, COMMA, COMMA  # cells 40-43: append LIT n",
    "EXIT                    # cell 44",
)

ENTRIES: tuple[Entry, ...] = (
    prim("DROP", "DROP", 4),
    prim("SWAP", "SWAP", 4),
    prim("DUP", "DUP", 4),
    prim("OVER", "OVER", 4),
    prim("ROT", "ROT", 4),
    prim("-ROT", "NROT", 4),
    prim("2DROP", "TWODROP", 4),
    prim("2DUP", "TWODUP", 4),
    prim("2SWAP", "TWOSWAP", 4),
    prim("?DUP", "QDUP", 4),
    prim("1+", "INCR", 4),
    prim("1-", "DECR", 4),
    prim("4+", "INCR4", 4),
    prim("4-", "DECR4", 4),
    prim("+", "ADD", 4),
    prim("-", "SUB", 4),
    prim("*", "MUL", 4),
    prim("/MOD", "DIVMOD", 4),
    prim("=", "EQU", 4),
    prim("<>", "NEQU", 4),
    prim("<", "LT", 4),
    prim(">", "GT", 4),
    prim("<=", "LE", 4),
    prim(">=", "GE", 4),
    prim("0=", "ZEQU", 4),
    prim("0<>", "ZNEQU", 4),
    prim("0<", "ZLT", 4),
    prim("0>", "ZGT", 4),
    prim("0<=", "ZLE", 4),
    prim("0>=", "ZGE", 4),
    prim("AND", "AND", 4),
    prim("OR", "OR", 4),
    prim("XOR", "XOR", 4),
    prim("INVERT", "INVERT", 4),
    prim("EXIT", "EXIT", 4),
    prim("LIT", "LIT", 4),
    prim("!", "STORE", 4),
    prim("@", "FETCH", 4),
    prim("+!", "ADDSTORE", 4),
    prim("-!", "SUBSTORE", 4),
    prim("C!", "STOREBYTE", 4),
    prim("C@", "FETCHBYTE", 4),
    prim("CMOVE", "CMOVE", 4),
    prim("STATE", "STATE", 6),
    prim("HERE", "HERE", 6),
    prim("LATEST", "LATEST", 4),
    prim("S0", "SZ", 7),
    prim("BASE", "BASE", 5),
    prim("R0", "RZ", 7),
    prim("DOCOL", "DOCOL_CONST", 9),
    prim("F_IMMED", "F_IMMED_CONST", 9),
    prim("F_HIDDEN", "F_HIDDEN_CONST", 9),
    prim("F_LENMASK", "F_LENMASK_CONST", 9),
    prim(">R", "TOR", 4),
    prim("R>", "FROMR", 4),
    prim("RSP@", "RSPFETCH", 4),
    prim("RSP!", "RSPSTORE", 4),
    prim("RDROP", "RDROP", 4),
    prim("DSP@", "DSPFETCH", 4),
    prim("DSP!", "DSPSTORE", 4),
    prim("KEY", "KEY", 4),
    prim("EMIT", "EMIT", 4),
    prim("WORD", "WORD", 4),
    prim("NUMBER", "NUMBER", 5),
    prim("FIND", "FIND", 4),
    prim(">CFA", "TCFA", 4),
    Entry(name=">DFA", label="TDFA", first=9, body=("TCFA, INCR4, EXIT",)),
    prim("CREATE", "CREATE", 6),
    prim(",", "COMMA", 6),
    prim("[", "LBRAC", 6, immediate=True),
    prim("]", "RBRAC", 6),
    Entry(
        name=":",
        label="COLON",
        first=6,
        body=(
            "WORD, CREATE            # the header, named by the next word",
            "LIT, docol, COMMA       # the codeword: DOCOL",
            "LATEST, FETCH, HIDDEN   # hide the word until ; ends it",
            "RBRAC, EXIT             # STATE = 1: compile",
        ),
    ),
    Entry(
        name=";",
        label="SEMICOLON",
        first=6,
        immediate=True,
        body=(
            "LIT, EXIT, COMMA        # end the body with EXIT",
            "LATEST, FETCH, HIDDEN   # unhide the word",
            "LBRAC, EXIT             # STATE = 0: interpret",
        ),
    ),
    prim("IMMEDIATE", "IMMEDIATE", 6, immediate=True),
    prim("HIDDEN", "HIDDEN", 6),
    Entry(name="HIDE", label="HIDE", first=6, body=("WORD, FIND, HIDDEN, EXIT",)),
    prim("'", "TICK", 6),
    prim("BRANCH", "BRANCH", 4),
    prim("0BRANCH", "ZBRANCH", 4),
    prim("LITSTRING", "LITSTRING", 9),
    prim("TELL", "TELL", 7),
    Entry(
        name="QUIT",
        label="QUIT",
        first=4,
        last=6,
        body=("INTERPRET, BRANCH, -8   # loop forever",),
    ),
    Entry(
        name="QUIT",
        label="QUIT",
        first=7,
        body=(
            "RZ, RSPSTORE            # R0 RSP!: empty the return stack",
            "INTERPRET, BRANCH, -8   # loop forever",
        ),
    ),
    Entry(name="INTERPRET", label="INTERPRET", first=4, last=4, body=INTERPRET_4),
    Entry(name="INTERPRET", label="INTERPRET", first=5, last=5, body=INTERPRET_5),
    Entry(name="INTERPRET", label="INTERPRET", first=6, last=6, body=INTERPRET_6),
    prim("INTERPRET", "INTERPRET", 7),
    prim("CHAR", "CHAR", 7),
    prim("EXECUTE", "EXECUTE", 4),
    prim(".", "DOT", 4, last=7),
    prim("CR", "CR", 4, last=7),
    prim("BYE", "BYE", 4),
    Entry(
        name="HI",
        label="HI",
        first=4,
        last=4,
        body=(
            "LIT, 72, EMIT, LIT, 73, EMIT   # H I",
            "LIT, 10, EMIT, EXIT            # newline",
        ),
    ),
    Entry(
        name="STAR",
        label="STAR",
        first=4,
        last=4,
        body=("LIT, 42, EMIT, EXIT            # *",),
    ),
)


def header(entry: Entry, link: str) -> str:
    """The .data lines of ENTRY's header, linked to the header LINK."""
    flags: str = " | F_IMMED (128)" if entry.immediate else ""
    length: int = len(entry.name) | (F_IMMED if entry.immediate else 0)
    codeword: str = "docol" if entry.body else f"code_{entry.label}"
    lines: list[str] = [
        f"name_{entry.label}:",
        f"        .word {link}",
        f"        .byte {length:<26}# length {len(entry.name)}{flags}",
        f'        .ascii "{entry.name}"',
        "        .align 2",
        f"{entry.label + ':':<10} .word {codeword}",
        *(f"        .word {cell}" for cell in entry.body),
    ]
    return "\n".join(lines)


def block(rung: int) -> str:
    """Rung RUNG's whole dictionary, oldest word first."""
    entries: list[Entry] = [e for e in ENTRIES if e.first <= rung <= e.last]
    links: list[str] = ["0"] + [f"name_{e.label}" for e in entries[:-1]]
    headers: str = "\n\n".join(
        header(e, link) for e, link in zip(entries, links, strict=True)
    )
    return f"{headers}\n\n# LATEST starts at name_{entries[-1].label}"


if __name__ == "__main__":
    print(block(int(sys.argv[1])))
