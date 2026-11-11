"""The simulator-wide switches and limits (the exported variables of spim.h).

Each is set from the command line by spim.py and read by the assembler and
the executor.  Modules read them as ``options.bare_machine`` (never
``from options import ...``), so a change made by the command line is seen
everywhere.
"""

from . import memory_layout as layout

bare_machine: bool = False
"""Simulate the bare machine: no pseudo-instructions, delayed branches/loads."""

accept_pseudo_insts: bool = True
"""Accept pseudo-instructions (``li``, ``la``, ...) in the assembly."""

delayed_branches: bool = False
"""Execute the instruction after a branch before the branch takes effect."""

delayed_loads: bool = False
"""Make a loaded value visible one instruction late, as on the R2000."""

quiet: bool = False
"""Do not print warnings (exception reports, the Loaded: line)."""

mapped_io: bool = False
"""Simulate the memory-mapped console (receiver/transmitter registers)."""

force_break: bool = False
"""Set to stop the executor's loop (the C engine's Ctrl-C hook)."""

spim_return_value: int = 0
"""The exit status the simulator will return (set by syscalls 10 and 17)."""

# Initial segment sizes and limits; -stext, -sdata, ... change them.
initial_text_size: int = layout.TEXT_SIZE
initial_data_size: int = layout.DATA_SIZE
initial_data_limit: int = layout.DATA_LIMIT
initial_stack_size: int = layout.STACK_SIZE
initial_stack_limit: int = layout.STACK_LIMIT
initial_k_text_size: int = layout.K_TEXT_SIZE
initial_k_data_size: int = layout.K_DATA_SIZE
initial_k_data_limit: int = layout.K_DATA_LIMIT
