"""Run the doctests of every module in the Python port.

The modules use relative imports, so ``python -m doctest FILE`` can't load them;
this imports each one as part of the package and runs its doctests.  Run from
anywhere: ``python3 port/python/run_doctests.py``.  Exits 1 if any fails.
"""

import doctest
import importlib
import pathlib
import pkgutil
import sys
from types import ModuleType

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import spimulator_py


def main() -> int:
    """Run every module's doctests.

    Returns:
        0 when all pass, 1 otherwise.
    """
    failed: int = 0
    attempted: int = 0
    for info in pkgutil.iter_modules(spimulator_py.__path__):
        module: ModuleType = importlib.import_module(f"spimulator_py.{info.name}")
        result: doctest.TestResults = doctest.testmod(module)
        failed += result.failed
        attempted += result.attempted
    print(f"doctests: {attempted} run, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
