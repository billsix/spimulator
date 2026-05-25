..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   Port note (2026): retargeted to MIPS/spimulator.  brk -> sbrk
   (syscall 9).  The i386 chapter's long virtual-memory section is
   condensed to a contrast, since spimulator presents a flat address
   space and does not model paging or swap.  The allocator is
   pgu/src/alloc.asm.

.. _intermediatememory:

Intermediate Memory Topics
==========================

How a Computer Views Memory
---------------------------

A computer views memory as one long sequence of numbered storage
locations — millions of them. Everything lives there: your program's
instructions, its data, the stack. A few terms we will use:

Byte
   The basic storage location, holding 8 bits — a number from 0 to 255.

Word
   On MIPS, four bytes grouped together: 32 bits, the size of a register.

Address
   The number of a storage location. The first byte is address 0, the
   next address 1, and so on.

Pointer
   A register or memory word that *holds* an address, so it "points at"
   another location. ``la $a0, label`` loads a pointer; ``lw $t0,
   0($a0)`` follows it.

The Memory Layout of a Program
------------------------------

When your program is loaded, each section goes into its own region of
memory. The instructions (``.text``) sit at a low address; the data
(``.data``) sits above them. The **stack** sits at a high address and
grows *downward* (toward lower addresses) as you use it, while your data
region sits low and the heap grows *upward* from the top of it. Between
the top of the heap and the bottom of the stack is a gap of memory you
have not asked for yet. The highest address your program may currently
use is called the *system break* (or just the *break*).

(spimulator uses its own fixed addresses for these regions, but the
shape is the same: code and data low, stack high and descending, room
to grow in the middle.)

Unlike the i386, **MIPS has no ``push`` or ``pop`` instruction.** You
manage the stack yourself through ``$sp``, exactly as you did in
:ref:`functionschapter`: to "push" a register, subtract 4 from ``$sp``
and ``sw`` the register there; to "pop", ``lw`` it back and add 4 to
``$sp``. The hardware gives you the pointer; the discipline is yours.

Every Memory Address Is (on a Real Machine) a Lie
-------------------------------------------------

On a real operating system, the address your program uses is not the
physical location in the RAM chips. Each program runs in its own
sandbox of *virtual memory*: the OS and the CPU translate ("map") your
virtual addresses to physical ones on the fly, so several programs can
all believe they were loaded at the same address without colliding. The
gap between the heap and the stack exists because those virtual
addresses have not been mapped to physical memory yet — touching them
gives a "segmentation fault." Real systems even map some of that virtual
memory to *disk* (swap), paging it in and out behind your back.

**spimulator does not model any of this.** It gives your program one
flat, simple address space; the addresses you compute are real offsets
into it, and ``sbrk`` simply hands you more of it. We mention virtual
memory only so you know it is there on a real machine — and so the next
section's "growing the break" makes sense as the simplified version of
what a real kernel does.

.. _dynamicmemory:

Getting More Memory
-------------------

If you know in advance how much storage you need, you put it in
``.data`` and it is there. But often you do not — a text editor cannot
know how long a file will be. So you ask the operating system for more
memory at run time, by growing the break.

On i386 Linux this was the ``brk`` syscall, which *set* the break to an
absolute address. spimulator provides **``sbrk``** (syscall 9), which
*grows* the heap by a number of bytes:

-  ``sbrk(0)`` (``$a0`` = 0) returns the current top of the heap — a
   query.
-  ``sbrk(n)`` (``$a0`` = n) grows the heap by ``n`` bytes and returns
   the address of the **start of the new region** (the old top). The
   result comes back in ``$v0``.

The problem with calling ``sbrk`` directly for everything is keeping
track of it all. Allocate room for one file, then another, then free the
first — now you have a hole you are not reusing, and if you keep growing
the break you run out of memory. What you need is a *memory manager*: a
pair of routines, ``allocate`` and ``deallocate``, that hand out and
reclaim blocks from a pool (the *heap*), reusing freed blocks instead of
always growing. This is *dynamic memory allocation*.

A Simple Memory Manager
-----------------------

Our manager marks each block with an 8-byte **header** holding an
"available" flag and the block's size, and hands the caller a pointer to
the bytes *after* the header — so the caller never sees the bookkeeping::

   +----------------+----------------+----------------------+
   | available flag | size of block  | the block itself ... |
   |   (4 bytes)    |   (4 bytes)    |                      |
   +----------------+----------------+----------------------+
   ^ header (offset 0)                ^ pointer handed to the caller

(Available = 1 means free; 0 means in use. spimulator's assembler has no
``.equ``, so these offsets — avail at 0, size at 4, header size 8 — are
written as literal numbers, with a comment, in ``alloc.asm``.)

Initialization records where the heap starts, by asking ``sbrk`` for the
current top:

.. literalinclude:: ../../src/alloc.asm
   :language: gas
   :start-after: doc-region-begin allocate_init
   :end-before: doc-region-end allocate_init
   :caption: alloc.asm — allocate_init

``allocate`` walks the managed regions looking for a free one big
enough. If it finds one, it marks it unavailable and returns it. If it
reaches the end without finding one, it grows the heap with ``sbrk`` and
carves the new block from there:

.. literalinclude:: ../../src/alloc.asm
   :language: gas
   :start-after: doc-region-begin allocate
   :end-before: doc-region-end allocate
   :caption: alloc.asm — allocate

Walking to the next region is pointer arithmetic: skip the 8-byte header
plus the region's own size. ``deallocate`` is almost trivial — it backs
the pointer up by 8 to reach the header and flips the available flag
back on, so a future ``allocate`` can reuse the block:

.. literalinclude:: ../../src/alloc.asm
   :language: gas
   :start-after: doc-region-begin deallocate
   :end-before: doc-region-end deallocate
   :caption: alloc.asm — deallocate

``alloc.asm`` exercises itself: its ``main`` allocates two blocks, frees
the first, then allocates a third that fits in the freed block, and
checks that the allocator handed back the *same* pointer — proving the
block was reused rather than the heap grown again. Run it::

   spimulator -f alloc.asm ; echo $?

It exits ``0`` when the freed block is reused as expected.

Review
------

Know the Concepts
~~~~~~~~~~~~~~~~~

-  Define byte, word, address, and pointer.
-  Sketch the memory layout of a running program. Which way does the
   stack grow?
-  Why does MIPS not need a ``push`` instruction, and how do you push a
   register anyway?
-  What does ``sbrk(0)`` return, and how does that differ from
   ``sbrk(100)``?
-  What is the difference, on a real machine, between a virtual and a
   physical address? Does spimulator distinguish them?
-  Why does a memory manager exist instead of just calling ``sbrk`` for
   every request?

Use the Concepts
~~~~~~~~~~~~~~~~

-  Extend ``allocate`` so that when it reuses a block much larger than
   requested, it splits the leftover into a new free block.
-  Add a ``count_free`` routine that walks the heap and returns how many
   blocks are currently marked available.
-  Write a program that allocates several blocks, stores a different
   value in each, frees one, and confirms the others are undisturbed.

Going Further
~~~~~~~~~~~~~

-  The current ``allocate`` uses a "first fit" strategy. Research
   "best fit" and "worst fit" and implement one.
-  What happens to a block's old contents when it is reused? Should
   ``allocate`` zero the memory it hands out? What are the trade-offs?
