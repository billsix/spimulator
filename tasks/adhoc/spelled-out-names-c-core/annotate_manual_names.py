#!/usr/bin/env python3
"""Put the manual's (or the old idiom's) name beside each spelled-out definition.

Decisions 1, 2 and 4 of ``tasks/spelled-out-names-c-core.md``: an architecture name is spelled
out with the MIPS manual's name in a comment (``coprocessor_0_status /* CP0 Status */``), a
register a student names in assembly with the name they type (``/* $sp */``), and the
allocation wrappers with their GNU-style name (``/* xmalloc in GNU style */``). For each
definition in ANNOTATIONS this finds the first line that defines the name and adds the tag:
into the comment already on the definition's last line (``/* TAG: old text */``), or as a new
trailing comment. Run it once; a second run would tag twice.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-c-core/annotate_manual_names.py
"""

import pathlib
import re
import sys

REGISTERS: dict[str, str] = {
    "general_purpose_registers": "the GPRs, $0..$31",
    "MULTIPLY_DIVIDE_HIGH": "HI and LO",
    "PROGRAM_COUNTER": "PC and nPC",
    "VALUE_REGISTER_0": "$v0",
    "ARGUMENT_REGISTER_0": "$a0",
    "ARGUMENT_REGISTER_1": "$a1",
    "ARGUMENT_REGISTER_2": "$a2",
    "ARGUMENT_REGISTER_3": "$a3",
    "FLOATING_POINT_ARGUMENT_REGISTER_0": "$f12",
    "STACK_POINTER_REGISTER": "$sp",
    "RESULT_REGISTER": "$v0",
    "FLOATING_POINT_RESULT_REGISTER": "$f0",
    "GLOBAL_POINTER_REGISTER": "$gp",
    "#header coprocessor_0_bad_virtual_address": "CP0 BadVAddr",
    "#header coprocessor_0_count": "CP0 Count",
    "#header coprocessor_0_compare": "CP0 Compare",
    "#header coprocessor_0_status": "CP0 Status",
    "COPROCESSOR_0_STATUS_COPROCESSOR_USABLE": "Status CU",
    "COPROCESSOR_0_STATUS_INTERRUPT_MASK": "Status IM",
    **{f"COPROCESSOR_0_STATUS_INTERRUPT_MASK_{n}": f"IM{n}" for n in range(8)},
    "COPROCESSOR_0_STATUS_USER_MODE": "Status UM",
    "COPROCESSOR_0_STATUS_EXCEPTION_LEVEL": "Status EXL",
    "COPROCESSOR_0_STATUS_INTERRUPT_ENABLE": "Status IE",
    "#header coprocessor_0_cause": "CP0 Cause",
    "COPROCESSOR_0_CAUSE_BRANCH_DELAY": "Cause BD",
    "COPROCESSOR_0_CAUSE_INTERRUPTS_PENDING": "Cause IP",
    **{f"COPROCESSOR_0_CAUSE_INTERRUPT_PENDING_{n}": f"IP{n}" for n in range(8)},
    "COPROCESSOR_0_CAUSE_EXCEPTION_CODE": "Cause ExcCode",
    "coprocessor_0_exception_code": "Cause ExcCode",
    "#header coprocessor_0_exception_program_counter": "CP0 EPC",
    "#header coprocessor_0_config": "CP0 Config",
    "COPROCESSOR_0_CONFIG_BIG_ENDIAN": "Config BE",
    "COPROCESSOR_0_CONFIG_ARCHITECTURE_TYPE": "Config AT",
    "COPROCESSOR_0_CONFIG_ARCHITECTURE_REVISION": "Config AR",
    "COPROCESSOR_0_CONFIG_MEMORY_MANAGEMENT_UNIT_TYPE": "Config MT",
    "floating_point_control_registers": "the FCRs",
    "floating_point_implementation_register": "FIR",
    "FLOATING_POINT_IMPLEMENTATION_HAS_WORD": "FIR W",
    "FLOATING_POINT_IMPLEMENTATION_HAS_DOUBLE": "FIR D",
    "FLOATING_POINT_IMPLEMENTATION_HAS_SINGLE": "FIR S",
    "floating_point_control_status_register": "FCSR",
    "FLOATING_POINT_CONTROL_STATUS_CONDITION_CODES": "FCSR FCC",
    **{
        f"FLOATING_POINT_CONTROL_STATUS_{group}_{meaning}": f"FCSR {group.title()} {letter}"
        for group in ("CAUSE", "ENABLE", "FLAG")
        for letter, meaning in (
            ("E", "UNIMPLEMENTED"),
            ("V", "INVALID"),
            ("Z", "DIVIDE_BY_ZERO"),
            ("O", "OVERFLOW"),
            ("U", "UNDERFLOW"),
            ("I", "INEXACT"),
        )
        if letter != "E" or group == "CAUSE"  # only Cause has the unimplemented bit
    },
}
EXCEPTION_CODES: dict[str, str] = {
    "EXCEPTION_CODE_INTERRUPT": "Int",
    "EXCEPTION_CODE_TRANSLATION_LOOKASIDE_BUFFER_MODIFIED": "Mod",
    "EXCEPTION_CODE_TRANSLATION_LOOKASIDE_BUFFER_LOAD": "TLBL",
    "EXCEPTION_CODE_TRANSLATION_LOOKASIDE_BUFFER_STORE": "TLBS",
    "EXCEPTION_CODE_ADDRESS_ERROR_LOAD": "AdEL",
    "EXCEPTION_CODE_ADDRESS_ERROR_STORE": "AdES",
    "EXCEPTION_CODE_INSTRUCTION_BUS_ERROR": "IBE",
    "EXCEPTION_CODE_DATA_BUS_ERROR": "DBE",
    "EXCEPTION_CODE_SYSTEM_CALL": "Sys",
    "EXCEPTION_CODE_BREAKPOINT": "Bp",
    "EXCEPTION_CODE_RESERVED_INSTRUCTION": "RI",
    "EXCEPTION_CODE_COPROCESSOR_UNUSABLE": "CpU",
    "EXCEPTION_CODE_ARITHMETIC_OVERFLOW": "Ov",
    "EXCEPTION_CODE_TRAP": "Tr",
    "EXCEPTION_CODE_FLOATING_POINT_EXCEPTION": "FPE",
    "EXCEPTION_CODE_COPROCESSOR_2_EXCEPTION": "C2E",
    "EXCEPTION_CODE_CACHE_ERROR": "CacheErr",
    "EXCEPTION_CODE_MACHINE_CHECK": "MCheck",
    "EXCEPTION_CODE_WATCH": "WATCH",
}
FIELDS: dict[str, str] = {
    "SOURCE_REGISTER": "rs",
    "TARGET_REGISTER": "rt",
    "DESTINATION_REGISTER": "rd",
    "SHIFT_AMOUNT": "shamt",
    "IMMEDIATE": "imm",
    "FLOATING_POINT_SOURCE": "fs",
    "FLOATING_POINT_TARGET": "ft",
    "FLOATING_POINT_DESTINATION": "fd",
    "FLOATING_POINT_CONDITION_CODE": "cc",
    "FLOATING_POINT_NULLIFY_DELAY": "nd",
    "FLOATING_POINT_TRUE_FALSE": "tf",
}
ALLOCATORS: dict[str, str] = {
    "allocate_or_exit": "xmalloc in GNU style",
    "reallocate_or_exit": "xrealloc in GNU style",
    "allocate_zeroed_or_exit": "zmalloc: a zero-filled xmalloc",
}
ANNOTATIONS: dict[str, dict[str, str]] = {
    "include/registers.h": REGISTERS,
    "include/instruction.h": EXCEPTION_CODES | FIELDS,
    "include/spim-utils.h": ALLOCATORS,
}


# The section comment above each CP0 register, renamed to carry the manual's name.
CP0_HEADERS: tuple[tuple[str, str], ...] = (
    (
        "/* BadVAddr register: */",
        "/* CP0 BadVAddr, the bad virtual address register: */",
    ),
    ("/* Count register: */", "/* CP0 Count register: */"),
    ("/* Compare register: */", "/* CP0 Compare register: */"),
    ("/* Status register: */", "/* CP0 Status register: */"),
    ("/* Cause register: */", "/* CP0 Cause register: */"),
    ("/* EPC register: */", "/* CP0 EPC, the exception program counter: */"),
    ("/* Config register: */", "/* CP0 Config register: */"),
)


def annotate(lines: list[str], name: str, tag: str) -> None:
    """Tag the first definition of ``name`` in ``lines``, in place.

    A macro gets the tag as a comment line just above its ``#define``. Any other definition
    gets it in the comment already on its last line (``/* TAG: old text */``), or as a new
    trailing comment.

    Args:
        lines: the file's lines.
        name: the defined name.
        tag: the manual's or the old idiom's name.

    Raises:
        SystemExit: when no line defines the name.
    """
    definition: re.Pattern[str] = re.compile(
        rf"^\s*(constexpr\b.*\b{name}\s*=|#define {name}\b|extern\b.*\b{name}\b"
        rf"|\[\[nodiscard\]\] .*\b{name}\(|{name}\s*=)"
    )
    start: int = next(
        (index for index, line in enumerate(lines) if definition.match(line)), -1
    )
    if start < 0:
        sys.stderr.write(f"no definition of {name}\n")
        raise SystemExit(1)
    elif lines[start].startswith("#define"):
        lines.insert(start, f"/* {tag} */")
        return
    end: int = start
    # A declaration continues until its terminator: ";" or an enumerator's ",".
    while not re.search(r"[;,]\s*(/\*.*\*/)?\s*$", lines[end]):
        end += 1
    if "/*" in lines[end] and lines[end].rstrip().endswith("*/"):
        lines[end] = re.sub(r"/\*\s*", f"/* {tag}: ", lines[end], count=1)
    else:
        lines[end] = lines[end].rstrip() + f" /* {tag} */"


def main() -> int:
    """Annotate every listed definition.

    Returns:
        int: 0.
    """
    path_name: str
    names: dict[str, str]
    for path_name, names in ANNOTATIONS.items():
        path: pathlib.Path = pathlib.Path(path_name)
        lines: list[str] = path.read_text().split("\n")
        name: str
        tag: str
        for name, tag in names.items():
            if not name.startswith("#header "):
                annotate(lines, name, tag)
        text: str = "\n".join(lines)
        if path_name == "include/registers.h":
            old: str
            new: str
            for old, new in CP0_HEADERS:
                text = text.replace(old, new, 1)
        path.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
