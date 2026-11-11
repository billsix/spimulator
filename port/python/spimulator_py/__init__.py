"""spimulator-python: the spimulator MIPS simulator engine, ported from C.

The C engine (src/ and include/ in this repository) is the standard: for the
same arguments and standard input, this package prints the same standard
output and standard error and exits with the same status.  Each module
mirrors one C file; start with spim.py (the command line), then parser.py
(the assembler) and run.py (the executor).

Derived from SPIM, Copyright (c) 1990-2023, James R. Larus, and the
spimulator fork; BSD 3-Clause, see LICENSE in the repository root.  Neither
the name of the copyright holders nor the names of their contributors may
be used to endorse or promote products derived from this software without
specific prior written permission.
"""

# The modules import one another, as the C files include one another's
# headers.  Loading spim first loads them all in an order that works, so any
# module of the package can be imported on its own (doctests do).
from . import spim  # noqa: E402, F401
