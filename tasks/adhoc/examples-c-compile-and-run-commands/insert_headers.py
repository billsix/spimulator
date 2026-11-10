"""Insert a BUILD AND RUN comment block into every teaching .c listed in
data/inventory.tsv (written by discover.sh), just before its first #include.

Idempotent: an existing block is regenerated in place, so a second run
changes nothing.  Only the comment is added; no code changes.  A demo whose
paired assembly comes in numbered variants (NAME-1.asm, NAME-2.asm) gets one
spimulator line per variant.  Run from anywhere.
"""

import csv
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[3]
SRC = ROOT / "examples" / "src"
INVENTORY = pathlib.Path(__file__).resolve().parent / "data" / "inventory.tsv"
MARK = "BUILD AND RUN"

# demo basename -> (arguments, stdin).  stdin is "" (none), "<file" or
# "|cmd" (a producer piped in), or "head" (pipe the output to head -3).
SPEC: dict[str, tuple[str, str]] = {
    "binary-search": ("5", ""),
    "bubble-sort": ("", "<bubble-sort.input"),
    "fizzbuzz": ("15", ""),
    "pascals-triangle": ("5", ""),
    "rpn-1-stack": ("", "<rpn-1-stack.input"),
    "rpn-2-ops": ("", "<rpn-2-ops.input"),
    "rpn-3-float": ("", "<rpn-3-float.input"),
    "rpn": ("", "<rpn.input"),
    "rpn-eval": ("", "<rpn.input"),
    "sieve": ("30", ""),
    "echo": ("hello world", ""),
    "factor": ("360", ""),
    "factorial": ("10", ""),
    "gcd": ("48 18", ""),
    "seq": ("1 5", ""),
    "tee": ("tee-copy.txt", "<tee.input"),
    "commaAndPeriodCounter": ("", "<commaAndPeriodCounter.input"),
    "cat": ("cat.input", ""),
    "cksum-1-stdin": ("", "<cksum-1-stdin.input"),
    "cksum-core": ("", "<cksum.input"),
    "cksum": ("cksum.input", ""),
    "comm": ("comm.c comm.c", ""),
    "cp": ("cp.c cp-copy.c", ""),
    "touch": ("touched.txt", ""),
    "yes": ("", "head"),
    "calc-1-numbers": ("", "<calc-1-numbers.input"),
    "calc-2-addsub": ("", "<calc-2-addsub.input"),
    "calc-3-muldiv": ("", "<calc-3-muldiv.input"),
    "calc-4-parens": ("", "<calc-4-parens.input"),
    "calc-5-neg": ("", "<calc-5-neg.input"),
    "calc-6-float": ("", "<calc-6-float.input"),
    "calc-sdt": ("", "<calc-sdt.input"),
    "calc-tree": ("", "<calc-sdt.input"),
    "calc-eval": ("", "<calc-sdt.input"),
    "fibonacci": ("10", ""),
    "hanoi": ("3", ""),
    "queens": ("6", ""),
    "get-char-from-user": ("", "|printf 'abc\\n'"),
    "base64": ("base64.input", ""),
    "cut": ("-c 2-4 cut.input", ""),
    "expand": ("", "|printf 'a\\tb\\n'"),
    "head-1-count": ("", "<head-1-count.input"),
    "head": ("-n 3 head.input", ""),
    "nl": ("", "|printf 'one\\ntwo\\n'"),
    "od": ("", "|printf 'hi\\n'"),
    "rev": ("rev.input", ""),
    "rot13": ("", "<rot13.input"),
    "strings": ("", "<strings.input"),
    "tac": ("tac.input", ""),
    "tail": ("-n 3 tail.input", ""),
    "tr": ("", "<tr.input"),
    "uniq": ("uniq.input", ""),
    "wc": ("wc.input", ""),
}

# Notes printed under the spimulator lines for versions whose run is not
# meant to match the native one (tools/check_example_commands.py exempts
# the same files).
NOTES: dict[str, list[str]] = {
    "get-char-from-user": [
        "get-char-from-user-1.asm is the deliberately broken version: its",
        "misaligned stack slot raises address-error exceptions.  -2.asm",
        "is the fixed one; it prints the value before the character.",
    ],
    "subrountines": [
        "subrountines-1.asm never sets $v0 before main returns, so its exit",
        "status is whatever was left there; the printed output matches.",
    ],
    "testStringsForEquality": [
        "testStringsForEquality-1.asm prints only the comparison results,",
        "without the C version's labels.",
    ],
}

# Library demos load the library .asm files first (as examples/tests/run-demo.sh does).
LIB_ASM = {
    "ctype-demo": ["../libctype/libctype.asm"],
    "str-demo": ["../libstr/libstr.asm"],
}
for name in ("abs-demo", "atexit-demo", "atoi-demo", "bsearch-demo", "exit-demo"):
    LIB_ASM[name] = ["../libctype/libctype.asm", "../libstdlib/libstdlib.asm"]


def helper_path(helper: str) -> str:
    """A helper source as seen from a demo directory (all demos are two deep)."""
    return "../" + helper[len("lib/"):] if helper.startswith("lib/") else "../../" + helper


def wrap(cmd: str, args: str, stdin: str) -> str:
    line = f"{cmd} {args}".rstrip()
    match stdin[:1]:
        case "":
            return line
        case "<":
            return f"{line} < {stdin[1:]}"
        case "|":
            return f"{stdin[1:]} | {line}"
        case _ if stdin == "head":
            return f"{line} | head -3"
        case _:
            raise ValueError(stdin)


def asm_variants(demo_dir: pathlib.Path, name: str) -> list[str]:
    if (demo_dir / f"{name}.asm").exists():
        return [f"{name}.asm"]
    variants = sorted(p.name for p in demo_dir.glob(f"{name}-*.asm"))
    if not variants:
        raise FileNotFoundError(f"no paired .asm for {name} in {demo_dir}")
    return variants


def block(demo: str, helpers: list[str]) -> str:
    path = pathlib.PurePosixPath(demo)
    name = path.stem
    includes = ["-I../.."] + sorted(
        {"-I" + helper_path(h).rsplit("/", 1)[0] for h in helpers if h.startswith("lib/")}
    )
    sources = " ".join([path.name] + [helper_path(h) for h in helpers])
    args, stdin = SPEC.get(name, ("", ""))
    sims = [
        " *     $ " + wrap("spimulator " + " ".join(f"-f {a}" for a in LIB_ASM.get(name, []) + [v]), args, stdin)
        for v in asm_variants(SRC / path.parent, name)
    ]
    label = "the paired MIPS assembly" if len(sims) == 1 else "each paired MIPS assembly version"
    return "\n".join([
        "/* clang-format off */",
        f"/* {MARK}, from this file's directory:",
        " *",
        " *   native Linux build, then run:",
        f" *     $ gcc -std=gnu23 -nostdlib {' '.join(includes)} -o {name} {sources}",
        f" *     $ {wrap('./' + name, args, stdin)}",
        " *",
        f" *   {label}, in spimulator:",
        *sims,
        *([" *"] + [" *   " + n for n in NOTES[name]] if name in NOTES else []),
        " */",
        "/* clang-format on */",
        "",
    ])


def main() -> None:
    with INVENTORY.open() as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    changed = 0
    for row in rows:
        file = SRC / row["demo"]
        text = file.read_text()
        new_block = block(row["demo"], row["helpers"].split()) + "\n"
        opener = "/* clang-format off */\n/* " + MARK
        if opener in text:
            start = text.index(opener)
            end = text.index("/* clang-format on */\n\n", start) + len("/* clang-format on */\n\n")
            new_text = text[:start] + new_block + text[end:]
        else:
            lines = text.splitlines(keepends=True)
            first_include = next(i for i, l in enumerate(lines) if l.startswith("#include"))
            new_text = "".join(lines[:first_include]) + new_block + "".join(lines[first_include:])
        if new_text != text:
            file.write_text(new_text)
            changed += 1
    print(f"updated {changed} of {len(rows)} files")


main()
