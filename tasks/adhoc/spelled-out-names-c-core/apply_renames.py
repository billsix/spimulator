#!/usr/bin/env python3
"""Apply ``data/renames.tsv`` to the C core, by AST position where clang can see the name.

Two passes over ``src/`` and ``include/`` (the generated files of ``tools/gen_opcodes.py`` are
skipped; they are regenerated from the renamed ``.tsv`` files):

1. **By AST position**, for variables, parameters, fields, functions and enum constants. Each
   declaration's decision is made once, from its location (a ``path:line`` row of
   ``renames.tsv``) or else its name (a ``*`` row), and every ``DeclRefExpr`` and ``MemberExpr``
   follows its referenced declaration's decision by clang's declaration id. So two
   declarations spelled alike can get different new names, or one can keep its name.
2. **By token**, for what the JSON AST has no reference positions for: typedef names, struct,
   union and enum tags, macros, enum constants and file-scope variables used as an array size
   or array designator, and a renamed field written in a designator chain
   (``.old =``, ``.old.inner =``) or in ``offsetof``. String and character literals are never changed; comments change only for
   names that are clearly identifiers (they contain ``_``, or mix or are all upper case).

``--kinds`` limits both passes to some declaration kinds, so the work can be committed in
batches. Afterwards ``--residue`` lists every old name still spelled in code, which is either a
name the build does not see (an unused macro body, an ``#if``'d-out branch) or a mistake.

Usage, from the repository root::

    python3 tasks/adhoc/spelled-out-names-c-core/apply_renames.py BUILDDIR --kinds KIND,...
    python3 tasks/adhoc/spelled-out-names-c-core/apply_renames.py BUILDDIR --residue

``KIND`` is ``local`` (variables inside a function, and parameters), ``field``, ``function``,
``global`` (file-scope variables), ``enum`` (enum constants), ``type`` (typedefs and tags) or
``macro``.
"""

import argparse
import collections
import json
import pathlib
import re
import shlex
import subprocess
import sys
import typing

Json: typing.TypeAlias = typing.Any

DATA: pathlib.Path = pathlib.Path("tasks/adhoc/spelled-out-names-c-core/data")
GENERATED: frozenset[str] = frozenset(
    {
        "include/tokens.h",
        "include/opcode-types.h",
        "include/opcode-tables.h",
        "src/opcode-tables.c",
    }
)
SCOPES: tuple[str, ...] = ("src", "include")
AST_KINDS: dict[str, str] = {
    "VarDecl": "global",
    "ParmVarDecl": "local",
    "FieldDecl": "field",
    "FunctionDecl": "function",
    "EnumConstantDecl": "enum",
}
TOKEN_KINDS: dict[str, str] = {
    "TypedefDecl": "type",
    "RecordDecl": "type",
    "EnumDecl": "type",
    "MacroDefinition": "macro",
}
JOINED_FLAGS: tuple[str, ...] = (
    "-I",
    "-D",
    "-U",
    "-std=",
    "-isystem",
    "-iquote",
    "-include",
)
SPLIT_FLAGS: frozenset[str] = frozenset(
    {"-I", "-D", "-U", "-isystem", "-iquote", "-include"}
)
# A C token stream, coarse but literal-safe: comments, string and character literals, and
# identifiers; everything else passes through. It runs on bytes, because clang's offsets are
# byte offsets and the sources hold UTF-8 (em dashes in comments).
LEXEME: re.Pattern[bytes] = re.compile(
    rb"""(?P<comment>/\*.*?\*/|//[^\n]*)"""
    rb"""|(?P<string>"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')"""
    rb"""|(?P<identifier>[A-Za-z_][A-Za-z0-9_]*)""",
    re.DOTALL,
)
IDENTIFIER: re.Pattern[bytes] = re.compile(rb"[A-Za-z_][A-Za-z0-9_]*")


class Rename(typing.NamedTuple):
    """One rule of ``renames.tsv``."""

    old: str
    new: str
    kinds: frozenset[str]
    location: str


def read_renames() -> list[Rename]:
    """The rows of ``renames.tsv``.

    Returns:
        list[Rename]: every rule.
    """
    rows: list[Rename] = []
    line: str
    for line in (DATA / "renames.tsv").read_text().splitlines():
        old: str
        new: str
        kinds: str
        location: str
        old, new, kinds, _, location = line.split("\t")
        rows.append(Rename(old, new, frozenset(kinds.split(",")), location))
    return rows


def parse_flags(arguments: list[str]) -> list[str]:
    """Keep the flags of a compile command that change what clang parses.

    Args:
        arguments: the command's arguments, without the compiler itself.

    Returns:
        list[str]: the include, define and standard flags, in order.
    """
    kept: list[str] = []
    take_next: bool = False
    argument: str
    for argument in arguments:
        if take_next:
            kept.append(argument)
            take_next = False
        elif argument in SPLIT_FLAGS:
            kept.append(argument)
            take_next = True
        elif argument.startswith(JOINED_FLAGS):
            kept.append(argument)
        else:
            pass  # warnings, optimization, -c, -o and its value
    return kept


def ast_of(entry: dict[str, Json]) -> Json:
    """The JSON AST of one compile-database entry.

    Args:
        entry: a ``compile_commands.json`` entry.

    Returns:
        Json: clang's ``-ast-dump=json`` of that translation unit.
    """
    arguments: list[str] = (
        entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    )
    completed: subprocess.CompletedProcess[str] = subprocess.run(
        [
            "clang",
            *parse_flags(arguments[1:]),
            "-fsyntax-only",
            "-Xclang",
            "-ast-dump=json",
            entry["file"],
        ],
        cwd=entry["directory"],
        capture_output=True,
        text=True,
        check=True,
    )
    return json.loads(completed.stdout)


class Walker:
    """Walk one translation unit in dump order, resolving clang's delta-encoded locations.

    clang's JSON writes a location's ``file`` and ``line`` only when they differ from the
    previous location printed, so every location must be visited in order.
    """

    def __init__(self, directory: pathlib.Path, root: pathlib.Path) -> None:
        """Start a walk.

        Args:
            directory: the compile command's directory (locations are relative to it).
            root: the repository root.
        """
        self.directory: pathlib.Path = directory
        self.root: pathlib.Path = root
        self.file: str = ""
        self.line: int = 0
        # declaration id -> (repo path, line, name, kind class, offset of the name)
        self.declarations: dict[str, tuple[str, int, str, str, int]] = {}
        # (repo path, offset, name, referenced declaration id)
        self.references: list[tuple[str, int, str, str]] = []
        self.function_depth: int = 0

    def repo_path(self) -> str | None:
        """The current file relative to the repository root, if it is in the core.

        Returns:
            str | None: ``src/x.c``-style path, or None outside ``src/`` and ``include/``.
        """
        path: pathlib.Path = (self.directory / self.file).resolve()
        if (
            path.is_relative_to(self.root)
            and path.relative_to(self.root).parts[0] in SCOPES
        ):
            return str(path.relative_to(self.root))
        else:
            return None

    def location(self, place: Json) -> tuple[str | None, int, int]:
        """Visit one location and return where its token is spelled.

        Args:
            place: a location object (possibly with ``spellingLoc``/``expansionLoc``).

        Returns:
            tuple[str | None, int, int]: repo path (None outside the core), line and offset.
        """
        match place:
            case {"spellingLoc": dict(spelling), **rest}:
                spelled: tuple[str | None, int, int] = self.location(spelling)
                if "expansionLoc" in rest:
                    self.location(rest["expansionLoc"])
                return spelled
            case {"offset": int(offset), **rest}:
                if "file" in rest:
                    self.file = rest["file"]
                if "line" in rest:
                    self.line = rest["line"]
                return self.repo_path(), self.line, offset
            case _:
                return None, 0, -1

    def visit(self, node: Json) -> None:
        """Visit a node and its children in dump order.

        Args:
            node: any JSON value of the dump.
        """
        match node:
            case {"kind": str(kind), **fields}:
                kind_class: str | None = AST_KINDS.get(kind)
                if kind == "VarDecl" and self.function_depth > 0:
                    kind_class = "local"
                elif kind == "VarDecl":
                    kind_class = "global"
                else:
                    pass
                spelled: tuple[str | None, int, int] = (None, 0, -1)
                if "loc" in fields:
                    spelled = self.location(fields["loc"])
                begin: tuple[str | None, int, int] = (None, 0, -1)
                end: tuple[str | None, int, int] = (None, 0, -1)
                if "range" in fields:
                    begin = self.location(fields["range"].get("begin", {}))
                    end = self.location(fields["range"].get("end", {}))
                match kind:
                    case _ if kind_class is not None and "name" in fields:
                        if spelled[0] is not None and not fields.get("isImplicit"):
                            self.declarations[fields["id"]] = (
                                spelled[0],
                                spelled[1],
                                fields["name"],
                                kind_class,
                                spelled[2],
                            )
                    case "DeclRefExpr" if begin[0] is not None:
                        referenced: dict[str, Json] = fields["referencedDecl"]
                        self.references.append(
                            (begin[0], begin[2], referenced["name"], referenced["id"])
                        )
                    case "MemberExpr" if end[0] is not None and fields.get("name"):
                        self.references.append(
                            (
                                end[0],
                                end[2],
                                fields["name"],
                                fields["referencedMemberDecl"],
                            )
                        )
                    case _:
                        pass
                is_function: bool = kind == "FunctionDecl"
                self.function_depth += 1 if is_function else 0
                if "inner" in fields:
                    self.visit(fields["inner"])
                self.function_depth -= 1 if is_function else 0
            case dict():
                value: Json
                for value in node.values():
                    self.visit(value)
            case list():
                item: Json
                for item in node:
                    self.visit(item)
            case _:
                pass


def decide(
    declaration: tuple[str, int, str, str, int],
    located: dict[tuple[str, int, str], str],
    by_name: dict[str, str],
) -> str | None:
    """The new name of one declaration, or None to keep it.

    Args:
        declaration: repo path, line, name, kind class and name offset.
        located: (path, line, old) to new, from the ``path:line`` rows.
        by_name: old to new, from the ``*`` rows.

    Returns:
        str | None: the new name, or None.
    """
    path: str
    line: int
    name: str
    path, line, name, _, _ = declaration
    new: str | None = located.get((path, line, name), by_name.get(name))
    return None if new is None or new == name else new


def ast_edits(
    builddir: pathlib.Path, root: pathlib.Path, renames: list[Rename], kinds: set[str]
) -> dict[tuple[str, int], tuple[str, str]]:
    """The AST-positioned edits for the chosen kinds.

    Args:
        builddir: the build directory with ``compile_commands.json``.
        root: the repository root.
        renames: every rule.
        kinds: the kind classes to rename.

    Returns:
        dict[tuple[str, int], tuple[str, str]]: (path, offset) to (old, new).
    """
    by_name: dict[str, str] = {
        rule.old: rule.new for rule in renames if rule.location == "*"
    }
    located: dict[tuple[str, int, str], str] = {}
    rule: Rename
    for rule in renames:
        if rule.location != "*":
            path: str
            line: str
            path, line = rule.location.rsplit(":", 1)
            located[(path, int(line), rule.old)] = rule.new
    entries: list[dict[str, Json]] = json.loads(
        (builddir / "compile_commands.json").read_text()
    )
    edits: dict[tuple[str, int], tuple[str, str]] = {}
    conflicts: list[str] = []

    def add(path: str, offset: int, old: str, new: str) -> None:
        previous: tuple[str, str] | None = edits.get((path, offset))
        if previous is not None and previous != (old, new):
            conflicts.append(f"{path}@{offset}: {previous} vs {(old, new)}")
        edits[(path, offset)] = (old, new)

    entry: dict[str, Json]
    for entry in entries:
        walker: Walker = Walker(pathlib.Path(entry["directory"]), root)
        walker.visit(ast_of(entry))
        new_of: dict[str, str | None] = {
            identity: decide(declaration, located, by_name)
            for identity, declaration in walker.declarations.items()
            if declaration[3] in kinds
        }
        identity: str
        declaration: tuple[str, int, str, str, int]
        for identity, declaration in walker.declarations.items():
            new: str | None = new_of.get(identity)
            if new is not None:
                add(declaration[0], declaration[4], declaration[2], new)
        path: str
        offset: int
        name: str
        for path, offset, name, identity in walker.references:
            new = new_of.get(identity)
            if new is not None:
                add(path, offset, name, new)
    if conflicts:
        sys.stderr.write("conflicting edits:\n" + "\n".join(conflicts) + "\n")
        raise SystemExit(1)
    return edits


def token_edits(
    root: pathlib.Path, renames: list[Rename], kinds: set[str], field_designators: bool
) -> dict[tuple[str, int], tuple[str, str]]:
    """The token edits: types and macros, and renamed fields written as designators.

    Args:
        root: the repository root.
        renames: every rule.
        kinds: the kind classes to rename.
        field_designators: also rename ``.old =`` designators and ``offsetof`` members of
            renamed fields.

    Returns:
        dict[tuple[str, int], tuple[str, str]]: (path, offset) to (old, new).
    """
    located_olds: set[str] = {rule.old for rule in renames if rule.location != "*"}
    # Enum constants and file-scope variables also appear where the JSON AST has no reference
    # (an array size, an array designator), so the token pass renames them too. A name that is
    # also a parameter, or has a per-declaration row, is left to the AST pass.
    constant_like: bool = bool(kinds & {"enum", "global"})
    by_token: dict[str, str] = {
        rule.old: rule.new
        for rule in renames
        if rule.location == "*"
        and (
            any(TOKEN_KINDS.get(kind) in kinds for kind in rule.kinds)
            or (
                constant_like
                and rule.old not in located_olds
                and rule.kinds <= {"EnumConstantDecl", "VarDecl"}
            )
        )
    }
    fields: dict[str, str] = {
        rule.old: rule.new
        for rule in renames
        if rule.location == "*" and "FieldDecl" in rule.kinds and field_designators
    }
    # A field renamed only at its declaration(s) is still safe to rename in a designator when
    # every one of its rows gives the same new name and no row renames it everywhere.
    field_lines: set[str] = {
        f"{path}:{line}"
        for kind, _, path, line in (
            row.split("\t") for row in (DATA / "names.tsv").read_text().splitlines()
        )
        if kind == "FieldDecl"
    }
    located_fields: dict[str, set[str]] = collections.defaultdict(set)
    rule: Rename
    for rule in renames:
        if rule.location in field_lines and field_designators:
            located_fields[rule.old].add(rule.new)
    old: str
    news: set[str]
    for old, news in located_fields.items():
        if len(news) == 1 and old not in fields:
            fields[old] = next(iter(news))
        elif old in fields and news != {fields[old]}:
            del fields[old]  # ambiguous in a designator: left for the build to report
    edits: dict[tuple[str, int], tuple[str, str]] = {}
    path: pathlib.Path
    for path in core_files(root):
        relative: str = str(path.relative_to(root))
        text: bytes = path.read_bytes()
        match: re.Match[bytes]
        for match in LEXEME.finditer(text):
            if match.group("identifier") is not None:
                token: str = match.group("identifier").decode()
                before: str = (
                    text[max(0, match.start() - 200) : match.start()]
                    .decode(errors="replace")
                    .rstrip()
                )
                after: str = (
                    text[match.end() : match.end() + 4]
                    .decode(errors="replace")
                    .lstrip()
                )
                if token in by_token:
                    edits[(relative, match.start())] = (token, by_token[token])
                elif token in fields and (
                    (before.endswith(".") and after.startswith(("=", ".", "[")))
                    or re.search(r"offsetof\s*\([^,]*,\s*$", before) is not None
                ):
                    edits[(relative, match.start())] = (token, fields[token])
                else:
                    pass
            elif match.group("comment") is not None:
                comment: bytes = match.group("comment")
                word: re.Match[bytes]
                for word in IDENTIFIER.finditer(comment):
                    name: str = word.group(0).decode()
                    new: str | None = comment_rename(name, renames, kinds)
                    if new is not None:
                        edits[(relative, match.start() + word.start())] = (name, new)
            else:
                pass  # a string or character literal: never changed
    return edits


def comment_rename(name: str, renames: list[Rename], kinds: set[str]) -> str | None:
    """The new spelling of a name mentioned in a comment, if it is clearly an identifier.

    Args:
        name: a word in a comment.
        renames: every rule.
        kinds: the kind classes being renamed.

    Returns:
        str | None: the new name, or None to leave the word.
    """
    looks_like_code: bool = "_" in name or (name.upper() == name and len(name) >= 3)
    if not looks_like_code:
        return None
    rule: Rename
    for rule in renames:
        if (
            rule.old == name
            and rule.location == "*"
            and any(
                AST_KIND_CLASSES.get(kind, set()) & kinds
                or TOKEN_KINDS.get(kind) in kinds
                for kind in rule.kinds
            )
        ):
            return rule.new
    return None


AST_KIND_CLASSES: dict[str, set[str]] = {
    "VarDecl": {"local", "global"},
    "ParmVarDecl": {"local"},
    "FieldDecl": {"field"},
    "FunctionDecl": {"function"},
    "EnumConstantDecl": {"enum"},
}


def core_files(root: pathlib.Path) -> list[pathlib.Path]:
    """The hand-written C sources and headers of the core.

    Args:
        root: the repository root.

    Returns:
        list[pathlib.Path]: every ``src/*.c`` and ``include/*.h`` except the generated ones.
    """
    listed: list[str] = subprocess.run(
        ["git", "ls-files", "src/*.c", "include/*.h"],
        capture_output=True,
        text=True,
        check=True,
        cwd=root,
    ).stdout.split()
    return [root / name for name in listed if name not in GENERATED]


def apply(root: pathlib.Path, edits: dict[tuple[str, int], tuple[str, str]]) -> int:
    """Rewrite the files, checking each edit's old text first.

    Args:
        root: the repository root.
        edits: (path, offset) to (old, new).

    Returns:
        int: how many tokens changed.
    """
    by_path: dict[str, list[tuple[int, str, str]]] = collections.defaultdict(list)
    path: str
    offset: int
    old: str
    new: str
    for (path, offset), (old, new) in edits.items():
        if path not in GENERATED:
            by_path[path].append((offset, old, new))
    changed: int = 0
    for path, items in by_path.items():
        text: bytes = (root / path).read_bytes()
        pieces: list[bytes] = []
        cursor: int = 0
        for offset, old, new in sorted(items):
            if text[offset : offset + len(old)] != old.encode() or offset < cursor:
                sys.stderr.write(f"{path}@{offset}: expected {old!r}\n")
                raise SystemExit(1)
            pieces.append(text[cursor:offset])
            pieces.append(new.encode())
            cursor = offset + len(old)
            changed += 1
        pieces.append(text[cursor:])
        (root / path).write_bytes(b"".join(pieces))
    return changed


def residue(root: pathlib.Path, renames: list[Rename]) -> int:
    """Print every old name of a ``*`` rule still spelled as an identifier in code.

    Args:
        root: the repository root.
        renames: every rule.

    Returns:
        int: the number of sites printed.
    """
    olds: set[str] = {rule.old for rule in renames if rule.location == "*"}
    count: int = 0
    path: pathlib.Path
    for path in core_files(root):
        text: bytes = path.read_bytes()
        match: re.Match[bytes]
        for match in LEXEME.finditer(text):
            identifier: bytes | None = match.group("identifier")
            token: str | None = None if identifier is None else identifier.decode()
            if token in olds:
                line: int = text.count(b"\n", 0, match.start()) + 1
                print(f"{path.relative_to(root)}:{line}: {token}")
                count += 1
    return count


def main() -> int:
    """Apply one batch, or report the residue.

    Returns:
        int: 0, or 1 on a conflict, a mismatch, or residue found.
    """
    parser: argparse.ArgumentParser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("builddir", type=pathlib.Path)
    parser.add_argument("--kinds", default="")
    parser.add_argument("--residue", action="store_true")
    options: argparse.Namespace = parser.parse_args()
    root: pathlib.Path = pathlib.Path.cwd().resolve()
    renames: list[Rename] = read_renames()
    if options.residue:
        return 1 if residue(root, renames) else 0
    kinds: set[str] = set(options.kinds.split(","))
    edits: dict[tuple[str, int], tuple[str, str]] = {}
    if kinds & {"local", "global", "field", "function", "enum"}:
        edits.update(ast_edits(options.builddir, root, renames, kinds))
    edits.update(token_edits(root, renames, kinds, "field" in kinds))
    print(f"{apply(root, edits)} tokens renamed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
