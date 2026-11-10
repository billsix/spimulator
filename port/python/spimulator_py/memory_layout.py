"""Where each memory segment lives, and the machine's fixed constants.

These are the constants of the C engine's spim.h and memory.h.  A MIPS
program sees five segments (addresses grow to the right)::

    0x00400000  text         user instructions          (TEXT_BOT)
    0x10000000  data         user data, grows up        (DATA_BOT)
    0x7fffffff  stack        grows down from STACK_TOP
    0x80000000  kernel text  the exception handler      (K_TEXT_BOT)
    0x90000000  kernel data                             (K_DATA_BOT)
    0xffff0000  memory-mapped console registers         (MM_IO_BOT)
"""

KILO: int = 1024
MEGA: int = KILO * KILO
BYTES_PER_WORD: int = 4

TEXT_SIZE: int = 256 * KILO
K_TEXT_SIZE: int = 64 * KILO
DATA_SIZE: int = 256 * KILO
DATA_LIMIT: int = MEGA
K_DATA_SIZE: int = 64 * KILO
K_DATA_LIMIT: int = MEGA
STACK_SIZE: int = 64 * KILO
STACK_LIMIT: int = 256 * KILO

TEXT_BOT: int = 0x00400000
DATA_BOT: int = 0x10000000
STACK_TOP: int = 0x80000000
K_TEXT_BOT: int = 0x80000000
K_DATA_BOT: int = 0x90000000
MM_IO_BOT: int = 0xFFFF0000
MM_IO_TOP: int = 0xFFFFFFFF

EXCEPTION_ADDR: int = 0x80000180
"""Where the processor jumps when an exception is raised (MIPS32)."""

DEFAULT_RUN_LOCATION: str = "__start"
END_OF_TRAP_HANDLER_SYMBOL: str = "__eoth"
DEFAULT_RUN_STEPS: int = 0x7FFFFFFF  # INT_MAX

SMALL_DATA_SEG_MAX_SIZE: int = 8

IO_INTERVAL: int = 100
"""Instructions between two looks at the memory-mapped console."""
RECV_INTERVAL: int = 100
TRANS_LATENCY: int = 100
TIMER_TICK_MS: int = 10
"""The CP0 Count register advances once per this many milliseconds."""

# The memory-mapped console registers.
RECV_CTRL_ADDR: int = 0xFFFF0000
RECV_BUFFER_ADDR: int = 0xFFFF0004
RECV_READY: int = 0x1
RECV_INT_ENABLE: int = 0x2
RECV_INT_LEVEL: int = 3
TRANS_CTRL_ADDR: int = 0xFFFF0008
TRANS_BUFFER_ADDR: int = 0xFFFF000C
TRANS_READY: int = 0x1
TRANS_INT_ENABLE: int = 0x2
TRANS_INT_LEVEL: int = 2
