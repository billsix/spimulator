:orphan:

..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

.. _guiappendix:

GUI Programming (not in the spimulator edition)
===============================================

The original *Programming from the Ground Up* closed with an appendix
on writing a GTK/GNOME graphical program in assembly, calling into the
C libraries that ship with a Linux desktop.

That appendix is **not part of this MIPS/spimulator edition**. spimulator is a
teaching simulator for the MIPS instruction set: it emulates a CPU and
a small set of operating-system services (the ones in
:ref:`syscallap`), but it has no windowing system, no shared
libraries, and no way to link against GTK. There is nothing for the
chapter to target.

If you want to pursue graphical programming, the path is the same one
the original book pointed at — move to a real machine and call into the
platform's libraries from a higher-level language — but that is beyond
what an instruction-set simulator can teach. The lessons this book
*does* teach (the Unix process model, system calls, the stack, records
in memory) are exactly the foundation you would build on to get there.
