..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   Port note (2026): retargeted to MIPS/spimulator.  The i386 original
   split the work across .include'd constant files (record-def.s,
   linux.s) and separately linked helper objects (read-record.s,
   write-record.s, count-chars.s, write-newline.s).  spimulator's
   assembler has no .equ/.include, and the curriculum's house style is
   one self-contained file per program, so each program here inlines
   what it needs and reaches the kernel through spimulator's syscalls
   directly.  The record format and the teaching arc are unchanged.

.. _records:

Reading and Writing Simple Records
==================================

As mentioned in :ref:`filesch`, many applications deal with data that is
*persistent* — it lives longer than the program by being stored on disk.
There are two basic kinds of persistent data: structured and
unstructured. Unstructured data is like what the ``toupper`` program
dealt with — text a person typed, which a program can't really interpret.
Structured data, on the other hand, is divided into fixed-length
*records* and fixed-format *fields*, which a program can interpret
directly. Structured data can contain variable-length fields, but at
that point you are usually better off with a database. [1]_

This chapter reads and writes simple fixed-length records. Say we want
to store basic information about people we know, in this fixed-length
record:

-  Firstname — 40 bytes
-  Lastname — 40 bytes
-  Address — 240 bytes
-  Age — 4 bytes

Everything is character data except the age, a 4-byte word. (We could
use a single byte, but a word is easier to process and keeps the record
size a multiple of 4.) The whole record is **324 bytes**.

The record layout, by offset
-----------------------------

To reach a field we need its *offset* — how many bytes it sits from the
start of the record. The i386 original kept these in a ``.equ`` constant
file (``record-def.s``); spimulator's assembler has no ``.equ``, so we
simply use the literal offsets, documented once:

==========  ======  =====
field       offset   size
==========  ======  =====
firstname        0     40
lastname        40     40
address         80    240
age            320      4
**total**            **324**
==========  ======  =====

Likewise, the i386 original collected the Linux system-call numbers in a
``linux.s`` file. Under spimulator the system calls we need —
``open`` (13), ``read`` (14), ``write`` (15), ``close`` (16) — are
built into the simulator; see :ref:`syscallap`. And where the original
factored ``read-record`` and ``write-record`` into separately linked
functions, each program below simply issues the ``read``/``write``
syscall directly on a 324-byte buffer. We will write three programs: one
to build a file of records, one to display them, and one to add a year
to every age.

Writing Records
---------------

This program writes three hardcoded records to disk. It opens the file,
writes three records, and closes it. The records live in the ``.data``
section. Each text field is laid down with ``.ascii`` for the characters
and ``.space`` for the padding that fills the field out to its full
length with zero bytes — spimulator's equivalent of the i386 original's
``.rept`` padding:

.. literalinclude:: ../../src/write-records.asm
   :language: gas
   :start-after: doc-region-begin record data
   :end-before: doc-region-end record data
   :caption: write-records.asm — the record data

Because each field is padded with zeros, every field is null-terminated,
and the whole record is exactly 324 bytes no matter how short the text
is. Opening the file uses the ``open`` syscall with the create-for-writing
flags:

.. literalinclude:: ../../src/write-records.asm
   :language: gas
   :start-after: doc-region-begin write open
   :end-before: doc-region-end write open
   :caption: write-records.asm — opening the file

Flag 65 is ``O_WRONLY | O_CREAT`` and mode 420 is octal 0644 (see the
``open`` flag table in :ref:`syscallap`). With the descriptor saved in
``$s0``, writing each record is one ``write`` syscall of 324 bytes:

.. literalinclude:: ../../src/write-records.asm
   :language: gas
   :start-after: doc-region-begin write records
   :end-before: doc-region-end write records
   :caption: write-records.asm — writing the records

Build and run it in one step::

   spimulator -f write-records.asm

This creates a file called ``test.dat`` containing the records. They
contain non-printable bytes (the null padding), so a text editor may not
show them sensibly — which is why we need the next program to read them.

Reading Records
---------------

This program reads each record and prints its first name. The i386
original needed a ``count_chars`` (``strlen``) helper to find the length
of each name before writing it. Under spimulator we lean on
``print_string`` (syscall 4), which prints from the given address up to
the first null byte — and since each field is null-padded, that stops
exactly at the end of the name. (This is why a record must always
contain at least one null byte per field.)

Open the file for reading (flag 0, ``O_RDONLY``):

.. literalinclude:: ../../src/read-records.asm
   :language: gas
   :start-after: doc-region-begin read open
   :end-before: doc-region-end read open
   :caption: read-records.asm — opening the file

Then loop: read 324 bytes, stop when a read returns fewer than that
(end of file), and otherwise print the firstname, which sits at offset 0
of the buffer:

.. literalinclude:: ../../src/read-records.asm
   :language: gas
   :start-after: doc-region-begin read loop
   :end-before: doc-region-end read loop
   :caption: read-records.asm — the read loop

Run it (after running ``write-records`` to create ``test.dat``)::

   spimulator -f read-records.asm

It prints::

   Fredrick
   Marilyn
   Derrick

Printing the firstname is just ``print_string`` on the buffer's start
address — offset 0 is the firstname field, so no arithmetic is needed
here. The next program reaches a field that is *not* at offset 0, and
there the offset matters.

Modifying the Records
---------------------

This program opens an input and an output file, reads each record, adds
one to its age, and writes the updated record out. The age is a *word*
at offset 320, so we reach it with explicit load and store:

.. literalinclude:: ../../src/add-year.asm
   :language: gas
   :start-after: doc-region-begin bump age
   :end-before: doc-region-end bump age
   :caption: add-year.asm — incrementing the age field

``lw $t2, 320($t1)`` loads the age, ``addi`` bumps it, and ``sw`` stores
it back into the buffer before the record is written out. This is the
load/store architecture doing exactly what its name says.

One subtlety bites here that did not on i386: **alignment.** A ``lw`` or
``sw`` of a word requires a 4-byte-aligned address, or spimulator traps
with an "unaligned address" error. The record buffer therefore must
start on a word boundary, which we guarantee with ``.align 2`` in front
of it::

       .align 2
   record_buffer:
       .space 324

Without that, the buffer would start wherever the preceding strings
happened to end (rarely a multiple of 4), and ``320($buffer)`` would be
unaligned. i386 allowed unaligned word access; MIPS does not, so this is
a real thing to watch for whenever you load or store a word from a
computed address. Build and run::

   spimulator -f write-records.asm     # if test.dat doesn't exist yet
   spimulator -f add-year.asm

This adds a year to every record in ``test.dat`` and writes the new
records to ``testout.dat``. To see the new ages, run ``read-records`` on
the output (rename or copy it to ``test.dat`` first), or extend
``read-records`` to also print the age with ``print_int`` (syscall 1) on
the word at offset 320.

Review
------

Know the Concepts
~~~~~~~~~~~~~~~~~

-  What is a record?
-  What is the advantage of fixed-length records over variable-length
   records?
-  Why does spimulator require the record buffer to be word-aligned
   before you ``lw`` the age, and how do you guarantee that?
-  How does ``print_string`` know where a firstname ends?
-  The age lives at offset 320. Write the three instructions that load
   it into a register, add one, and store it back. Which of those touch
   memory, and which only touch registers?

Use the Concepts
~~~~~~~~~~~~~~~~

-  Add another field to the person record, and update all three programs
   to account for it (remember to fix the offsets and the record size).
-  Write a program that uses a loop to write 30 identical records.
-  Write a program that finds the largest age in the file and returns it
   as the program's exit status. (You already wrote the array version of
   this in :ref:`firstprogs` — adapt ``maximum`` to walk records.)
-  Write a program that finds the smallest age and returns it as the
   exit status.

Going Further
~~~~~~~~~~~~~

-  Rewrite the programs to take the filenames from the command line
   (``argv``) instead of hardcoding them.
-  Add error checking: every ``open``/``read``/``write`` syscall returns
   a status in ``$v0`` (negative on error). Pick a program, check
   ``$v0``, and on error write a message to standard error (``write`` to
   fd 2) and exit non-zero — the pattern from ``error-exit``.
-  Write a program that adds one record read from the keyboard
   (``read_string``, syscall 8). You'll need to ensure each field ends
   with a null byte; use a default age since numeric input from the
   keyboard isn't covered until later.
-  Write a ``compare_strings`` function that compares two strings up to 5
   characters, then a program that prints every record whose firstname
   starts with 5 characters the user enters.

.. [1]
   A database is a program that handles persistent structured data for
   you — reading, writing, lookups, and basic processing — through a
   high-level interface. References for learning how databases work are
   listed in :ref:`wherenextch`.
