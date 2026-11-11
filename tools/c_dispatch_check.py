#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# SHARED, VENDORED TOOL -- keep every copy byte-identical.
# Canonical: runClaudeInContainer tasks/reference/c_dispatch_check.py
#   (github.com/billsix/runClaudeInContainer). Edit the canonical, then re-copy
#   it over every vendored copy (runCrushInContainer mirrors it).
# Vendored into a C project's tools/ so the project's own CI is self-contained
#   (a public repo can't rely on a mounted ~/.claude).
# Checks the "Dispatch" rules of c-coding-standard.md.
# ---------------------------------------------------------------------------
"""List the C sites that the C coding standard's dispatch rules flag.

The rules (c-coding-standard.md, "Dispatch"): a decision on one value with 3+ outcomes, or with
tests that list several constants, is a ``switch``; every ``switch`` has a ``default``; a run of
early-exit ``if``s is one ``if`` / ``else if`` / ``else`` chain; every chain ends in an ``else``.
The tool reads clang's JSON AST of each translation unit, so a test written through a macro is
seen as the code it expands to. It reports four kinds of site:

- ``switch-chain``: an ``if`` / ``else if`` chain comparing one value with integer, character or
  enumerator constants, with 3+ tests or a test listing several constants with ``||``.
- ``no-else``: an ``if`` / ``else if`` chain of 2+ tests with no final ``else``.
- ``guard-run``: two or more consecutive ``if``s with no ``else`` whose bodies always leave
  (``return``, ``break``, ``continue``, ``goto``, or a call to a function that never returns).
- ``no-default``: a ``switch`` with no ``default``.

**Accepting a site.** Put ``dispatch-check: ok`` in a comment, with the reason, on the line where
the flagged ``if`` or ``switch`` starts or on the line just above it::

    /* dispatch-check: ok -- every branch leaves; the code after is the loop body */

**What it can't see.** Code that the build's ``#if``/``#ifdef`` leaves out. Only the ``.c`` files
of the compile database are parsed, and only sites in those files are reported (an ``inline``
function in a header is not). No-return functions are recognized by their ``[[noreturn]]`` /
``_Noreturn`` / ``__attribute__((noreturn))`` attribute, plus ``exit``, ``abort``, ``_Exit``,
``longjmp`` and ``siglongjmp``; name others with ``--noreturn``.

Usage, from the project root::

    python3 tools/c_dispatch_check.py BUILDDIR [SOURCE ...] [--noreturn NAME ...] [--out OUTDIR]

- BUILDDIR holds ``compile_commands.json`` (meson writes one; for CMake set
  ``CMAKE_EXPORT_COMPILE_COMMANDS=ON``; for make use ``bear -- make``).
- SOURCE: directories or ``.c`` files, relative to the working directory; only translation units
  under them are checked. The default is every ``.c`` file under the working directory.
- ``--out OUTDIR`` also writes the full worklogs: ``if_chains.tsv`` (every chain of 2+ tests),
  ``guard_runs.tsv``, ``switches.tsv`` (with each subject's type) and ``summary.txt``.

The flagged sites go to stdout, one per line (``path:line``, kind, function, detail;
tab-separated), and the totals to stderr. The exit status is 0 when nothing is flagged, 1 when
something is, and 2 when clang could not parse a file (reported on stderr; the others are still
checked) or when no translation unit matches the sources.

Written for github.com/billsix/spimulator ``tasks/archive/2026/11/11/c-switch-and-else-if-chains.md``.
"""

import argparse
import json
import pathlib
import shlex
import subprocess
import sys
from collections.abc import Iterator
from dataclasses import dataclass
from typing import Any

# Paths are reported relative to the working directory, the project root.
ROOT: pathlib.Path = pathlib.Path.cwd()

# The comment that accepts a flagged site.
ACCEPT_MARKER: str = "dispatch-check: ok"

# Functions that never return, beyond the ones the TU itself marks [[noreturn]].
KNOWN_NORETURN: frozenset[str] = frozenset(
    {"exit", "abort", "_Exit", "longjmp", "siglongjmp"}
)

type Node = dict[str, Any]


@dataclass(frozen=True, slots=True, kw_only=True)
class Test:
    """What one `if` condition compares.

    Attributes:
        kind: "int" (subject == integer constant), "string" (string equality with a literal),
            or "other".
        subject: The canonical text of the compared expression, or "" for "other".
        constants: The constants the subject is compared with (several for an `||` of
            equalities).
    """

    kind: str
    subject: str
    constants: tuple[str, ...]


class LocationTracker:
    """Follows clang's JSON locations, which omit a file or line equal to the previous one."""

    def __init__(self) -> None:
        """Start with no file and line seen."""
        self.file: str = ""
        self.line: int = 0

    def visit(self, loc: Node) -> None:
        """Update the current file and line from one JSON location.

        Args:
            loc: A clang JSON location object (possibly with spelling/expansion parts).
        """
        for part in ("spellingLoc", "expansionLoc"):
            if part in loc:
                self.visit(loc[part])
        if "file" in loc:
            self.file = loc["file"]
        if "line" in loc:
            self.line = loc["line"]

    def visit_node(self, node: Node) -> tuple[str, int]:
        """Update from a node's own location and range, in clang's printing order.

        Args:
            node: A clang JSON AST node.

        Returns:
            The (file, line) of the node's start: its expansion location, where code is.
        """
        start: tuple[str, int] = (self.file, self.line)
        if "loc" in node:
            self.visit(node["loc"])
        node_range: Node = node.get("range", {})
        if "begin" in node_range:
            self.visit(node_range["begin"].get("expansionLoc", node_range["begin"]))
            start = (self.file, self.line)
            self.visit(node_range["begin"])
        if "end" in node_range:
            self.visit(node_range["end"])
        return start


def strip(node: Node) -> Node:
    """Remove the wrappers that do not change what an expression is.

    Args:
        node: An expression node.

    Returns:
        The node under any implicit casts, parentheses and constant-expression wrappers.
    """
    current: Node = node
    while current.get("kind") in (
        "ImplicitCastExpr",
        "ParenExpr",
        "ConstantExpr",
    ) and current.get("inner"):
        current = current["inner"][0]
    return current


def text(node: Node) -> str:
    """Render an expression as canonical text, so two spellings of one subject compare equal.

    Args:
        node: An expression node.

    Returns:
        A short text form; unrecognized expressions render as their node kind.
    """
    current: Node = strip(node)
    inner: list[Node] = current.get("inner", [])
    match current.get("kind"):
        case "DeclRefExpr":
            return str(current["referencedDecl"]["name"])
        case "MemberExpr":
            arrow: str = "->" if current.get("isArrow") else "."
            return f"{text(inner[0])}{arrow}{current['name']}"
        case "ArraySubscriptExpr":
            return f"{text(inner[0])}[{text(inner[1])}]"
        case "UnaryOperator":
            return f"{current['opcode']}{text(inner[0])}"
        case "CallExpr":
            return f"{text(inner[0])}({', '.join(text(arg) for arg in inner[1:])})"
        case "IntegerLiteral":
            return str(current["value"])
        case "CharacterLiteral":
            return repr(chr(int(current["value"])))
        case "StringLiteral":
            return str(current["value"])
        case "BinaryOperator":
            return f"({text(inner[0])} {current['opcode']} {text(inner[1])})"
        case "CStyleCastExpr":
            return f"({current['type']['qualType']}){text(inner[0])}"
        case _:
            return f"<{current.get('kind')}>"


def constant(node: Node) -> str | None:
    """Return an integer constant's text, or None if the node is not one.

    Args:
        node: An expression node.

    Returns:
        The literal, character or enumerator text, or None.
    """
    current: Node = strip(node)
    match current.get("kind"):
        case "IntegerLiteral" | "CharacterLiteral":
            return text(current)
        case "DeclRefExpr" if (
            current["referencedDecl"].get("kind") == "EnumConstantDecl"
        ):
            return text(current)
        case "UnaryOperator" if current.get("opcode") == "-":
            operand: str | None = constant(current["inner"][0])
            return None if operand is None else f"-{operand}"
        case _:
            return None


def string_test(node: Node) -> tuple[str, str] | None:
    """Recognize `streq(x, "lit")`, `strcmp(x, "lit") == 0` and `!strcmp(x, "lit")`.

    Args:
        node: A condition node.

    Returns:
        The (subject, literal) pair, or None.
    """
    current: Node = strip(node)
    call: Node | None = None
    match current.get("kind"):
        case "CallExpr":
            call = current
        case "UnaryOperator" if current.get("opcode") == "!":
            call = strip(current["inner"][0])
        case "BinaryOperator" if (
            current.get("opcode") == "==" and constant(current["inner"][1]) == "0"
        ):
            call = strip(current["inner"][0])
        case _:
            call = None
    if call is None or call.get("kind") != "CallExpr":
        return None
    callee: str = text(call["inner"][0])
    arguments: list[Node] = call["inner"][1:]
    if callee not in ("streq", "strcmp", "strcasecmp") or len(arguments) < 2:
        return None
    if callee == "streq" and current.get("kind") != "CallExpr":
        return None
    if callee != "streq" and current.get("kind") == "CallExpr":
        return None
    left: Node = strip(arguments[0])
    right: Node = strip(arguments[1])
    if right.get("kind") == "StringLiteral":
        return (text(left), text(right))
    elif left.get("kind") == "StringLiteral":
        return (text(right), text(left))
    else:
        return None


def classify(condition: Node) -> Test:
    """Say what one condition compares.

    Args:
        condition: An `if` condition node.

    Returns:
        Its Test; an `||` of tests on one subject and kind merges into one Test.
    """
    current: Node = strip(condition)
    found_string: tuple[str, str] | None = string_test(current)
    inner: list[Node] = current.get("inner", [])
    if found_string is not None:
        return Test(
            kind="string", subject=found_string[0], constants=(found_string[1],)
        )
    elif current.get("kind") == "BinaryOperator" and current.get("opcode") == "||":
        left: Test = classify(inner[0])
        right: Test = classify(inner[1])
        if left.kind == right.kind != "other" and left.subject == right.subject:
            return Test(
                kind=left.kind,
                subject=left.subject,
                constants=left.constants + right.constants,
            )
        else:
            return Test(kind="other", subject="", constants=())
    elif current.get("kind") == "BinaryOperator" and current.get("opcode") == "==":
        right_constant: str | None = constant(inner[1])
        left_constant: str | None = constant(inner[0])
        if right_constant is not None and left_constant is None:
            return Test(kind="int", subject=text(inner[0]), constants=(right_constant,))
        elif left_constant is not None and right_constant is None:
            return Test(kind="int", subject=text(inner[1]), constants=(left_constant,))
        else:
            return Test(kind="other", subject="", constants=())
    else:
        return Test(kind="other", subject="", constants=())


def shape(tests: list[Test]) -> str:
    """Name what a chain's tests have in common.

    Args:
        tests: The chain's tests, in order.

    Returns:
        "switch" when all compare one integer subject, "string-table" when all compare one
        string subject, and "mixed" otherwise.
    """
    kinds: set[str] = {test.kind for test in tests}
    subjects: set[str] = {test.subject for test in tests}
    if kinds == {"int"} and len(subjects) == 1:
        return "switch"
    elif kinds == {"string"} and len(subjects) == 1:
        return "string-table"
    else:
        return "mixed"


def collect_noreturn(node: Node, names: set[str]) -> None:
    """Add the name of every function the TU marks no-return.

    Args:
        node: A node to search, recursively.
        names: The set to add to.
    """
    # A no-return function carries a no-return attribute node, or says so in its type.
    if node.get("kind") == "FunctionDecl" and (
        any(
            child.get("kind")
            in ("NoReturnAttr", "CXX11NoReturnAttr", "C11NoReturnAttr")
            for child in node.get("inner", [])
        )
        or "noreturn" in node.get("type", {}).get("qualType", "")
    ):
        names.add(node["name"])
    for child in node.get("inner", []):
        collect_noreturn(child, names)


def always_leaves(statement: Node, noreturn: set[str]) -> bool:
    """Say whether a statement always leaves the enclosing statement list.

    Args:
        statement: A statement node (an `if` body).
        noreturn: Names of functions that never return.

    Returns:
        True if control cannot reach the statement after it.
    """
    current: Node = strip(statement)
    inner: list[Node] = current.get("inner", [])
    match current.get("kind"):
        case "ReturnStmt" | "BreakStmt" | "ContinueStmt" | "GotoStmt":
            return True
        case "CompoundStmt":
            return bool(inner) and always_leaves(inner[-1], noreturn)
        case "CallExpr":
            return text(inner[0]) in noreturn
        case "IfStmt" if current.get("hasElse"):
            return always_leaves(inner[-2], noreturn) and always_leaves(
                inner[-1], noreturn
            )
        case _:
            return False


@dataclass(frozen=True, slots=True, kw_only=True)
class Finding:
    """One worklog row.

    Attributes:
        where: `path:line`, repository-relative.
        function: The enclosing function.
        shape: "switch", "string-table" or "mixed" (or a switch's subject type).
        tests: How many tests (or `case` labels).
        has_else: Whether the chain ends in a plain `else` (or the switch has a `default`).
        subject: The common subject, or the distinct subjects joined by " | ".
        detail: The constants, or what follows a guard run.
    """

    where: str
    function: str
    shape: str
    tests: int
    has_else: bool
    subject: str
    detail: str

    def row(self) -> str:
        """Render as one TSV line.

        Returns:
            The tab-separated fields.
        """
        return "\t".join(
            (
                self.where,
                self.function,
                self.shape,
                str(self.tests),
                "yes" if self.has_else else "no",
                self.subject,
                self.detail,
            )
        )


class Scanner:
    """Walks one translation unit and records chains, guard runs and switches."""

    def __init__(
        self, source: pathlib.Path, directory: pathlib.Path, noreturn: set[str]
    ) -> None:
        """Prepare to scan one source file.

        Args:
            source: The absolute path of the `.c` file whose own code is reported.
            directory: The compile command's directory, which clang's relative paths start from.
            noreturn: Names of functions that never return.
        """
        self.source: str = str(source)
        self.directory: pathlib.Path = directory
        self.noreturn: set[str] = noreturn
        self.tracker: LocationTracker = LocationTracker()
        self.function: str = ""
        self.chains: list[Finding] = []
        self.runs: list[Finding] = []
        self.switches: list[Finding] = []

    def where(self, start: tuple[str, int]) -> str | None:
        """Turn a location into `path:line`, or None when it is outside the scanned file.

        Args:
            start: A (file, line) pair.

        Returns:
            The repository-relative location, or None.
        """
        if not start[0] or str((self.directory / start[0]).resolve()) != self.source:
            return None
        else:
            return f"{pathlib.Path(self.source).relative_to(ROOT)}:{start[1]}"

    def walk(self, node: Node, chained: bool = False) -> None:
        """Visit a node and its children in clang's printing order.

        Args:
            node: The node to visit.
            chained: True for an `if` that is the `else` branch of another `if`.
        """
        start: tuple[str, int] = self.tracker.visit_node(node)
        kind: str = node.get("kind", "")
        if kind == "FunctionDecl" and "name" in node:
            self.function = node["name"]
        location: str | None = self.where(start)
        if kind == "IfStmt" and not chained and location is not None:
            self.record_chain(node, location)
        elif kind == "SwitchStmt" and location is not None:
            self.record_switch(node, location)
        if kind == "CompoundStmt":
            self.record_runs(node)
        inner: list[Node] = node.get("inner", [])
        for index, child in enumerate(inner):
            is_else_if: bool = (
                kind == "IfStmt"
                and node.get("hasElse", False)
                and index == len(inner) - 1
                and child.get("kind") == "IfStmt"
            )
            self.walk(child, chained=is_else_if)

    def record_chain(self, node: Node, location: str) -> None:
        """Record an `if` / `else if` chain of two or more tests.

        Args:
            node: The chain's first `if`.
            location: Its `path:line`.
        """
        tests: list[Test] = []
        current: Node | None = node
        has_else: bool = False
        while current is not None:
            inner: list[Node] = current["inner"]
            tests.append(
                classify(inner[0] if current.get("hasInit") is None else inner[-3])
            )
            if not current.get("hasElse"):
                current = None
            elif inner[-1].get("kind") == "IfStmt":
                current = inner[-1]
            else:
                has_else = True
                current = None
        if len(tests) >= 2:
            self.chains.append(self.finding(location, tests, has_else, ""))

    def record_runs(self, block: Node) -> None:
        """Record each run of two or more else-less `if`s whose bodies always leave.

        Args:
            block: A compound statement.
        """
        statements: list[Node] = block.get("inner", [])
        run: list[Node] = []
        for index, statement in enumerate([*statements, {}]):
            is_guard: bool = (
                statement.get("kind") == "IfStmt"
                and not statement.get("hasElse")
                and always_leaves(statement["inner"][-1], self.noreturn)
            )
            if is_guard:
                run.append(statement)
            elif len(run) >= 2:
                following: str = (
                    "end of block" if not statement else f"then {statement.get('kind')}"
                )
                self.flush_run(run, following, index)
                run = []
            else:
                run = []

    def flush_run(self, run: list[Node], following: str, end_index: int) -> None:
        """Record one guard run, located by a fresh tracker pass over its first `if`.

        Args:
            run: The run's `if` statements.
            following: What comes after the run.
            end_index: Index of the statement after the run (unused beyond ordering).
        """
        del end_index
        probe: LocationTracker = LocationTracker()
        probe.file, probe.line = self.tracker.file, self.tracker.line
        start: tuple[str, int] = probe.visit_node(run[0])
        location: str | None = self.where(start)
        if (
            location is None
            and run[0].get("range", {}).get("begin", {}).get("offset") is not None
        ):
            location = self.where((self.source, self.line_of(run[0])))
        if location is not None:
            tests: list[Test] = [classify(statement["inner"][0]) for statement in run]
            self.runs.append(self.finding(location, tests, False, following))

    def line_of(self, node: Node) -> int:
        """Compute a node's line from its byte offset in the scanned file.

        Args:
            node: A node with a range.

        Returns:
            The 1-based line number.
        """
        begin: Node = node["range"]["begin"]
        offset: int = begin.get("expansionLoc", begin)["offset"]
        return pathlib.Path(self.source).read_bytes()[:offset].count(b"\n") + 1

    def record_switch(self, node: Node, location: str) -> None:
        """Record a switch and whether it has a default.

        Args:
            node: The switch statement.
            location: Its `path:line`.
        """
        # The subject's type after integer promotion is int, so read it under the promotion cast.
        subject_type: str = strip(node["inner"][0]).get("type", {}).get("qualType", "?")
        labels: list[str] = []
        has_default: bool = False
        for label in iter_labels(node["inner"][-1]):
            if label.get("kind") == "DefaultStmt":
                has_default = True
            else:
                labels.append(text(label["inner"][0]))
        is_enum: bool = subject_type.startswith("enum ") or any(
            strip(label_node["inner"][0]).get("referencedDecl", {}).get("kind")
            == "EnumConstantDecl"
            for label_node in iter_labels(node["inner"][-1])
            if label_node.get("kind") == "CaseStmt"
        )
        self.switches.append(
            Finding(
                where=location,
                function=self.function,
                shape="enum" if is_enum else subject_type,
                tests=len(labels),
                has_else=has_default,
                subject=text(node["inner"][0]),
                detail=f"subject type: {subject_type}",
            )
        )

    def finding(
        self, location: str, tests: list[Test], has_else: bool, detail: str
    ) -> Finding:
        """Build a chain or run row.

        Args:
            location: `path:line`.
            tests: The tests, in order.
            has_else: Whether it ends in a plain `else`.
            detail: Extra text appended after the constants.

        Returns:
            The row.
        """
        chain_shape: str = shape(tests)
        subjects: list[str] = list(
            dict.fromkeys(test.subject or "<other>" for test in tests)
        )
        constants: str = ",".join(c for test in tests for c in test.constants)
        return Finding(
            where=location,
            function=self.function,
            shape=chain_shape,
            tests=len(tests),
            has_else=has_else,
            subject=" | ".join(subjects),
            detail=" ".join(part for part in (constants, detail) if part),
        )


def iter_labels(body: Node) -> Iterator[Node]:
    """Yield a switch body's `case` and `default` labels, not descending into nested switches.

    Args:
        body: The switch's body statement.

    Yields:
        Each CaseStmt and DefaultStmt node.
    """
    for child in body.get("inner", []):
        match child.get("kind"):
            case "CaseStmt" | "DefaultStmt":
                yield child
                yield from iter_labels(child)
            case "SwitchStmt":
                pass  # a nested switch's labels are its own
            case _:
                yield from iter_labels(child)


# Compiler flags that change what clang parses: they are kept, with their value when it is a
# separate argument. Every other flag (warnings, optimization, output) is dropped.
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


def parse_flags(arguments: list[str]) -> list[str]:
    """Keep the flags of a compile command that change what clang parses.

    Args:
        arguments: The command's arguments, without the compiler itself.

    Returns:
        The include, define and standard flags, in order.
    """
    kept: list[str] = []
    take_next: bool = False
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
            pass  # warnings, optimization, -c, -o FILE's flag (its value is dropped below)
    return kept


def ast_of(entry: Node) -> Node | None:
    """Dump one compile command's file as a clang JSON AST.

    Args:
        entry: A compile_commands.json entry, with either a "command" string or an
            "arguments" list.

    Returns:
        The translation unit node, or None if clang could not parse the file (reported on
        stderr).
    """
    arguments: list[str] = (
        list(entry["arguments"])
        if "arguments" in entry
        else shlex.split(entry["command"])
    )
    command: list[str] = [
        "clang",
        *parse_flags(arguments[1:]),
        "-fsyntax-only",
        "-Xclang",
        "-ast-dump=json",
        entry["file"],
    ]
    result: subprocess.CompletedProcess[str] = subprocess.run(
        command, cwd=entry["directory"], capture_output=True, text=True, check=False
    )
    if result.returncode != 0:
        # The first error line says why; the rest of clang's output is the usual cascade.
        first_error: str = next(
            (line for line in result.stderr.splitlines() if "error" in line),
            result.stderr,
        )
        print(
            f"{entry['file']}: clang could not parse it: {first_error}", file=sys.stderr
        )
        return None
    else:
        return json.loads(result.stdout)


def accepted(where: str) -> bool:
    """Say whether a flagged site carries the accept marker.

    Args:
        where: The site's `path:line`, relative to the working directory.

    Returns:
        True if that line or the one above it contains ACCEPT_MARKER.
    """
    path, line = where.rsplit(":", 1)
    lines: list[str] = (ROOT / path).read_text(errors="replace").splitlines()
    number: int = int(line)
    return any(
        ACCEPT_MARKER in lines[index]
        for index in (number - 1, number - 2)
        if index >= 0
    )


def flagged(
    chains: list[Finding], runs: list[Finding], switches: list[Finding]
) -> list[str]:
    """List the sites the dispatch rules flag.

    Args:
        chains: Every `if` / `else if` chain of two or more tests.
        runs: Every run of early-exit `if`s.
        switches: Every `switch`.

    Returns:
        One line per flagged site: `path:line`, kind, function and detail, tab-separated.
    """
    lines: list[str] = []
    for chain in chains:
        # The constants are comma-joined in `detail`; more constants than tests means an `||` list.
        constant_count: int = len(chain.detail.split(",")) if chain.detail else 0
        if chain.shape == "switch" and (
            chain.tests >= 3 or constant_count > chain.tests
        ):
            lines.append(
                f"{chain.where}\tswitch-chain\t{chain.function}\t{chain.subject}"
            )
        if not chain.has_else:
            lines.append(
                f"{chain.where}\tno-else\t{chain.function}\t{chain.tests} tests"
            )
    lines.extend(
        f"{run.where}\tguard-run\t{run.function}\t{run.tests} ifs on {run.subject}"
        for run in runs
    )
    lines.extend(
        f"{switch.where}\tno-default\t{switch.function}\t{switch.detail}"
        for switch in switches
        if not switch.has_else
    )
    return lines


def selected(source: pathlib.Path, roots: list[pathlib.Path]) -> bool:
    """Say whether a translation unit is under one of the requested sources.

    Args:
        source: The unit's absolute path.
        roots: The requested directories and files, absolute.

    Returns:
        True if the unit is one of them or inside one of them.
    """
    return any(source == root or root in source.parents for root in roots)


def main(arguments: argparse.Namespace) -> int:
    """Scan the requested translation units, print the flagged sites, and write the worklogs.

    Args:
        arguments: The parsed command line.

    Returns:
        The exit status: 0 clean, 1 sites flagged, 2 a file could not be parsed.
    """
    build_dir: pathlib.Path = pathlib.Path(arguments.build_dir).resolve()
    roots: list[pathlib.Path] = [
        (ROOT / source).resolve() for source in (arguments.sources or ["."])
    ]
    entries: list[Node] = json.loads((build_dir / "compile_commands.json").read_text())
    by_file: dict[pathlib.Path, Node] = {}
    for entry in entries:
        source: pathlib.Path = (
            pathlib.Path(entry["directory"]) / entry["file"]
        ).resolve()
        if source.suffix == ".c" and selected(source, roots) and ROOT in source.parents:
            by_file.setdefault(source, entry)
    if not by_file:
        # A gate whose source list matches nothing must not pass.
        print(
            "no .c translation unit in the compile database matches the sources",
            file=sys.stderr,
        )
        return 2
    chains: list[Finding] = []
    runs: list[Finding] = []
    switches: list[Finding] = []
    unparsed: int = 0
    for source, entry in sorted(by_file.items()):
        unit: Node | None = ast_of(entry)
        if unit is None:
            unparsed += 1
            continue
        else:
            noreturn: set[str] = set(KNOWN_NORETURN) | set(arguments.noreturn)
            collect_noreturn(unit, noreturn)
            scanner: Scanner = Scanner(
                source, pathlib.Path(entry["directory"]), noreturn
            )
            scanner.walk(unit)
            chains += scanner.chains
            runs += scanner.runs
            switches += scanner.switches
    findings: list[str] = flagged(chains, runs, switches)
    reported: list[str] = [
        line for line in findings if not accepted(line.split("\t")[0])
    ]
    no_else: int = sum(1 for f in chains if not f.has_else)
    no_default: int = sum(1 for f in switches if not f.has_else)
    summary: list[str] = [
        f"translation units: {len(by_file) - unparsed} checked, {unparsed} not parsed",
        f"if/else-if chains (2+ tests): {len(chains)}, no final else: {no_else}",
        f"guard runs (2+ exiting ifs): {len(runs)}",
        f"switch statements: {len(switches)}, no default: {no_default}",
        f"flagged: {len(reported)} (accepted: {len(findings) - len(reported)})",
    ]
    if arguments.out is not None:
        out_dir: pathlib.Path = pathlib.Path(arguments.out)
        header: str = "where\tfunction\tshape\ttests\thas_else\tsubject\tdetail"
        out_dir.mkdir(parents=True, exist_ok=True)
        for name, rows in (
            ("if_chains", chains),
            ("guard_runs", runs),
            ("switches", switches),
        ):
            rows_text: list[str] = [header, *(finding.row() for finding in rows)]
            (out_dir / f"{name}.tsv").write_text("\n".join(rows_text) + "\n")
        (out_dir / "summary.txt").write_text("\n".join(summary) + "\n")
    for line in reported:
        print(line)
    print("\n".join(summary), file=sys.stderr)
    if unparsed:
        return 2
    elif reported:
        return 1
    else:
        return 0


def parse_arguments() -> argparse.Namespace:
    """Read the command line.

    Returns:
        The parsed arguments.
    """
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="List the C sites the C coding standard's dispatch rules flag."
    )
    parser.add_argument("build_dir", help="the directory holding compile_commands.json")
    parser.add_argument(
        "sources",
        nargs="*",
        help="directories or .c files to check (default: everything)",
    )
    parser.add_argument(
        "--noreturn",
        action="append",
        default=[],
        metavar="NAME",
        help="a function that never returns but is not marked so (repeatable)",
    )
    parser.add_argument(
        "--out", metavar="OUTDIR", help="also write the full worklogs here"
    )
    return parser.parse_args()


if __name__ == "__main__":
    sys.exit(main(parse_arguments()))
