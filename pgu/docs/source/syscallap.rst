..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   This appendix was rewritten for the MIPS/spimulator edition: the original
   listed i386 Linux system calls (number in %eax, ``int $0x80``).  It
   now lists spimulator's emulated system calls (number in $v0, ``syscall``).

.. _syscallap:

Important System Calls
======================

On a real machine, a *system call* is how a user program asks the
operating-system kernel to do something it cannot do for itself —
read a file, write to the terminal, allocate memory, or exit. The
program puts a number identifying the service it wants into an
agreed-upon register, puts the arguments into other agreed-upon
registers, and executes a trap instruction that transfers control
to the kernel.

spimulator does not run on a real kernel. Instead, spimulator *emulates* a tiny
operating system: when your program executes the ``syscall``
instruction, spimulator itself looks at your registers and performs the
requested service. The mechanism is the same one you would use on a
real MIPS Linux box — only the implementation lives inside the
simulator. (On i386 Linux, the program this book was originally
written for, the equivalent trap is ``int $0x80`` with the call
number in ``%eax``.)

The convention is:

* the **system-call number** goes in ``$v0``,
* **arguments** go in ``$a0``, ``$a1``, ``$a2``, ``$a3`` (in order),
* the ``syscall`` instruction performs the service, and
* a **result**, if any, comes back in ``$v0`` (floating-point
  results come back in ``$f0``).

.. list-table:: spimulator System Calls
   :header-rows: 1
   :widths: 4 12 26 26

   * - $v0
     - Name
     - Arguments
     - Result / Notes
   * - 1
     - ``print_int``
     - ``$a0`` = integer
     - Prints the integer in decimal.
   * - 4
     - ``print_string``
     - ``$a0`` = address of a null-terminated string
     - Prints characters until the terminating 0 byte.
   * - 5
     - ``read_int``
     -
     - Reads a decimal integer from stdin; returns it in ``$v0``.
   * - 8
     - ``read_string``
     - ``$a0`` = buffer, ``$a1`` = max length
     - Reads a line into the buffer (``fgets`` semantics).
   * - 9
     - ``sbrk``
     - ``$a0`` = number of bytes
     - Allocates that many bytes of heap; returns the address of
       the block in ``$v0``. The simulator's equivalent of ``brk``.
   * - 10
     - ``exit``
     -
     - Ends the program with status **0**, always.
   * - 11
     - ``print_char``
     - ``$a0`` = character
     - Prints one byte.
   * - 12
     - ``read_char``
     -
     - Reads one byte from stdin; returns it in ``$v0`` (-1 at EOF).
   * - 13
     - ``open``
     - ``$a0`` = null-terminated filename, ``$a1`` = flags,
       ``$a2`` = mode
     - Opens/creates a file on the host filesystem; returns the file
       descriptor in ``$v0`` (negative on error). See the flag values
       below.
   * - 14
     - ``read``
     - ``$a0`` = fd, ``$a1`` = buffer, ``$a2`` = count
     - Reads up to ``count`` bytes; returns the number actually read
       in ``$v0`` (0 at end of file).
   * - 15
     - ``write``
     - ``$a0`` = fd, ``$a1`` = buffer, ``$a2`` = count
     - Writes ``count`` bytes; returns the number written in ``$v0``.
   * - 16
     - ``close``
     - ``$a0`` = fd
     - Closes the file descriptor.
   * - 17
     - ``exit2``
     - ``$a0`` = status
     - Ends the program with the status code in ``$a0`` — this is the
       value the shell sees as ``$?``. **This is the call to use when
       you want your program to return a status**, since ``exit``
       (syscall 10) always returns 0.

The standard file descriptors are the usual Unix ones: 0 is standard
input, 1 is standard output, 2 is standard error. ``print_string``
and ``print_char`` write to standard output; to write to standard
error, use ``write`` (syscall 15) with ``$a0`` = 2.

The ``open`` syscall passes its flags (``$a1``) and mode (``$a2``)
straight through to the *host* operating system's ``open()`` call, so
the flag numbers are the host's, not spimulator's own. The values below
are for **Linux** — which is what spimulator's container runs, and what
the examples in this book assume. (On another host, for example macOS,
constants such as ``O_CREAT`` have different numeric values; if you run
on such a host, use that system's numbers.) The flags may be combined by
adding them together:

.. list-table::
   :header-rows: 1
   :widths: 8 10 30

   * - Value
     - Name
     - Meaning
   * - 0
     - ``O_RDONLY``
     - open for reading
   * - 1
     - ``O_WRONLY``
     - open for writing
   * - 0x40 (64)
     - ``O_CREAT``
     - create the file if it does not exist
   * - 0x200 (512)
     - ``O_TRUNC``
     - truncate the file to zero length on open

So ``$a1`` = 65 (``1 | 0x40``) means "open for writing, creating the
file if necessary," and ``$a2`` (the mode) of 420 is octal 0644 —
the usual permissions for a newly created file.

Returning a status to the shell
--------------------------------

Because spimulator behaves like an ordinary Unix process, the status your
program returns is visible from the shell exactly as for any other
program::

   spimulator -f exit.asm ; echo $?

A program returns a status in one of two ways:

#. Explicitly, with ``syscall`` 17 (``exit2``) and the status in
   ``$a0``.
#. Implicitly, by ``jr $ra`` from ``main`` — spimulator's startup code takes
   whatever value is in ``$v0`` at that moment and passes it to
   ``exit2`` for you. Note that every ``syscall`` clobbers ``$v0`` with
   the call number, so a program that does any I/O must set
   ``$v0`` back to the desired status (often ``li $v0, 0``)
   immediately before its final ``jr $ra``.
