..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   Port note (2026): the i386 edition taught GDB (run/stepi/breakpoints
   on a real ELF binary).  spimulator programs run inside the simulator,
   so GDB does not apply.  This appendix is retargeted to spimulator's
   own debugging surfaces: -explain (runtime per-instruction narration),
   and the static-inspection flags -print-ast / -show-expansion /
   -listing.  See examples/TEACHING-ASSEMBLER-INTERNALS.md for more.

.. _debuggingappendix:

Debugging with spimulator
=========================

By the time you read this you have probably written a program that did
not work, and stared at it wondering why. A real machine's programs are
chased down with a separate debugger like GDB; spimulator programs run
*inside* the simulator, so spimulator gives you the debugging tools
directly. The most important one is **``-explain``**, which narrates
every instruction as it runs — the equivalent of single-stepping through
a debugger, except spimulator prints the trace for you.

An Example Debugging Session
----------------------------

We will debug the ``maximum`` program from :ref:`firstprogs`. Suppose
you entered it correctly except that you left out the line that advances
the cursor:

::

       addi $t0, $t0, 4      # step the cursor forward one word

When you run it, the program never exits — it loops forever::

   spimulator -f maximum.asm
   # (never returns; press Ctrl-C)

To see *why*, run it under ``-explain``. Because the program loops, its
narration is endless too, so pipe it through a pager or ``head`` to look
at the first iterations::

   spimulator -explain -f maximum.asm 2>&1 | head -60

After an introductory note explaining the output format, each
instruction that runs prints a step like::

   Stepped at PC = 0x00400120:
       memory[0x00400120] = 0x8d420000   →   lw $t2, 0($t0)
       source line ...: lw $t2, 0($t0)

Follow the trace around one loop. You will see the same handful of
instructions repeat — that is expected, we wrote a loop. But watch
``$t0`` (the cursor) and the value loaded into ``$t2``: on every pass
they are *identical*. The program loads ``data_items[0]`` (3), compares,
and branches back — then loads ``data_items[0]`` again, forever. It
never moves on to the next element, so it never reaches the terminating
0, so the loop never exits.

That is the symptom that points straight at the cause: nothing is
changing ``$t0`` between iterations. The cursor advance — ``addi $t0,
$t0, 4`` — is missing. Add it back, run again, and the trace now shows
``$t0`` climbing by 4 each pass and ``$t2`` taking each successive value
until it hits 0 and the program exits.

Reading the narration
---------------------

Each step's header has four parts:

-  ``PC = 0xADDR`` — the address of the instruction that just ran.
-  ``memory[0xADDR] = 0xWORD`` — the 32-bit machine code there (what the
   CPU's decoder reads).
-  ``→ instruction`` — the disassembled form, with registers in ABI
   names (``$t0``, ``$sp``, …).
-  ``source line N: …`` — the line of your file it came from. If the
   disassembly does not match your source line, you wrote a *pseudo-
   instruction* that expanded into more than one real instruction (for
   example ``la`` becomes ``lui`` + ``ori``).

``-explain`` takes an optional level, ``-explain=N`` (0–4): higher levels
add, per instruction, a plain-English description of what it did, the
input registers it read, the registers and memory it wrote, and a
bit-layout of the encoding. Start low and raise it when you need more
detail. (``-x`` is the short form.)

Looking before it runs
-----------------------

Some bugs are not in *running* the program but in how your source became
instructions. spimulator can show you that without running anything:

-  **``-print-ast``** parses your file, prints the parse tree to stderr,
   and exits. Use it to confirm the assembler understood your source the
   way you meant it.
-  **``-show-expansion``** shows each *pseudo-instruction* next to what
   it expands into — for example that ``move $t2, $t0`` is really
   ``addu $t2, $zero, $t0``, or that ``b label`` is a ``beq``. (The
   ``la``/``li`` load of a 32-bit value finishes expanding into a
   ``lui``/``ori`` pair at the final encoding stage; you see that pair
   narrated as two real instructions at run time under ``-explain``.)
-  **``-listing <file>``** writes a trace of every byte the assembler
   emits, in order, so you can see exactly what landed in memory.

Together these answer "what did the assembler actually build from what I
wrote?", while ``-explain`` answers "what did the program actually do
when it ran?"

.. list-table:: spimulator debugging flags
   :header-rows: 1
   :widths: 22 40

   * - Flag
     - What it does
   * - ``-explain`` (``-x``)
     - Narrate every instruction as it runs. ``-explain=N`` sets the
       detail level (0–4).
   * - ``-print-ast``
     - Print the parse tree and exit (no code runs).
   * - ``-show-expansion``
     - Show pseudo-instructions and their real expansions, then exit.
   * - ``-listing <file>``
     - Write an assemble-time trace of every byte emitted (``-`` for
       stderr).

Review
------

Know the Concepts
~~~~~~~~~~~~~~~~~

-  Why does a separate debugger like GDB not apply to a spimulator
   program?
-  Which spimulator flag is the analogue of single-stepping, and what
   does each line of its output tell you?
-  Your program loops forever. What would you watch in the ``-explain``
   trace to find out why?
-  When would you reach for ``-show-expansion`` rather than ``-explain``?

Use the Concepts
~~~~~~~~~~~~~~~~

-  Introduce a deliberate bug into one of your programs (swap two
   registers, drop an increment) and find it using ``-explain``.
-  Run ``-show-expansion`` on a program that uses ``la`` and ``li`` with
   a large constant, and write down what each pseudo-instruction became.
