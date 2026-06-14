..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   Port note (2026): retargeted from i386 Linux (as/ld, int $0x80,
   %eax/%ebx) to MIPS on spimulator (syscall, $v0/$a0).  The
   teaching arc is Jonathan Bartlett's; the assembly and the
   build/run mechanics are MIPS/spimulator.

.. _firstprogs:

Your First Programs
===================

In this chapter you will learn the process for writing and running
assembly-language programs. We are going to write MIPS assembly and
run it on **spimulator**, a fork of James Larus's *spim* simulator for
the MIPS processor. Even though spimulator runs a simulated CPU rather
than a chip you can hold, the lessons are real: spimulator behaves like
any other Unix process. It reads from
standard input, writes to standard output, and hands the shell a
status code when it exits. Assembly language just happens to be the
language it speaks.

You will also learn the structure of an assembly-language program and
a handful of instructions. As you go through this chapter, you may
want to refer to :ref:`instructionsappendix` and :ref:`syscallap`.

These programs may overwhelm you at first. Go through them with
diligence, read them and their explanations as many times as
necessary, and you will have a solid foundation to build on. Tinker
with the programs as much as you can. Even when your tinkering does
not work, every failure helps you learn.

Entering in the Program
-----------------------

This first program is simple. In fact, it does nothing but exit! It is
short, but it shows the basics of an assembly program and of asking the
operating system to do something on your behalf. Enter it in an editor
exactly as written, with the filename ``exit.asm``. Don't worry about
understanding it yet — this section is only about typing it in and
running it. In :ref:`assemblyoutline` we describe how it works.

.. literalinclude:: ../../src/exit.asm
   :language: gas
   :linenos:
   :lineno-match:
   :caption: src/exit.asm

What you typed in is the *source code* — the human-readable form of a
program. To run it, we hand it to spimulator.

Assembling and Running It
-------------------------

On a real machine you would *assemble* the source into machine code
and then *link* it into an executable file, using two separate tools.
spimulator folds those steps together: it reads your assembly source,
assembles it in memory, and runs it, all in one command::

   spimulator -f exit.asm

``spimulator`` is the simulator, and ``-f exit.asm`` tells it which
source file to load and run. There is no separate object file and no
linker step — which also means there is no ELF executable produced on
disk. The program exists only while spimulator is running it. (We will come
back to what a real linker does, and why spimulator does not need one, in
:ref:`linking`.)

When you run this program, the only thing that happens is that you
return to the next shell prompt. That is because the program does
nothing but exit. However, immediately after it runs, type::

   echo $?

It will print ``0``. Every program, when it exits, hands the operating
system an *exit status code* that says whether everything went all
right. By convention 0 means success; other numbers indicate failure,
or carry some other meaning the programmer chooses. The shell remembers
the status of the last program in the variable ``$?``.

This is worth pausing on, because it is the heart of how this book
views spimulator. A spimulator program is **just another Unix process**: you run
it from the shell, and it reports a status the shell can see — exactly
like ``ls`` or ``grep`` or any program written in C. The fact that it
is written in MIPS assembly and executed by a simulator changes
nothing about that relationship. In the following section we look at
what each part of the program does.

.. _assemblyoutline:

Outline of an Assembly-Language Program
---------------------------------------

Take another look at ``exit.asm``. At the top are many lines beginning
with a hash (``#``). These are *comments*. The assembler ignores them
entirely; they exist only for humans. Get into the habit of writing
comments that explain both *why* a program exists and *how* it works —
most programs you write will later be read, and modified, by someone
else (often a future version of you).

After the comments come the two *sections* of the program:

::

       .data

The ``.data`` section is where you declare storage that is set up
before the program runs — initialized constants and reserved buffers.
The ``exit`` program has nothing to store, so its data section is
empty (or absent).

::

       .text

The ``.text`` section is where the program's *instructions* live.
("Text" is the traditional name for the part of a program that holds
code.) Everything the CPU executes is here.

::

       .globl main

This tells the assembler that ``main`` is a name worth remembering —
specifically, that it is the entry point where execution should begin.
``main`` is a *symbol*: a name that stands for an address. spimulator's
startup code calls ``main`` to begin your program, just as a C
runtime calls the ``main`` function.

::

   main:

A name followed by a colon *defines a label* — it marks this spot in
the program with a symbol, so other instructions (and the startup
code) can refer to it by name instead of by a raw address. ``main:``
defines where the ``main`` symbol points.

Registers
~~~~~~~~~

Now the instructions. To understand them you need to know about
*registers*. A register is a small, fast storage location built into
the CPU itself. MIPS has **32 general-purpose registers**, each
holding one 32-bit word. By convention each has a name describing its
job:

-  ``$zero`` is special: it always reads as 0, no matter what you try
   to store in it. It is astonishingly useful.
-  ``$v0`` and ``$v1`` hold **values returned** from a function — and,
   as we are about to see, the **system-call number**.
-  ``$a0`` through ``$a3`` hold **arguments** passed to a function or a
   system call.
-  ``$t0`` through ``$t9`` are **temporaries** — scratch registers you
   may freely clobber.
-  ``$s0`` through ``$s7`` are **saved** registers — a function that
   uses them must preserve their old contents (more on this in
   :ref:`functionschapter`).
-  ``$sp`` is the **stack pointer**, ``$ra`` the **return address**,
   and a few others (``$gp``, ``$fp``, ``$k0``/``$k1``, ``$at``) have
   specialized roles you will meet later.

The most important thing to know about MIPS up front is that it is a
**load/store architecture**. Arithmetic instructions operate *only* on
registers — they cannot read or write memory directly. To use a value
that lives in memory, you must first ``lw`` (load word) it into a
register; to save a register's value to memory, you ``sw`` (store
word) it. This is different from many older processors (including the
i386 this book was first written for), where a single instruction
could fetch from memory, compute, and store back. On MIPS those are
always separate steps, which makes the cost of touching memory
explicit. We will see this clearly in the maximum program.

The exit system call
~~~~~~~~~~~~~~~~~~~~~

Here are the three instructions that make up the body of the program:

.. literalinclude:: ../../src/exit.asm
   :language: gas
   :start-after: doc-region-begin exit syscall
   :end-before: doc-region-end exit syscall
   :caption: src/exit.asm — the exit system call

``li`` means *load immediate*: it puts a constant value into a
register. ``li $a0, 0`` puts 0 into ``$a0``, and ``li $v0, 17`` puts 17
into ``$v0``.

Why those two registers and those two numbers? Because of how a
*system call* works. When a program needs the operating system to do
something it cannot do for itself — such as ending the process — it
asks by way of a system call. The convention on spimulator is:

-  Put the **system-call number** in ``$v0``.
-  Put the **arguments** in ``$a0``, ``$a1``, and so on.
-  Execute the ``syscall`` instruction to hand control to the
   operating system.

System call **17** is ``exit2`` — "exit with a status." It takes the
status code in ``$a0`` and never returns. So ``$a0 = 0`` (the status),
``$v0 = 17`` (the call), and ``syscall`` ends the program and hands the
0 back to the shell, where you saw it as ``echo $?``.

This is precisely what *Programming from the Ground Up* originally
taught on i386 Linux, where the same job was done with ``%eax = 1``,
``%ebx = status``, and the ``int $0x80`` trap. The idea is identical:
agree on which registers carry the call number and the arguments, then
execute a single instruction that traps into the operating system.
Only the register names and the numbers differ. (For the full list of
spimulator's system calls, see :ref:`syscallap`.)

Try changing the status. Edit the ``li $a0, 0`` to ``li $a0, 42``,
run the program again, and ``echo $?`` — you will see ``42`` come back
out. The status code is yours to choose.

A word about ``jr $ra``
~~~~~~~~~~~~~~~~~~~~~~~~

There is a second way to end a program. ``main`` may simply *return*,
with ``jr $ra`` ("jump to the return address"). spimulator's startup code
takes whatever value is in ``$v0`` at that moment and passes it to
``exit2`` for you. So this five-line program is equivalent to::

       main:
           li $v0, 0          # the status code to return
           jr $ra             # return to the runtime

The catch is that **every** ``syscall`` overwrites ``$v0`` with the
call number, so a program that does any I/O before returning must set
``$v0`` back to the status it wants right before ``jr $ra``. You will
see ``li $v0, 0`` just before the closing ``jr $ra`` in nearly every
later program for exactly this reason. The explicit ``li $v0, 17 ;
syscall`` form sidesteps the discipline, and is more honest about what
is happening.

A note on delay slots
~~~~~~~~~~~~~~~~~~~~~~

One MIPS feature will eventually surprise you: the instruction
*immediately after* a branch or jump is executed **before** the branch
takes effect. That slot is called the *branch delay slot*, and it is a
consequence of how the hardware pipeline overlaps instructions. spimulator
can simulate this faithfully or hide it; the programs in this book are
written so that the difference does not bite you, but it is worth
knowing the slot exists — it is one of the things that makes MIPS
*MIPS*, and it has no equivalent in the i386 the book started from.

Planning the Program
--------------------

Our next program finds the maximum of a list of numbers. Computers are
detail-oriented, so before writing it we have to plan a number of
things:

-  Where will the list of numbers be stored?
-  What procedure finds the maximum?
-  How much storage does that procedure need?
-  Does all the storage fit in registers, or do we need memory too?

Finding a maximum sounds trivial — you can do it at a glance. But your
mind quietly keeps track of two things while it scans: the largest
number seen *so far*, and *where you are* in the list. A computer keeps
nothing automatically; we must set aside storage for the current
position and the current maximum, and we must decide how the program
knows when to *stop*. We will store the numbers ending with a 0, so the
program can stop when it reads a 0.

Let us name the start of the list ``data_items``, and assign a register
to each running value:

-  An address register will hold the **current position** in the list
   (we walk a pointer through it).
-  One register will hold the **current highest value**.
-  One register will hold the **current element** being examined.

When the program starts and looks at the first item, that item is — by
default — the largest seen so far. From there we repeat:

#. Load the current element. If it is 0 (the terminator), stop.
#. If the current element is greater than the current maximum, it
   becomes the new maximum.
#. Advance the position to the next element.
#. Repeat.

The "if"s are *flow control*: they tell the CPU which steps to take.
MIPS expresses them with **branch** instructions (``beq``, ``bne``,
``ble``, ...), which jump to a label only when a condition holds, and
the unconditional **jump** ``j``, which always goes to a label. A loop
is built by branching back to a label at the top; the conditional
branch that leaves the loop is what keeps it from running forever.

.. _maximum:

Finding a Maximum Value
-----------------------

Enter the following program as ``maximum.asm``. The data — our list of
numbers, terminated by 0 — goes in the ``.data`` section:

.. literalinclude:: ../../src/maximum.asm
   :language: gas
   :start-after: doc-region-begin maximum data
   :end-before: doc-region-end maximum data
   :caption: maximum.asm — the list of numbers

``.word`` lays down a sequence of 32-bit words, one per number. The
final 0 is our terminator.

Here is the loop that walks the list:

.. literalinclude:: ../../src/maximum.asm
   :language: gas
   :start-after: doc-region-begin maximum loop
   :end-before: doc-region-end maximum loop
   :caption: maximum.asm — the loop

Walk through it. ``la $t0, data_items`` loads the *address* of the
list into ``$t0`` — ``la`` is "load address," a companion to ``li``.
``$t0`` is our cursor. ``lw $t1, 0($t0)`` loads the word *at the
address in* ``$t0`` into ``$t1`` — this is the load/store architecture
in action: we cannot compare a memory location directly, so we load it
into a register first. ``$t1`` starts as the maximum (the first item is
the biggest so far).

Inside ``start_loop`` we load the current element into ``$t2``. If it
is ``$zero`` we are done (``beq``). Otherwise, if it is *not* greater
than the maximum (``ble $t2, $t1, ...``) we skip the update; otherwise
``move`` it into ``$t1`` as the new maximum. Then ``addi $t0, $t0, 4``
advances the cursor by four bytes — one word — and ``j start_loop``
goes back to the top.

Notice ``addi ..., 4``. On the i386 original, the program used a
scaled-index addressing mode that multiplied the index by 4
automatically. MIPS has no such mode: each element is a 4-byte word, so
*we* add 4 to step to the next one. The load/store architecture makes
the arithmetic of walking an array explicit.

When the loop sees the terminating 0, it falls through to the exit:
``$t1`` (the maximum) is moved into ``$a0`` and handed to ``exit2``
(``syscall`` 17). Run it and check the result::

   spimulator -f maximum.asm ; echo $?

The largest number in the list is ``222``, so ``echo $?`` prints
``222``. The maximum *is* the program's exit status — the same trick
the ``exit`` program used to return 0, now returning a computed value.

Review
------

In this chapter you wrote and ran two MIPS programs on spimulator, learned
that a spimulator program is an ordinary Unix process that returns a status
code via ``$?``, met the system-call mechanism (``$v0`` = call number,
``$a0..$a3`` = arguments, ``syscall``), and saw the load/store
architecture force every memory access through explicit ``lw``/``sw``.
In the next chapter we look more closely at how the computer
represents numbers.
