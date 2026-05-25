..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   Port note (2026): reframed for MIPS/spimulator.  The i386 original
   taught dynamic linking against glibc (ld, -lc, .so, ELF).  spimulator
   has no linker, no shared libraries, and no C library to link against,
   so this chapter teaches the IDEA of sharing code, contrasts how a real
   system does it, and shows what spimulator offers instead: loading
   several source files into one address space (-f a.asm -f b.asm) with
   cross-file .globl symbols.  The glibc examples (helloworld-lib,
   printf-example) are dropped.

.. _linking:

Sharing Functions with Code
===========================

By now you realize the computer does a lot of work even for simple
tasks, and that you do a lot of work to write the code for it. We need
ways to make this easier. Several help:

-  write in a high-level language instead of assembly (see
   :ref:`highlevellanguages`);
-  keep pre-written code you can paste into your programs;
-  keep a set of functions that *any* program can share.

The middle option — copy and paste — has real drawbacks: pasted code
often has to be modified to fit; every program carries its own copy,
wasting space; and a bug must be fixed in every copy. So the third
option, *sharing* a single central copy of common functions, is what
real systems lean on most.

How real systems share code
---------------------------

On a full operating system there are two ways to combine your code with
shared functions:

*Static linking* takes the machine code of the functions you use and
copies it into your program when you build it, using a tool called a
*linker* (``ld`` on Linux). Each source file is assembled into an
*object file*, and the linker stitches the object files — yours and the
library's — into one executable, fixing up all the addresses.

*Dynamic linking* goes further: the shared functions live in a separate
file on disk (a *shared library* — also called a *shared object*,
*dynamic library*, *DLL*, or *.so file* [1]_), and your program merely
*refers* to them. At run time the operating system's loader maps the
library in and resolves the references. This is how a C program reaches
``printf`` — it links against the C library (``libc``) with something
like ``gcc hello.c -o hello``, and ``printf`` is resolved to the copy of
``libc`` already in memory. The payoff is huge: one copy of ``libc`` is
shared by every program, and fixing a bug in it fixes every program at
once. The cost is dependency management — the tangle sometimes called
"DLL hell."

None of that exists under spimulator
-------------------------------------

spimulator is a CPU simulator with a tiny built-in operating system. It
has **no linker**, **no shared libraries**, **no ELF executables**, and
**no C library** to link against. You cannot call ``printf``; there is
nothing to link it from. Every routine your program uses, you write
yourself in MIPS, and the only "outside" services available are
spimulator's own system calls (:ref:`syscallap`).

What spimulator *does* give you is the simplest possible form of
combining code: you can load **several source files at once**, each with
its own ``-f``, and they share one address space. A symbol marked
``.globl`` in one file is visible to the others. So you can keep a
reusable routine in its own file:

::

   # greet.asm — a reusable routine, with no main of its own
           .data
   msg:    .asciiz "hello world\n"
           .text
           .globl greet              # make 'greet' visible to other files
   greet:
           li $v0, 4
           la $a0, msg
           syscall
           jr $ra

and call it from a separate program:

::

   # main.asm
           .text
           .globl main
   main:
           jal greet                 # defined over in greet.asm
           li $a0, 0
           li $v0, 17                # exit2(0)
           syscall

Run them together by naming both files::

   spimulator -f greet.asm -f main.asm

It prints ``hello world``. spimulator loaded both files into one memory
space, and ``jal greet`` in ``main.asm`` found the ``greet`` label that
``greet.asm`` exported with ``.globl``. (Make sure exactly one of the
files defines ``main``.)

This is "linking" stripped to its essence: combine several pieces of
code so they can call one another. What it is *not* is everything a real
linker does — there is no separate object-file step, no relocation, no
procedure-linkage table or global-offset table, no on-disk executable,
and no run-time loader. spimulator simply assembles all the files into
the same address space at once.

You have already been doing this
--------------------------------

The deeper point of this chapter is not the mechanics but the *idea*:
write a routine once, and call it from wherever you need it. You have
done this throughout the book — ``atoi``, ``print_uint``, ``error_exit``
are each a single routine reused by its program. Splitting such a
routine into its own ``.globl`` file, and pulling it in with another
``-f``, is the same idea scaled up to many programs. On a real system,
that scaling is exactly what static and dynamic libraries automate.

Review
------

Know the Concepts
~~~~~~~~~~~~~~~~~

-  Why is sharing one copy of a function better than pasting a copy into
   every program?
-  What is the difference between static and dynamic linking on a real
   operating system?
-  Why can't a spimulator program call ``printf``?
-  How does spimulator let two source files call each other's routines,
   and what makes a routine in one file visible to another?

Use the Concepts
~~~~~~~~~~~~~~~~

-  Take a routine you wrote earlier (say ``print_uint``) and move it into
   its own file with a ``.globl`` directive. Write a small ``main.asm``
   that calls it, and run them with two ``-f`` arguments.
-  Put two reusable routines in one "library" file and call both from a
   program in another file.

Going Further
~~~~~~~~~~~~~

-  On a Linux machine, compile a C "hello world" and use ``ldd`` to see
   which shared libraries it depends on. Then read about ``ld`` and the
   dynamic loader to see the machinery spimulator does without.

.. [1]
   The exact terminology varies by platform: *shared library* and *.so*
   on Linux, *DLL* on Windows, *dynamic library* on macOS. They are the
   same idea.
