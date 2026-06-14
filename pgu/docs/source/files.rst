..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   Port note (2026): retargeted to MIPS/spimulator.  open/read/write/
   close move from i386 Linux (%eax number, int $0x80) to spimulator's
   syscalls 13/14/15/16 ($v0 number, $a0-$a3 args).  The i386 chapter
   also introduced the .equ directive here; spimulator's assembler has
   no .equ, so that discussion is replaced.  The toupper program is
   pgu/src/toupper.asm.

.. _filesch:

Dealing with Files
==================

A lot of programming deals with files. After a reboot, the only things
that remain from previous sessions are what was written to disk. Data
stored in files is called *persistent* data, because it persists after
the program stops running.

The UNIX File Concept
---------------------

Each operating system has its own way of dealing with files, but the
UNIX method — which spimulator, like Linux, follows — is the simplest
and most universal. A UNIX file, no matter what program created it, is
just a sequential stream of bytes. You *open* it by name; the operating
system hands you a number, a *file descriptor*, that you use to refer to
the file from then on. You *read* and *write* using that descriptor, and
when you are done you *close* it, after which the descriptor is no longer
valid.

In our programs we deal with files like this:

#. **Open** the file by name, saying how you want it opened (read, write,
   create-if-absent, and so on). On spimulator this is ``syscall`` 13:
   ``$v0`` = 13, ``$a0`` = address of the filename, ``$a1`` = the flags
   (0 to read; 65 = ``O_WRONLY|O_CREAT`` to create-and-write; add 0x200
   to also truncate), ``$a2`` = the permission mode (420, octal 0644,
   is fine). The flag numbers are explained in :ref:`syscallap`, and
   why they are the numbers they are in :ref:`countingchapter`. [1]_
#. The open returns a **file descriptor** in ``$v0`` — the number you
   use to refer to the file. A negative value means the open failed.
#. **Read** and/or **write** using that descriptor. ``read`` is
   ``syscall`` 14 (``$a0`` = fd, ``$a1`` = buffer address, ``$a2`` =
   buffer size); it returns the number of bytes read in ``$v0``, or 0 at
   end of file. ``write`` is ``syscall`` 15, with the same arguments,
   except the buffer already holds the data to write. Buffers are
   explained in :ref:`buffersbss`.
#. **Close** the file with ``syscall`` 16 (``$a0`` = fd) when you are
   done.

.. _buffersbss:

Buffers and reserved storage
----------------------------

A *buffer* is a contiguous block of bytes used for bulk data transfer.
When you ask to read a file, the operating system needs somewhere to put
the bytes; that place is a buffer. Buffers are a fixed size, chosen by
the programmer: to read 500 bytes at a time, you hand ``read`` the
address of a 500-byte region and the number 500.

How do you reserve 500 bytes? You could lay down 500 zero bytes with a
``.byte`` directive, but that is tedious and clutters the source. The
``.space`` directive does it in one line — it reserves a run of zeroed
bytes::

       .data
   buffer:
       .space 500

On a real assembler there is also a separate ``.bss`` section, meant for
storage that is reserved but not initialized, so it costs nothing in the
executable file on disk. Under spimulator there is no executable file —
the simulator loads your source directly — so a zero-filled ``.space``
in ``.data`` is all we need, and is what the programs here use.

(If you have read the i386 edition of this book: it introduced the
``.equ`` directive at this point, to give names to the syscall numbers.
spimulator's assembler does **not** support ``.equ``, so the programs
here write the numbers literally, with a comment naming each one. The
full table of numbers lives in :ref:`syscallap`.)

Standard and Special Files
--------------------------

A program does not start with no files open. By convention three file
descriptors are already open when ``main`` begins:

STDIN
   *standard input*, file descriptor **0** — usually the keyboard. [3]_

STDOUT
   *standard output*, file descriptor **1** — usually the screen.
   ``print_string`` and ``print_char`` write here.

STDERR
   *standard error*, file descriptor **2** — also the screen, but kept
   separate so error messages can be redirected away from normal output.

Any of these can be redirected to or from a real file by the shell,
without the program knowing. And many "files" are not files at all:
UNIX treats pipes, devices, and network connections as files too, all
read and written with the same ``read`` and ``write`` syscalls.

Using Files in a Program
------------------------

We will write a program that takes two filenames, reads the first file,
converts every lowercase letter to uppercase, and writes the result to
the second. Thinking about it in the order we will *solve* it (which is
roughly the reverse of the order it *runs*):

-  the core job is converting a block of bytes to uppercase;
-  around that, a loop that reads a block, converts it, and writes it;
-  and before the loop, opening the two files.

Breaking a problem into smaller pieces like this, then building the
pieces back up, is one of the keys to programming. [4]_

The program reads its two filenames from the command line — ``argv[1]``
and ``argv[2]`` — the same ``argv`` mechanism introduced earlier. Here is
the whole program; put it in ``toupper.asm``:

.. literalinclude:: ../../src/toupper.asm
   :language: gas
   :linenos:
   :lineno-match:
   :caption: toupper.asm

The heart of it is the in-place conversion of one buffer-load of bytes:

.. literalinclude:: ../../src/toupper.asm
   :language: gas
   :start-after: doc-region-begin convert
   :end-before: doc-region-end convert
   :caption: toupper.asm — the uppercase conversion

For each byte, if it is between ``'a'`` and ``'z'`` we subtract 32
(``'a' - 'A'``) to get the uppercase letter, and store it back; anything
else is left alone. The bytes are touched with ``lb`` (load byte) and
``sb`` (store byte) — there is no alignment requirement for single
bytes, unlike the word access in :ref:`records`.

Around that, ``main`` opens ``argv[1]`` for reading and ``argv[2]`` for
writing (creating and truncating it), then loops: ``read`` a block into
the buffer, stop when ``read`` returns 0 or less (end of file or error),
convert the block, and ``write`` exactly the number of bytes that were
read. When the loop ends it closes both descriptors and exits. Note that
``main`` saves the two filename pointers (``argv[1]`` and ``argv[2]``)
into ``$s`` registers before the first syscall, because a ``syscall``
clobbers ``$a0`` and ``$a1``.

Build and run it on any text file::

   spimulator -f toupper.asm input.txt output.txt

``output.txt`` will contain the contents of ``input.txt`` with every
letter uppercased.

Review
------

Know the Concepts
~~~~~~~~~~~~~~~~~

-  Describe the life cycle of a file descriptor.
-  What are the standard file descriptors and what are they used for?
-  What is a buffer?
-  How do you reserve a 500-byte buffer under spimulator, and why is a
   separate ``.bss`` section unnecessary here?
-  What are the syscall numbers for reading and writing files?
-  Why does ``toupper`` save ``argv[1]`` and ``argv[2]`` into ``$s``
   registers before the first ``open``?

Use the Concepts
~~~~~~~~~~~~~~~~

-  Modify ``toupper`` to read from ``STDIN`` (fd 0) and write to
   ``STDOUT`` (fd 1) instead of named files. (Hint: skip the opens and
   use 0 and 1 as the descriptors directly.)
-  Change the size of the buffer. What difference does it make?
-  Write a program that creates a file ``heynow.txt`` and writes
   "Hey diddle diddle!" into it.
-  Write a "tolower" that converts the other direction.

Going Further
~~~~~~~~~~~~~

-  What error results can each of these syscalls return? (They come back
   in ``$v0`` as negative numbers.)
-  Make the program operate on command-line files *or* on STDIN/STDOUT
   depending on how many arguments ``argc`` reports.
-  Add error checking: after each syscall, test ``$v0`` and, on failure,
   write a message to standard error (fd 2) and exit non-zero — the
   pattern from ``error-exit``.

.. [1]
   The flag numbers are bit-patterns ORed (added) together; this is
   explained in :ref:`truthbinarynumbers`.

.. [3]
   In UNIX, almost everything is a "file" — your keyboard input and your
   screen display included.

.. [4]
   Maureen Sprankle's *Problem Solving and Programming Concepts* is an
   excellent book on the problem-solving process applied to programming.
