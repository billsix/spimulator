#!/usr/bin/env bash
# The Python port's tier-(a) gate (the python-coding-standard.md reference doc
# from github.com/billsix/runClaudeInContainer): ruff, ty, the annotate-every-
# binding check, and the doctests.  Every step runs, and the script fails if any
# failed.  Run from anywhere; the meson test port-python-lint runs it.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
status=0
(cd "$here" && ruff check .) || status=1
(cd "$here" && ruff format --check .) || status=1
# ty reads its configuration from port/python/pyproject.toml, so run it there.
(cd "$here" && ty check) || status=1
(cd "$root" && python3 tools/check_local_annotations.py --include-module port/python/spimulator_py) || status=1
python3 "$here/run_doctests.py" || status=1
exit $status
