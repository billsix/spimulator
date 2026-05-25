..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

.. _historyap:

Document History
================

-  12/17/2002 - Version 0.5 - Initial posting of book under GNU FDL
-  07/18/2003 - Version 0.6 - Added ASCII appendix, finished the
   discussion of the CPU in the Memory chapter, reworked exercises
   into a new format, corrected several errors.  Thanks to Harald
   Korneliussen for the many suggestions and the ASCII table.
-  01/11/2004 - Version 0.7 - Added C translation appendix, added
   the beginnings of an appendix of x86 instructions, added the
   beginnings of a GDB appendix, finished out the files chapter,
   finished out the counting chapter, added a records chapter,
   created a source file of common linux definitions, corrected
   several errors, and lots of other fixes
-  01/22/2004 - Version 0.8 - Finished GDB appendix, mostly finished
   w/ appendix of x86 instructions, added section on planning
   programs, added lots of review questions, and got everything to
   a completed, initial draft state.
-  01/29/2004 - Version 0.9 - Lots of editing of all chapters.
   Made code more consistent and made explanations clearer.  Added
   some illustrations.
-  01/31/2004 - Version 1.0 - Rewrote chapter 9.  Added full index.
   Lots of minor corrections.
-  04/18/2004 - Version 1.1 - Lots of minor updates based on reader
   comments.  Made cleared distinction between dynamic and shared
   libraries.
-  2026 - MIPS/spimulator port (William Emerison Six) - Retargeted the
   book from i386 Linux assembly to MIPS assembly running on the
   spimulator simulator.  Rewrote the example programs in MIPS and
   reworked the prose to match: the syscall mechanism (``syscall``
   with ``$v0``/``$a0``), the load/store architecture, the o32 calling
   convention, ``sbrk``-based allocation, and the spimulator system-call
   table.  Replaced the x86 instruction and syscall appendices with MIPS
   equivalents, reframed the linking chapter (spimulator has no linker),
   condensed the virtual-memory discussion (spimulator presents a flat
   address space), and dropped the GUI appendix (spimulator has no GUI).
