..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   Port note (2026): retargeted from the i386 cdecl convention
   (push args, %ebp frame, call/ret) to the MIPS o32 convention
   ($a0-$a3, $v0, jal/$ra) that spimulator uses.

.. _functionschapter:

All About Functions
===================

Dealing with Complexity
-----------------------

In :ref:`firstprogs` our programs were a single run of instructions. Real
programs cannot be written that way — they would be impossible to
understand, test, or share among several people. Programmers break a
program into *functions*: named units of code that each do one
well-defined job. A function is given some data to work on — its
*arguments* (or *parameters*) — and usually hands back a *return value*.
What a function does with its arguments, and what it returns, is its
*interface*. A large program is built from hundreds of small functions
calling one another, bottoming out at the *primitive functions* the
system provides — on spimulator, those are the system calls of
:ref:`syscallap`.

To use functions, you and the code that calls you must agree on the
mechanics: where do the arguments go? where does the return value come
back? who is allowed to clobber which registers? That agreement is
called a *calling convention*.

.. _howfunctionswork:

How Functions Work on MIPS
--------------------------

spimulator follows the standard MIPS **o32** calling convention. Its
rules are few:

-  The first four arguments are passed in ``$a0``, ``$a1``, ``$a2``,
   ``$a3``. (Further arguments go on the stack; we will not need that
   here.)
-  The return value comes back in ``$v0``.
-  ``jal`` ("jump and link") calls a function: it jumps to the label
   *and* stores the address of the following instruction in ``$ra``,
   the return-address register. The function returns with ``jr $ra``,
   which jumps back to that saved address.
-  Registers come in two flavors. ``$t0``–``$t9`` (and ``$a0``–``$a3``,
   ``$v0``–``$v1``) are **caller-saved** (or "temporary"): a called
   function may freely overwrite them, so if the caller needs a value
   to survive a call, *the caller* must save it. ``$s0``–``$s7`` are
   **callee-saved**: a function that wants to use one must save the old
   value and restore it before returning, so its caller sees no change.

Compare this with the i386 convention the book originally used, where
arguments were *pushed onto the stack* and read back as offsets from a
frame pointer (``%ebp``). MIPS puts the first few arguments in
registers instead, which is faster, and is why short functions on MIPS
often touch the stack very little.

The one register you can never ignore is ``$ra``. Because ``jal``
overwrites it, any function that itself makes a call — or that is
``main``, called by the runtime — must preserve ``$ra`` across the
calls it makes, or it will not be able to return.

The Stack
~~~~~~~~~

The *stack* is a region of memory used as scratch space, managed
through the stack pointer ``$sp``. It grows downward: to allocate
``n`` bytes you *subtract* ``n`` from ``$sp``; to free them you add it
back. A function's chunk of the stack is its *stack frame*. A function
uses its frame to save ``$ra`` (and any callee-saved registers, and any
locals that don't fit in registers) for the duration of a call, then
tears the frame down before returning. Because each call gets its own
frame, recursive calls nest cleanly — each invocation has private
storage.

Writing a Function: power
-------------------------

Our first function computes ``base`` raised to a power. ``main`` calls
it twice — to compute ``2^3`` and ``5^2`` — and returns their sum as the
program's exit status. Here is how ``main`` makes the calls:

.. literalinclude:: ../../src/power.asm
   :language: gas
   :start-after: doc-region-begin power calls
   :end-before: doc-region-end power calls
   :caption: power.asm — calling the function

The arguments go into ``$a0`` and ``$a1`` before each ``jal power``;
the result comes back in ``$v0``. Note the one subtlety: after the
first call, the answer (8) is in ``$v0``, but the *second* ``jal``
would overwrite ``$v0``. So ``main`` copies the first answer into
``$s1`` — a callee-saved register — to keep it alive across the second
call. (``main`` also saved the runtime's ``$ra`` in ``$s0`` at the top,
so its final ``jr $ra`` exits cleanly.)

Here is the function itself:

.. literalinclude:: ../../src/power.asm
   :language: gas
   :start-after: doc-region-begin power function
   :end-before: doc-region-end power function
   :caption: power.asm — the function

``power`` keeps its running ``result`` in a one-word stack frame, just
to show the prologue (``addi $sp, $sp, -8``) and epilogue (``addi $sp,
$sp, 8``) in their simplest form. It calls nothing, so it does not need
to save ``$ra``. Each loop iteration loads ``result`` from the frame,
multiplies it by ``base``, and stores it back — the load/store
architecture again. Run it::

   spimulator -f power.asm ; echo $?

``2^3 + 5^2`` is ``8 + 25 = 33``, so ``echo $?`` prints ``33``.

.. _recursivefunctions:

Recursive Functions
-------------------

A *recursive* function calls itself. The classic example is the
factorial: ``n! = n * (n-1)!``, with ``1! = 1`` as the base case that
stops the recursion. This is where the stack earns its keep — and where
saving ``$ra`` becomes mandatory.

.. literalinclude:: ../../src/factorial.asm
   :language: gas
   :start-after: doc-region-begin factorial
   :end-before: doc-region-end factorial
   :caption: factorial.asm — recursion

Read it carefully. The **base case** comes first: if ``n <= 1``, put 1
in ``$v0`` and return immediately. Otherwise we **recurse**, and before
the recursive ``jal`` we must save two things in a fresh stack frame:

-  ``$ra`` — our own return address, which the recursive ``jal`` is
   about to overwrite. Without saving it, we could never get back to
   *our* caller.
-  ``$a0`` — our ``n``. The recursive call needs ``$a0`` for ``n-1``,
   but we still need our own ``n`` afterward to compute ``n * (n-1)!``.

After ``jal factorial`` returns with ``(n-1)!`` in ``$v0``, we reload
our ``n`` and ``$ra``, tear down the frame, multiply, and return.

The beauty of the stack is that this just works to any depth. Computing
``factorial(5)`` stacks five frames, each holding that invocation's own
``$ra`` and ``n``; as the base case returns, the frames unwind in
reverse, each multiplying in its saved ``n``. Run it::

   spimulator -f factorial.asm ; echo $?

``factorial(5)`` is ``120``, so ``echo $?`` prints ``120``. (Keep the
argument small: a factorial grows fast, and the exit status only carries
0–255. ``factorial(6)`` is 720, which would wrap.)

A bug worth seeing on purpose
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Forget to save ``$ra`` before the recursive ``jal`` — or, in a
non-recursive function, before *any* ``jal`` — and the function returns
to the wrong place. In ``main``, that classic mistake makes the program
jump back into itself and loop forever. If a program of yours never
stops, suspect a clobbered ``$ra`` first.

.. _functionsreviewuseconcepts:

Review
------

A function communicates with its caller through a *calling convention*.
On spimulator's MIPS o32 convention: arguments in ``$a0``–``$a3``,
return value in ``$v0``, call with ``jal`` (which sets ``$ra``), return
with ``jr $ra``. ``$t`` registers are caller-saved scratch; ``$s``
registers are callee-saved. Any function that makes a call must preserve
``$ra`` (and any ``$s`` registers it uses, and any live values held in
caller-saved registers) across that call — and the stack, addressed
through ``$sp``, is where it saves them. Recursion is just a function
calling itself, with each call getting its own stack frame.
