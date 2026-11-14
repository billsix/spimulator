#!/usr/bin/env python3
"""Discover and rename the abbreviated names of one engine port, from the C core's renames.

Shared by the three port tasks (``tasks/archive/2026/11/14/spelled-out-names-python-port.md``,
``tasks/spelled-out-names-haskell-port.md``, ``tasks/spelled-out-names-rust-port.md``). The C core's rename
(``tasks/archive/2026/11/14/spelled-out-names-c-core.md``) left two inputs, in
``tasks/adhoc/spelled-out-names-c-core/data/``: ``renames.tsv`` (every C name and its new
spelling) and ``glossary.tsv`` (every abbreviated word and its spelling).

``discover PORT`` writes ``data/<port>.names.tsv``: each identifier the port declares, its
proposed new name and where the proposal came from:

- ``mirror``: the identifier spells a C name in the port's casing (``gpr``, ``CP0_STATUS``,
  ``RdRsRtInst``); it takes the C name's new words, in the identifier's own casing.
- ``glossary``: each abbreviated word of the glossary is spelled out.
- ``manual``: a row of ``data/<port>.manual.tsv`` (``old  new``) decides it.
- ``ambiguous``: a word with more than one meaning; it needs a manual row.

``apply PORT`` renames every ``mirror``, ``glossary`` and ``manual`` row, as whole identifier
tokens of the port's language, in code and in comments, never inside a string literal
(Python docstrings count as comments, since their doctests are code). The port's own build,
type checker and parity suites are the check.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-python-port/port_names.py discover python
    python3 tasks/adhoc/spelled-out-names-python-port/port_names.py apply python
"""

import ast
import collections
import io
import keyword
import pathlib
import re
import subprocess
import sys
import tokenize

HERE: pathlib.Path = pathlib.Path("tasks/adhoc/spelled-out-names-python-port")
C_DATA: pathlib.Path = pathlib.Path("tasks/adhoc/spelled-out-names-c-core/data")
PORTS: dict[str, tuple[str, tuple[str, ...]]] = {
    "python": ("port/python", (".py",)),
    "haskell": ("port/haskell", (".hs",)),
    "rust": ("port/rust", (".rs",)),
}
GENERATED: frozenset[str] = frozenset(
    {
        "port/python/spimulator_py/opcode_table.py",
        "port/haskell/src/Opcodes.hs",
        "port/haskell/src/OpcodeTypes.hs",
        "port/rust/src/opcodes_table.rs",
        "port/rust/src/opcode_types.rs",
    }
)
WORD: re.Pattern[str] = re.compile(r"[A-Z]+(?![a-z])|[A-Z]?[a-z]+|[0-9]+")
# The MIPS register fields: renamed even as two-letter names (the C core renamed them), where
# every other name of one or two letters is a tight-scope local or pattern variable and stays.
ARCHITECTURE_FIELDS: frozenset[str] = frozenset(
    {"rs", "rt", "rd", "fs", "ft", "fd", "fr", "cc", "pc", "ea"}
)
# Words whose glossary spelling depends on the name they are in; the C rename decided them per
# name, and a port name with one of them needs a manual row unless it mirrors a C name.
AMBIGUOUS: frozenset[str] = frozenset(
    {
        "op",
        "ops",
        "fp",
        "fd",
        "int",
        "asm",
        "cmp",
        "mod",
        "id",
        "in",
        "at",
        "sh",
        "hi",
        "lo",
    }
    | {
        "cb",
        "loc",
        "gp",
        "sp",
        "res",
        "cp",
        "pc",
        "k",
        "b",
        "h",
        "u",
        "n",
        "s",
        "r",
        "i",
    }
)


# Names the language or its standard library defines; a port that rebinds one still means it.
EXTERNAL: dict[str, frozenset[str]] = {
    "python": frozenset(dir(__builtins__)) | frozenset(keyword.kwlist),
    "haskell": frozenset(),
    "rust": frozenset(),
}


def words_of(name: str) -> list[str]:
    """The lower-cased words of an identifier, split on ``_``, case changes and digits.

    Args:
        name: an identifier.

    Returns:
        list[str]: its words.
    """
    return [word.lower() for word in WORD.findall(name)]


# Acronyms more familiar than their expansion (decision 3 of the C task) stay upper case
# inside a CamelCase or camelCase name: ``instructionEncodeIO``, ``TokenEOF``.
FAMILIAR: frozenset[str] = frozenset(
    {"io", "eof", "json", "ast", "ascii", "asan", "mips", "spim", "repl", "mdmx"}
)


def recase(words: list[str], like: str, port: str = "python") -> str:
    """Join words in the casing of an existing identifier.

    Args:
        words: lower-case words.
        like: the identifier whose style to copy (``snake``, ``SCREAMING``, ``CamelCase``,
            ``camelCase``); its leading underscores and trailing primes are kept.
        port: the language; a one-word lower-case Haskell name is camelCase, not snake.

    Returns:
        str: the new identifier.
    """

    def capital(word: str) -> str:
        return word.upper() if word in FAMILIAR else word.capitalize()

    leading: str = like[: len(like) - len(like.lstrip("_"))]
    trailing: str = like[len(like.rstrip("'")) :]
    body: str = like.strip("_").rstrip("'")
    joined: str
    if body.upper() == body and body.lower() != body:
        joined = "_".join(words).upper()
    elif "_" in body or (body.lower() == body and port != "haskell"):
        joined = "_".join(words)
    elif body[0].isupper():
        joined = "".join(capital(word) for word in words)
    else:
        joined = words[0] + "".join(capital(word) for word in words[1:])
    return leading + joined + trailing


def c_mirrors() -> dict[tuple[str, ...], list[str]]:
    """C name words to new C name words, for the renames of every declaration.

    Returns:
        dict[tuple[str, ...], list[str]]: old words to new words.
    """
    table: dict[tuple[str, ...], list[str]] = {}
    line: str
    for line in (C_DATA / "renames.tsv").read_text().splitlines():
        old: str
        new: str
        old, new, _, _, location = line.split("\t")
        if location == "*":
            table[tuple(words_of(old))] = words_of(new)
    return table


def glossary() -> dict[str, list[str]]:
    """Abbreviated word to its spelled-out words, from the C core's glossary.

    Returns:
        dict[str, list[str]]: the words that are spelled out, and how.
    """
    table: dict[str, list[str]] = {}
    line: str
    for line in (C_DATA / "glossary.tsv").read_text().splitlines()[1:]:
        word: str
        spelling: str
        category: str
        word, _, spelling, category, _ = line.split("\t")
        if (
            category in ("spell-out", "architecture")
            and re.fullmatch(r"[a-z0-9_]+", spelling)
            and word not in AMBIGUOUS
        ):
            table[word] = spelling.split("_")
    return table


def port_files(port: str) -> list[pathlib.Path]:
    """The port's hand-written sources.

    Args:
        port: ``python``, ``haskell`` or ``rust``.

    Returns:
        list[pathlib.Path]: tracked files of the port's language, without generated ones.
    """
    directory: str
    suffixes: tuple[str, ...]
    directory, suffixes = PORTS[port]
    listed: list[str] = subprocess.run(
        ["git", "ls-files", directory], capture_output=True, text=True, check=True
    ).stdout.split()
    return [
        pathlib.Path(name)
        for name in listed
        if name.endswith(suffixes)
        and name not in GENERATED
        and not name.startswith("port/haskell/tools/")
    ]


def python_declared(paths: list[pathlib.Path]) -> set[str]:
    """Every name a Python source declares: bindings, defs, classes, parameters, attributes.

    Args:
        paths: the sources.

    Returns:
        set[str]: the declared names.
    """
    declared: set[str] = set()
    path: pathlib.Path
    for path in paths:
        node: ast.AST
        for node in ast.walk(ast.parse(path.read_text())):
            match node:
                case ast.FunctionDef(name=name) | ast.ClassDef(name=name):
                    declared.add(name)
                case ast.arg(arg=name):
                    declared.add(name)
                case ast.Name(id=name, ctx=ast.Store()):
                    declared.add(name)
                case ast.Attribute(attr=name, ctx=ast.Store()):
                    declared.add(name)
                case ast.AnnAssign(target=ast.Name(id=name)):
                    declared.add(name)
                case _:
                    pass
    return declared


HASKELL_KEYWORDS: frozenset[str] = frozenset(
    {
        "case",
        "class",
        "data",
        "default",
        "deriving",
        "do",
        "else",
        "foreign",
        "if",
        "import",
    }
    | {
        "in",
        "infix",
        "infixl",
        "infixr",
        "instance",
        "let",
        "module",
        "newtype",
        "of",
        "then",
    }
    | {"type", "where", "forall", "qualified", "as", "hiding", "family", "pattern"}
)


def haskell_declared(paths: list[pathlib.Path]) -> set[str]:
    """Every identifier of the Haskell sources that is not a library's, a keyword or a module.

    The libraries' names come from ``haskell_external.sh`` (``data/haskell.external.txt``,
    ghci's ``:browse`` of each imported module). A module name is a file name, so it stays.

    Args:
        paths: the sources.

    Returns:
        set[str]: the port's own identifiers.
    """
    external: set[str] = set(
        (HERE / "data" / "haskell.external.txt").read_text().split()
    )
    modules: set[str] = {path.stem for path in paths}
    tokens: set[str] = {
        token
        for path in paths
        for token in identifiers("haskell", path.read_text(), code=True)
    }
    return tokens - external - HASKELL_KEYWORDS - modules


def regex_declared(port: str, paths: list[pathlib.Path]) -> set[str]:
    """Names a Haskell or Rust source declares, found by declaration patterns.

    Args:
        port: ``haskell`` or ``rust``.
        paths: the sources.

    Returns:
        set[str]: the declared names. A miss only means a name is not renamed; a wrong hit
        is caught by the compiler.
    """
    patterns: dict[str, list[str]] = {
        "rust": [
            r"\bfn\s+([A-Za-z_]\w*)",
            r"\blet\s+(?:mut\s+)?([A-Za-z_]\w*)",
            r"\b(?:struct|enum|type|trait|const|static|mod)\s+(?:mut\s+)?([A-Za-z_]\w*)",
            r"[(,]\s*(?:mut\s+)?([a-z_]\w*)\s*:\s*[&A-Za-z(\[]",
            r"^\s*(?:pub(?:\([a-z]+\))?\s+)?([a-z_]\w*)\s*:\s*[&A-Za-z(\[]",
            r"^\s*([A-Z]\w*)\s*(?:[,({=]|$)",
            r"\|\s*([a-z_]\w*)\s*[|,:]",
            r"\bfor\s+([a-z_]\w*)\s+in\b",
            r"\b(?:Some|Ok|Err)\(([a-z_]\w*)\)",
        ],
        "haskell": [
            r"^([a-z_][\w']*)\s*::",
            r"^\s+([a-z_][\w']*)\s*::",
            r"^\s*(?:data|newtype|type|class)\s+([A-Z][\w']*)",
            r"[|=]\s*([A-Z][\w']*)",
            r"[{,]\s*([a-z_][\w']*)\s*::",
            r"^\s*(?:let\s+)?([a-z_][\w']*)\s*(?:[a-z_][\w']*\s*)*=[^=>]",
            r"\\([a-z_][\w']*)",
            r"([a-z_][\w']*)\s*<-",
        ],
    }
    declared: set[str] = set()
    path: pathlib.Path
    for path in paths:
        text: str = path.read_text()
        pattern: str
        for pattern in patterns[port]:
            declared.update(re.findall(pattern, text, re.MULTILINE))
    return declared


def discover(port: str) -> int:
    """Write ``data/<port>.names.tsv``.

    Args:
        port: ``python``, ``haskell`` or ``rust``.

    Returns:
        int: 0.
    """
    paths: list[pathlib.Path] = port_files(port)
    match port:
        case "python":
            declared: set[str] = python_declared(paths)
        case "haskell":
            declared = haskell_declared(paths)
        case _:
            declared = regex_declared(port, paths)
    mirrors: dict[tuple[str, ...], list[str]] = c_mirrors()
    spelled: dict[str, list[str]] = glossary()
    manual_path: pathlib.Path = HERE / "data" / f"{port}.manual.tsv"
    manual: dict[str, str] = (
        dict(
            line.split("\t")[:2]
            for line in manual_path.read_text().splitlines()
            if line.strip() and not line.startswith("#")
        )
        if manual_path.exists()
        else {}
    )
    generated_names: set[str] = {
        token
        for path_name in GENERATED
        if path_name.startswith(PORTS[port][0])
        for token in identifiers(port, pathlib.Path(path_name).read_text(), code=True)
    }
    uses: collections.Counter[str] = collections.Counter(
        token for path in paths for token in identifiers(port, path.read_text())
    )
    rows: list[str] = []
    name: str
    for name in sorted(declared):
        words: list[str] = words_of(name)
        if (
            not words
            or name in EXTERNAL[port]
            or name in generated_names
            or (name.startswith("__") and name.endswith("__"))
        ):
            continue  # the language's or its library's name, not the port's
        if (
            name not in manual
            and len(name.strip("_'")) <= 2
            and name.lower().strip("_'") not in ARCHITECTURE_FIELDS
        ):
            continue  # a tight-scope local or pattern variable
        new: str
        source: str
        if name in manual:
            new, source = manual[name], "manual"
        elif tuple(words) in mirrors and mirrors[tuple(words)] != words:
            new, source = recase(mirrors[tuple(words)], name, port), "mirror"
        elif any(word in AMBIGUOUS for word in words) and len(name) > 1:
            if any(word in spelled for word in words) or len(words) > 1:
                new, source = name, "ambiguous"
            else:
                continue
        else:
            spelled_words: list[str] = [
                part for word in words for part in spelled.get(word, [word])
            ]
            if spelled_words == words:
                continue
            new, source = recase(spelled_words, name, port), "glossary"
        if new != name or source == "ambiguous":
            rows.append(f"{name}\t{new}\t{source}\t{uses[name]}\n")
    (HERE / "data").mkdir(parents=True, exist_ok=True)
    (HERE / "data" / f"{port}.names.tsv").write_text("".join(rows))
    counts: collections.Counter[str] = collections.Counter(
        row.split("\t")[2] for row in rows
    )
    print(f"{port}: {len(rows)} names {dict(counts)}")
    return 0


def identifiers(port: str, text: str, code: bool = False) -> list[str]:
    """Every identifier token of a source, outside string literals.

    Args:
        port: the language.
        text: the source.
        code: leave out the words of comments.

    Returns:
        list[str]: the identifiers, in order.
    """
    return [
        text[start:end]
        for start, end, in_comment in spans(port, text)
        if not (code and in_comment)
    ]


def spans(port: str, text: str) -> list[tuple[int, int, bool]]:
    """The identifier tokens of a source, as (start, end, in_comment) character offsets.

    String literals are skipped; comments (and Python docstrings) are included and marked.

    Args:
        port: the language.
        text: the source.

    Returns:
        list[tuple[int, int, bool]]: one entry per identifier.
    """
    if port == "python":
        return python_spans(text)
    elif port == "rust":
        lexeme: re.Pattern[str] = re.compile(
            r"(?P<comment>//[^\n]*|/\*.*?\*/)"
            r'|(?P<string>r#*"(?:.|\n)*?"#*|b?"(?:\\.|[^"\\])*"'
            r"|b?'(?:\\.|[^'\\])'(?!\w))"
            r"|(?P<lifetime>'[A-Za-z_]\w*)"
            r"|(?P<identifier>[A-Za-z_]\w*)",
            re.DOTALL,
        )
    else:
        lexeme = re.compile(
            r"(?P<comment>--[^\n]*|\{-.*?-\})"
            r'|(?P<string>"(?:\\.|[^"\\])*"|(?<![\w\'])\'(?:\\[^\']+|[^\'\\])\')'
            r"|(?P<identifier>[A-Za-z_][\w']*)",
            re.DOTALL,
        )
    found: list[tuple[int, int, bool]] = []
    match: re.Match[str]
    for match in lexeme.finditer(text):
        if match.group("identifier") is not None:
            found.append((match.start(), match.end(), False))
        elif match.group("comment") is not None:
            word: re.Match[str]
            for word in re.finditer(r"[A-Za-z_][\w']*", match.group("comment")):
                found.append(
                    (match.start() + word.start(), match.start() + word.end(), True)
                )
        else:
            pass  # a string, character or lifetime: never renamed
    return found


def python_spans(text: str) -> list[tuple[int, int, bool]]:
    """The identifier tokens of a Python source, from ``tokenize``.

    f-string expressions are code; docstrings and comments are included as comments; other
    string literals are skipped.

    Args:
        text: the source.

    Returns:
        list[tuple[int, int, bool]]: (start, end, in_comment) character offsets.
    """
    line_starts: list[int] = [0]
    line_starts.extend(m.end() for m in re.finditer("\n", text))
    docstrings: set[int] = set()
    node: ast.AST
    for node in ast.walk(ast.parse(text)):
        if isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef)) and node.body:
            first: ast.stmt = node.body[0]
            if (
                isinstance(first, ast.Expr)
                and isinstance(first.value, ast.Constant)
                and isinstance(first.value.value, str)
            ):
                docstrings.add(line_starts[first.lineno - 1] + first.col_offset)
    found: list[tuple[int, int, bool]] = []
    token: tokenize.TokenInfo
    for token in tokenize.generate_tokens(io.StringIO(text).readline):
        start: int = line_starts[token.start[0] - 1] + token.start[1]
        if token.type == tokenize.NAME:
            found.append((start, start + len(token.string), False))
        elif token.type == tokenize.COMMENT or (
            token.type == tokenize.STRING and start in docstrings
        ):
            word: re.Match[str]
            for word in re.finditer(r"[A-Za-z_]\w*", token.string):
                found.append((start + word.start(), start + word.end(), True))
        else:
            pass  # a string literal (or f-string text): never renamed
    return found


def comment_safe(name: str) -> bool:
    """Whether a word in a comment is clearly the identifier, so it may be renamed there.

    Args:
        name: the old name.

    Returns:
        bool: True for names with ``_``, or mixed or upper case of three letters or more.
    """
    return "_" in name or (len(name) >= 3 and name.lower() != name)


def apply(port: str) -> int:
    """Rename the port's names from ``data/<port>.names.tsv``.

    Args:
        port: ``python``, ``haskell`` or ``rust``.

    Returns:
        int: 0, or 1 when a name is still ambiguous.
    """
    table: dict[str, str] = {}
    ambiguous: list[str] = []
    line: str
    for line in (HERE / "data" / f"{port}.names.tsv").read_text().splitlines():
        old: str
        new: str
        source: str
        old, new, source, _ = line.split("\t")
        if source == "ambiguous":
            ambiguous.append(old)
        elif new != old:
            table[old] = new
    if ambiguous:
        sys.stderr.write(f"{len(ambiguous)} ambiguous names need a manual row\n")
        return 1
    changed: int = 0
    path: pathlib.Path
    for path in port_files(port):
        text: str = path.read_text()
        pieces: list[str] = []
        cursor: int = 0
        start: int
        end: int
        in_comment: bool
        for start, end, in_comment in spans(port, text):
            name: str = text[start:end]
            if name in table and (not in_comment or comment_safe(name)):
                pieces.append(text[cursor:start])
                pieces.append(table[name])
                cursor = end
                changed += 1
        pieces.append(text[cursor:])
        path.write_text("".join(pieces))
    print(f"{port}: {changed} tokens renamed")
    return 0


if __name__ == "__main__":
    match sys.argv[1:]:
        case ["discover", port] if port in PORTS:
            sys.exit(discover(port))
        case ["apply", port] if port in PORTS:
            sys.exit(apply(port))
        case _:
            sys.exit(f"usage: {sys.argv[0]} discover|apply python|haskell|rust")
