#!/usr/bin/env python3
"""The Rust port's renames that a whole-port token pass cannot make.

Run after ``port_names.py apply rust``:

- The ``ExceptionCode`` variants share spellings with other enums' variants (``Int`` is also
  a ``ScanValue``), so they are renamed here, at the enum and at every ``ExceptionCode::``
  path, with the manual's mnemonic at the start of each variant's doc comment.
- ``console.rs``'s ``fd`` is a POSIX file descriptor, not the floating-point destination field.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-python-port/fix_rust_after_apply.py
"""

import pathlib
import re
import sys

PORT: pathlib.Path = pathlib.Path("port/rust/src")
VARIANTS: dict[str, tuple[str, str]] = {
    "Int": ("Interrupt", "Int"),
    "Mod": ("TranslationLookasideBufferModified", "Mod"),
    "Tlbl": ("TranslationLookasideBufferLoad", "TLBL"),
    "Tlbs": ("TranslationLookasideBufferStore", "TLBS"),
    "AdEL": ("AddressErrorLoad", "AdEL"),
    "AdES": ("AddressErrorStore", "AdES"),
    "Ibe": ("InstructionBusError", "IBE"),
    "Dbe": ("DataBusError", "DBE"),
    "Sys": ("SystemCall", "Sys"),
    "Bp": ("Breakpoint", "Bp"),
    "Ri": ("ReservedInstruction", "RI"),
    "CpU": ("CoprocessorUnusable", "CpU"),
    "Ov": ("ArithmeticOverflow", "Ov"),
    "Tr": ("Trap", "Tr"),
    "Fpe": ("FloatingPointException", "FPE"),
}


def main() -> int:
    """Make both fixes.

    Returns:
        int: 0.
    """
    path: pathlib.Path
    for path in sorted(PORT.glob("*.rs")):
        text: str = path.read_text()
        text = re.sub(
            r"\bExceptionCode::([A-Za-z]+)\b",
            lambda match: (
                "ExceptionCode::"
                + VARIANTS.get(match.group(1), (match.group(1), ""))[0]
            ),
            text,
        )
        if path.name == "run.rs":
            start: int = text.index("pub enum ExceptionCode {")
            end: int = text.index("\n}", start)
            body: str = text[start:end]
            old: str
            new: str
            manual: str
            for old, (new, manual) in VARIANTS.items():
                body, count = re.subn(
                    rf"(///[ ]*)([^\n]*)\n(\s+){old} = ",
                    rf"\g<1>{manual}: \g<2>\n\g<3>{new} = ",
                    body,
                    count=1,
                )
                assert count <= 1, old  # a code the port does not raise has no variant
            text = text[:start] + body + text[end:]
        if path.name == "console.rs":
            text = re.sub(r"\bfloating_point_destination\b", "fd", text)
        path.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
