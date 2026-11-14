"""Where each memory segment lives, and the machine's fixed constants.

These are the constants of the C engine's spim.h and memory.h.  A MIPS
program sees five segments (addresses grow to the right)::

    0x00400000  text         user instructions          (TEXT_BOTTOM)
    0x10000000  data         user data, grows up        (DATA_BOTTOM)
    0x7fffffff  stack        grows down from STACK_TOP
    0x80000000  kernel text  the exception handler      (KERNEL_TEXT_BOTTOM)
    0x90000000  kernel data                             (KERNEL_DATA_BOTTOM)
    0xffff0000  memory-mapped console registers         (MEMORY_MAPPED_IO_BOTTOM)
"""

KILO: int = 1024
MEGA: int = KILO * KILO
BYTES_PER_WORD: int = 4

TEXT_SIZE: int = 256 * KILO
KERNEL_TEXT_SIZE: int = 64 * KILO
DATA_SIZE: int = 256 * KILO
DATA_LIMIT: int = MEGA
KERNEL_DATA_SIZE: int = 64 * KILO
KERNEL_DATA_LIMIT: int = MEGA
STACK_SIZE: int = 64 * KILO
STACK_LIMIT: int = 256 * KILO

TEXT_BOTTOM: int = 0x00400000
DATA_BOTTOM: int = 0x10000000
STACK_TOP: int = 0x80000000
KERNEL_TEXT_BOTTOM: int = 0x80000000
KERNEL_DATA_BOTTOM: int = 0x90000000
MEMORY_MAPPED_IO_BOTTOM: int = 0xFFFF0000
MEMORY_MAPPED_IO_TOP: int = 0xFFFFFFFF

EXCEPTION_ADDRESS: int = 0x80000180
"""Where the processor jumps when an exception is raised (MIPS32)."""

DEFAULT_RUN_LOCATION: str = "__start"
END_OF_TRAP_HANDLER_SYMBOL: str = "__eoth"
DEFAULT_RUN_STEPS: int = 0x7FFFFFFF  # INT_MAX

SMALL_DATA_SEGMENT_MAX_SIZE: int = 8

IO_INTERVAL: int = 100
"""Instructions between two looks at the memory-mapped console."""
RECEIVER_INTERVAL: int = 100
TRANSMITTER_LATENCY: int = 100
TIMER_TICK_MS: int = 10
"""The CP0 Count register advances once per this many milliseconds."""

# The memory-mapped console registers.
RECEIVER_CONTROL_ADDRESS: int = 0xFFFF0000
RECEIVER_BUFFER_ADDRESS: int = 0xFFFF0004
RECEIVER_READY: int = 0x1
RECEIVER_INTERRUPT_ENABLE: int = 0x2
RECEIVER_INTERRUPT_LEVEL: int = 3
TRANSMITTER_CONTROL_ADDRESS: int = 0xFFFF0008
TRANSMITTER_BUFFER_ADDRESS: int = 0xFFFF000C
TRANSMITTER_READY: int = 0x1
TRANSMITTER_INTERRUPT_ENABLE: int = 0x2
TRANSMITTER_INTERRUPT_LEVEL: int = 2
