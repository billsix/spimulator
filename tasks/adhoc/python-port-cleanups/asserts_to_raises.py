"""Turn `assert X is not None` / `assert isinstance(v, T)` into explicit raises."""

import pathlib
import re
import sys

_NOT_NONE: re.Pattern[str] = re.compile(r"^(\s+)assert (\S+) is not None$")
_ISINSTANCE: re.Pattern[str] = re.compile(r"^(\s+)assert isinstance\((\w+), (\w+)\)$")
_DEF: re.Pattern[str] = re.compile(r"^\s*def (\w+)\(")

for name in sys.argv[1:]:
    path: pathlib.Path = pathlib.Path(name)
    out: list[str] = []
    function: str = "?"
    count: int = 0
    for line in path.read_text().split("\n"):
        definition: re.Match[str] | None = _DEF.match(line)
        if definition is not None and not line.startswith("        def "):
            function = definition.group(1)
        not_none: re.Match[str] | None = _NOT_NONE.match(line)
        instance: re.Match[str] | None = _ISINSTANCE.match(line)
        if not_none is not None:
            pad: str = not_none.group(1)
            what: str = not_none.group(2)
            out.append(f"{pad}if {what} is None:")
            out.append(f'{pad}    raise AssertionError("{function}: {what} is None")')
            count += 1
        elif instance is not None:
            pad = instance.group(1)
            value: str = instance.group(2)
            kind: str = instance.group(3)
            out.append(f"{pad}if not isinstance({value}, {kind}):")
            out.append(
                f'{pad}    raise TypeError(f"{function}: expected {kind},'
                f' got {{type({value}).__name__}}")'
            )
            count += 1
        else:
            out.append(line)
    path.write_text("\n".join(out))
    print(f"{name}: {count}")
