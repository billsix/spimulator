..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   Port note (2026): every assembly block retargeted from i386 to MIPS.
   Notable MIPS differences called out: no .equ (literal offsets), no
   dedicated `loop` instruction (use addi+branch), no leal (use la / add).

.. _ctranslationap:

C Idioms in Assembly Language
=============================

This appendix is for C programmers learning assembly language. It shows
how the constructs you know in C map to MIPS assembly running on
spimulator.

If Statement
------------

In C an ``if`` has a condition, a true branch, and a false branch:

::

   if (a == b) {
       /* True Branch Code Here */
   } else {
       /* False Branch Code Here */
   }
   /* At This Point, Reconverge */

Assembly language is linear, so you build the blocks by jumping around
them. MIPS compares two registers directly in the branch instruction —
there is no separate compare-then-test-flags step:

::

       lw   $t0, a            # load a and b into registers
       lw   $t1, b
       beq  $t0, $t1, true_branch   # if (a == b) take the true branch

   false_branch:             # (label only for documentation)
       # False Branch Code Here
       j    reconverge        # jump past the true branch

   true_branch:
       # True Branch Code Here

   reconverge:
       # both branches continue from here

The branch family covers the comparisons directly: ``beq`` (==),
``bne`` (!=), ``blt`` (<), ``ble`` (<=), ``bgt`` (>), ``bge`` (>=). A
``switch``/``case`` statement is written as a sequence of these.

Function Call
-------------

On MIPS, the first four arguments go in ``$a0``–``$a3`` and you call
with ``jal``; the return value comes back in ``$v0``. (Unlike the i386,
nothing is pushed for these — and spimulator has no C library, so there
is no ``printf`` to call; we call our own routine instead.) The C call

::

       int n = square(7);

becomes

::

       li   $a0, 7            # first argument
       jal  square            # call; sets $ra, returns into $v0
       move $t0, $v0          # n = the return value

A function that itself makes calls must save ``$ra`` (and any
callee-saved ``$s`` registers it uses) across them — see
:ref:`functionschapter`.

Variables and Assignment
------------------------

Global and static variables live in the ``.data`` section; local
variables are reserved on the stack at the start of a function and given
back at the end. They are reached differently: a global through its
label, a local through an offset from the stack pointer. Consider:

::

   int my_global_var;

   int foo() {
       int my_local_var;
       my_local_var = 1;
       my_global_var = 2;
       return 0;
   }

In MIPS (spimulator's assembler has no ``.equ``, so the local's stack
offset is just written as a number, with a comment):

::

       .data
   my_global_var:
       .word 0

       .text
   foo:
       addi $sp, $sp, -4      # make room for my_local_var at 0($sp)

       # my_local_var = 1
       li   $t0, 1
       sw   $t0, 0($sp)       # the local, at offset 0 in the frame

       # my_global_var = 2
       li   $t0, 2
       la   $t1, my_global_var
       sw   $t0, 0($t1)       # store through the global's address

       addi $sp, $sp, 4       # release the frame
       li   $v0, 0            # return 0
       jr   $ra

Note that reaching the global takes an extra step on MIPS: you must
first load its *address* with ``la`` (which the assembler expands to
``lui``/``ori``), then load or store through that register. There is no
single instruction that touches a named memory location — this is the
load/store architecture again.

Loops
-----

Loops are built from branches, just like ``if``. A C ``while``:

::

       while (a < b) {
           /* Do stuff here */
       }

becomes

::

   loop_begin:
       lw   $t0, a
       lw   $t1, b
       bge  $t0, $t1, loop_end    # exit when !(a < b)

   loop_body:
       # Do stuff here
       j    loop_begin

   loop_end:
       # finished looping

A ``for`` loop is the same with an initialization before the loop and an
increment at the bottom. To run a body 100 times:

::

   for (i = 0; i < 100; i++) { /* ... */ }

::

       li   $t0, 0            # i = 0
       li   $t1, 100
   loop_begin:
       bge  $t0, $t1, loop_end    # while (i < 100)
       # Do process here
       addi $t0, $t0, 1      # i++
       j    loop_begin
   loop_end:

Unlike the i386, **MIPS has no dedicated ``loop`` instruction** that
decrements a counter and branches; you increment (or decrement) a
register yourself and use a conditional branch, as above. The regular,
explicit form is the only form.

Structs
-------

A struct is just a description of a block of memory. In C:

::

   struct person {
       char firstname[40];
       char lastname[40];
       int age;
   };

That is 84 bytes laid out at fixed offsets. With no ``.equ``, we record
the offsets as named numbers in a comment and use them as literal
constants:

::

   # struct person: firstname @0 (40), lastname @40 (40), age @80 (4)
   # PERSON_SIZE = 84

To reserve one as a local, make room on the stack; to set the age, store
a word at the base of the struct plus the age offset (80):

::

   foo:
       addi $sp, $sp, -84     # reserve a struct person at 0($sp)

       # p.age = 30;
       li   $t0, 30
       sw   $t0, 80($sp)      # base ($sp) + PERSON_AGE_OFFSET (80)

       addi $sp, $sp, 84
       jr   $ra

(Because ``age`` is a word, the struct's base must be word-aligned —
true here since ``$sp`` is always word-aligned. See :ref:`records`.)

Pointers
--------

A pointer is just an address held in a register or memory. Taking the
address of a global is exactly what ``la`` does:

::

   int global_data = 30;          /* C */
   a = &global_data;

::

       .data
   global_data:
       .word 30

       .text
       la   $t0, global_data  # $t0 = &global_data

For a local variable, its address is its slot in the stack frame —
compute it from ``$sp``:

::

   void foo() {
       int a;
       int *b;
       a = 30;
       b = &a;
       *b = 44;
   }

::

   foo:
       addi $sp, $sp, -8      # a @0($sp), b @4($sp)

       li   $t0, 30
       sw   $t0, 0($sp)       # a = 30

       addi $t0, $sp, 0       # b = &a   (the address of a's slot)
       sw   $t0, 4($sp)       # store the pointer b

       lw   $t1, 4($sp)       # load b
       li   $t2, 44
       sw   $t2, 0($t1)       # *b = 44

       addi $sp, $sp, 8
       jr   $ra

Where the i386 used ``leal`` ("load effective address") to compute an
address into a register, MIPS uses ``la`` for a labelled address and
plain ``addi``/``add`` to compute one from a base register like ``$sp``.
To use a pointer, load it into a register and access memory through it
with base-plus-offset (``lw``/``sw``).

Getting the Compiler to Help
----------------------------

A nice way to learn is to let a C compiler show you its assembly. With
``-S`` the compiler emits assembly instead of an object file::

   gcc -S file.c            # native assembly in file.s
   clang --target=mipsel-linux-gnu -S file.c    # MIPS assembly

Turn off optimization with ``-O0`` so the output follows your source.
The names are mostly gone — replaced by stack offsets and
compiler-generated labels — but you can trace the same logic you would
write by hand. (The example tree that ships with spimulator does exactly
this for you: each C demo is compiled to a readable assembly listing
beside its source, so you can compare your hand-written MIPS against the
compiler's.)

Try it on a C program you have written: compile it with ``-O0`` and
trace the logic, then turn optimizations on and see how the compiler
rearranges things — and try to figure out why.
