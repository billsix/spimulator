..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   This appendix tours programs from the spimulator examples curriculum
   (../../../examples/src), referenced in place, with real output and
   -explain traces captured from spimulator.

.. _examplesappendix:

Worked Examples and Debugging Output
====================================

This appendix tours a large slice of the spimulator **examples
curriculum** — the collection of MIPS programs under ``examples/src/`` —
explaining what each does, showing it run, and (for a few) walking
through spimulator's ``-explain`` narration. It has two purposes: to see
fuller programs than the focused chapter examples, and to learn to read
the debugger's per-instruction output.

Run any of these from the repository root. Some take command-line
arguments; the Unix filters read standard input::

   spimulator -f examples/src/intro/exit/exit.asm ; echo $?
   printf 'hello\n' | spimulator -f examples/src/transforms/rot13/rot13.asm

.. _explainlevels:

Reading ``-explain`` output
---------------------------

``-explain`` (short form ``-x``) makes spimulator narrate execution one
instruction at a time. It prints a one-time header describing the
format, then a step per instruction. A step header looks like::

   Stepped at PC = 0x00400028:
       memory[0x00400028] = 0x34040000   →   ori $a0, $r0, 0
       source line 110:         li $a0, 0    # the status code to return

— the **PC**, the 32-bit **machine code**, the **disassembly** (ABI
register names), and the **source line**. Here the disassembly is
``ori`` though the source says ``li``: ``li`` is a pseudo-instruction,
and this is the real instruction produced for it.

How much spimulator says is set by the level, ``-explain=N``:

- **0** — silent; the program just runs.
- **1** — the step header, and nothing more.
- **2** — adds, per instruction: its category, what it did in plain
  English, the inputs it read, what it wrote, and a "try it" hint.
- **3** — adds a bit-layout box showing the instruction's fields.
- **4** — adds a step-by-step decode of the 32-bit word.

The *same* instruction — the ``li $a0, 0`` that begins ``exit`` — at
four levels. At ``-explain=1``::

   Stepped at PC = 0x00400028:
       memory[0x00400028] = 0x34040000   →   ori $a0, $r0, 0
       source line 110:         li $a0, 0    # the status code to return

At ``-explain=2`` (adds the narration)::

     Category: Logical
     What it did:
       OR Immediate — computed $r0 OR zero-extended immediate; the
       result was placed in $a0.
     Inputs (before this step):    immediate = 0  (0x0000)
     Wrote:    $a0:  0x00000001  →  0x00000000   (decimal 0)
     PC:  0x00400028  →  0x0040002c

At ``-explain=3`` (adds the bit-layout box)::

     ┌────────┬───────┬───────┬──────────────────┐
     │ 001101 │ 00000 │ 00100 │ 0000000000000000 │
     └────────┴───────┴───────┴──────────────────┘
       opcode    rs      rt        immediate (16-bit)
       = 0x0d  = $r0   = $a0     = 0
     -> opcode=0x0d selects the `ori` instruction.

At ``-explain=4`` (walks the decode)::

     Step 1 — start with the opcode.
       opcode = 0x0d  →  `ori`, an I-type instruction.
     Step 2 — read the remaining I-type fields.
       rs = $r0   rt = $a0   imm = 0  (0x0000)

Start low to follow control flow; raise the level to dissect one
instruction. Every program also runs through spimulator's startup code
first (``__start`` in ``exceptions.s``): the first steps of any trace —
``lw $a0, 0($sp)`` ... ``jal main`` — are it building ``argc``/``argv``
and calling ``main``.

Part 1 — Intro
--------------

exit
~~~~

**exit** (``intro/exit``) does one thing: ends the process with a status
code. In C it is a single call:

.. literalinclude:: ../../../examples/src/intro/exit/exit.c
   :language: c
   :lines: 45
   :caption: intro/exit/exit.c

.. literalinclude:: ../../../examples/src/intro/exit/exit.asm
   :language: gas
   :start-after: doc-region-begin exit syscall
   :end-before: doc-region-end exit syscall
   :caption: intro/exit/exit.asm

``os_exit(0)`` becomes the syscall sequence dissected at the top of this
appendix: the status (0) into ``$a0``, the call number (17, ``exit2``)
into ``$v0``, then ``syscall``. The status is what the shell reads as
``$?``.

helloworld
~~~~~~~~~~

**helloworld** (``intro/helloworld``) prints a string and exits:

.. literalinclude:: ../../../examples/src/intro/helloworld/helloworld.c
   :language: c
   :lines: 31-34
   :caption: intro/helloworld/helloworld.c

.. literalinclude:: ../../../examples/src/intro/helloworld/helloworld.asm
   :language: gas
   :lines: 48-54
   :caption: intro/helloworld/helloworld.asm — main

The ``print_string(...)`` call is ``print_string`` syscall 4: ``$v0`` =
4 selects it, and ``$a0`` gets the *address* of the string — loaded with
``la`` (load address), since (load/store architecture) you can't name a
memory location directly. ``main`` then sets ``$v0`` back to 0 and
``jr $ra``: returning from ``main`` lets spimulator's startup code use
``$v0`` as the exit status. (Every syscall clobbered ``$v0`` with its
call number, which is why the explicit ``li $v0, 0`` is needed before
returning.) ::

   $ spimulator -f .../helloworld.asm
   hello world

print1through10
~~~~~~~~~~~~~~~

**print1through10** (``intro/print1through10``) is a counting loop:

.. literalinclude:: ../../../examples/src/intro/print1through10/print1through10.c
   :language: c
   :lines: 27-39
   :caption: intro/print1through10/print1through10.c

.. literalinclude:: ../../../examples/src/intro/print1through10/print1through10.asm
   :language: gas
   :lines: 49-71
   :caption: intro/print1through10/print1through10.asm — main

The C ``while (i <= 10)`` becomes a test-at-the-top loop: ``bgt $t0, 10,
endOfLoop`` leaves when the counter exceeds 10, the body prints ``i``
(``print_int``, syscall 1) and a newline, ``addi $t0, $t0, 1`` is the
``i++``, and ``b beginningOfLoop`` jumps back. The counter lives in a
temporary ``$t0`` because nothing here calls a function, so it never has
to survive a ``jal``. It actually counts 0 through 10 (the C starts
``i`` at 0)::

   $ spimulator -f .../print1through10.asm
   0 1 2 3 4 5 6 7 8 9 10

Under ``-explain=1`` you can watch the loop turn — the same addresses
recur as ``$t0`` climbs by one each pass::

   →   move $a0, $t0       # arg = i
   →   syscall             # print_int
   →   addi $t0, $t0, 1    # i++
   →   b beginningOfLoop   # ... and around again

increment-ints
~~~~~~~~~~~~~~

**increment-ints** (``intro/increment-ints``) is a small lesson in
pre- versus post-increment:

.. literalinclude:: ../../../examples/src/intro/increment-ints/increment-ints.c
   :language: c
   :lines: 28-40
   :caption: intro/increment-ints/increment-ints.c

.. literalinclude:: ../../../examples/src/intro/increment-ints/increment-ints-1.asm
   :language: gas
   :lines: 53-66
   :caption: intro/increment-ints/increment-ints-1.asm — first computation

The C ``++a + 5`` increments ``a`` *first* (to 6) then adds 5, giving
11; ``b++ + 5`` uses ``b`` (5) and adds 5, giving 10, *then* increments
``b``. The MIPS makes the ordering explicit: for ``++a`` it does
``addiu $t0, $t0, 1`` (the increment) *before* computing ``$t0 + 5``;
for ``b++`` it computes ``$t1 + 5`` first and only later does
``addiu $t1, $t1, 1``. The output is ``11 / 6 / 10 / 6``.

yes
~~~

**yes** (``intro/yes``) is the tightest possible loop — print ``y``
forever:

.. literalinclude:: ../../../examples/src/intro/yes/yes.c
   :language: c
   :lines: 34-36
   :caption: intro/yes/yes.c

.. literalinclude:: ../../../examples/src/intro/yes/yes.asm
   :language: gas
   :lines: 53-59
   :caption: intro/yes/yes.asm — main

The MIPS shows a small optimization the C compiler would also find:
``print_string`` always wants the same two arguments (``$v0`` = 4,
``$a0`` = the string), so they are set up *once* before the loop, and
the loop itself is just ``syscall`` followed by an unconditional
``j forever``. Stop it with Ctrl-C.

Part 2 — Algorithms
-------------------

fizzbuzz
~~~~~~~~

**fizzbuzz** (``algorithms/fizzbuzz``) — the classic interview warm-up:

.. literalinclude:: ../../../examples/src/algorithms/fizzbuzz/fizzbuzz.c
   :language: c
   :lines: 44-65
   :caption: algorithms/fizzbuzz/fizzbuzz.c — my_main

.. literalinclude:: ../../../examples/src/algorithms/fizzbuzz/fizzbuzz.asm
   :language: gas
   :lines: 88-120
   :caption: algorithms/fizzbuzz/fizzbuzz.asm — the Fizz/Buzz ladder

The interesting MIPS detail is the modulo test. MIPS has **no remainder
instruction** — instead, ``div $t0, $t1`` divides and leaves the
quotient in the ``LO`` register and the **remainder in ``HI``**; ``mfhi
$t1`` ("move from HI") fetches it. So ``i % 15 == 0`` becomes ``div``,
``mfhi``, then ``bnez`` (branch if the remainder is non-zero) to the
next test. The three tests cascade exactly as the C's ``if/else if``
ladder does::

   $ spimulator -f .../fizzbuzz.asm 15
   1 2 Fizz 4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz

sieve
~~~~~

**sieve** (``algorithms/sieve``) finds primes with the Sieve of
Eratosthenes:

.. literalinclude:: ../../../examples/src/algorithms/sieve/sieve.c
   :language: c
   :lines: 50-83
   :caption: algorithms/sieve/sieve.c — my_main

.. literalinclude:: ../../../examples/src/algorithms/sieve/sieve.asm
   :language: gas
   :lines: 89-119
   :caption: algorithms/sieve/sieve.asm — allocate and mark

Two MIPS techniques stand out. First, **dynamic allocation**: ``sbrk``
(syscall 9) grows the heap by ``limit + 1`` bytes and returns the base
of the new block in ``$v0`` — the dynamic memory of
:ref:`intermediatememory`. Second, the sieve is a *byte* array, so it
uses ``lbu`` (load byte unsigned) to test a flag and ``sb`` (store byte)
to mark a composite, rather than the word ``lw``/``sw``. The ``i * i``
bound is computed with ``mult``/``mflo`` (the product's low word)::

   $ spimulator -f .../sieve.asm 30
   2 3 5 7 11 13 17 19 23 29

bubble-sort
~~~~~~~~~~~

**bubble-sort** (``algorithms/bubble-sort``) reads integers from stdin
and sorts them in place:

.. literalinclude:: ../../../examples/src/algorithms/bubble-sort/bubble-sort.c
   :language: c
   :lines: 65-85
   :caption: algorithms/bubble-sort/bubble-sort.c — my_main

.. literalinclude:: ../../../examples/src/algorithms/bubble-sort/bubble-sort.asm
   :language: gas
   :lines: 78-92
   :caption: algorithms/bubble-sort/bubble-sort.asm — reading the input

Reading is the instructive part. ``read_int`` (syscall 5) returns the
integer in ``$v0`` *and* sets ``$a3`` to 1 at end of file — so
``bnez $a3, after_read`` ends the input loop. Each value is stored into
the ``data`` array at index ``n``: because the elements are words,
``sll $t0, $s1, 2`` multiplies the index by 4 (shift left by 2) to turn
it into a byte offset, which is then added to the array's base — the
manual index arithmetic MIPS requires (it has no scaled-index mode). The
sort itself is the textbook nested-loop swap.

binary-search
~~~~~~~~~~~~~

**binary-search** (``algorithms/binary-search``) takes a target on the
command line and looks for it in a sorted array, both ways:

.. literalinclude:: ../../../examples/src/algorithms/binary-search/binary-search.c
   :language: c
   :lines: 93-102
   :caption: algorithms/binary-search/binary-search.c — my_main

.. literalinclude:: ../../../examples/src/algorithms/binary-search/binary-search.asm
   :language: gas
   :lines: 146-161
   :caption: algorithms/binary-search/binary-search.asm — the binary search

The binary search keeps a ``lo`` and ``hi`` index and computes the
midpoint as ``mid = (lo + hi) / 2`` — and "divide by 2" is just
``srl $t3, $t3, 1`` (shift right by 1), cheaper than a ``div``. The word
at ``data[mid]`` is reached with the same ``sll ..., 2`` (×4) index
arithmetic as bubble-sort, then three branches decide the case: equal
(``beq`` → found), greater (search the lower half), otherwise the upper
half. It is worth running it next to ``linear_search`` in the same
program to feel the difference the algorithm makes.

Part 3 — Unix filters
---------------------

These read standard input and write standard output, so they drop into
a pipeline like any Unix tool. ``wc`` (below) is dissected in full; the
rest of the toolchest follows, each with its C and the telling part of
its MIPS.

tr
~~

**tr** (``transforms/tr``) is the archetype — a byte-at-a-time transform
(here, lowercase to uppercase):

.. literalinclude:: ../../../examples/src/transforms/tr/tr.c
   :language: c
   :lines: 33-41
   :caption: transforms/tr/tr.c

.. literalinclude:: ../../../examples/src/transforms/tr/tr.asm
   :language: gas
   :lines: 57-81
   :caption: transforms/tr/tr.asm

``read_char`` (syscall 12) returns one byte in ``$v0``, or -1 at end of
file. For each byte, if it's between ``'a'`` and ``'z'`` the program
subtracts 32 (``'a' - 'A'``) to uppercase it, then ``print_char`` (syscall
11). The whole filter is this read-transform-write loop::

   $ printf 'hello\n' | spimulator -f .../tr.asm
   HELLO

rot13
~~~~~

**rot13** (``transforms/rot13``) has tr's shape but a more interesting
transform — rotate each letter 13 places, wrapping around:

.. literalinclude:: ../../../examples/src/transforms/rot13/rot13.c
   :language: c
   :lines: 61-71
   :caption: transforms/rot13/rot13.c

.. literalinclude:: ../../../examples/src/transforms/rot13/rot13.asm
   :language: gas
   :lines: 79-119
   :caption: transforms/rot13/rot13.asm

The wraparound is ``(ch - 'a' + 13) % 26``, and the ``% 26`` is again a
``div`` followed by ``mfhi`` (MIPS has no remainder instruction). Because
ROT13 is its own inverse, running it twice returns the original — a handy
self-check::

   $ printf 'Hello, World!\n' | spimulator -f .../rot13.asm | spimulator -f .../rot13.asm
   Hello, World!

head
~~~~

**head** (``transforms/head``) prints the first N lines (default 10),
with an ``-n N`` flag:

.. literalinclude:: ../../../examples/src/transforms/head/head.c
   :language: c
   :lines: 52-91
   :caption: transforms/head/head.c — my_main

.. literalinclude:: ../../../examples/src/transforms/head/head.asm
   :language: gas
   :lines: 83-118
   :caption: transforms/head/head.asm — the line-counting loop

It parses ``-n N`` from ``argv`` (its own tiny ``atoi``), then copies
bytes while counting newlines, stopping once the line count reaches the
limit::

   $ printf 'a\nb\nc\nd\n' | spimulator -f .../head.asm -n 2
   a
   b

rev
~~~

**rev** (``transforms/rev``) reverses each line:

.. literalinclude:: ../../../examples/src/transforms/rev/rev.c
   :language: c
   :lines: 46-78
   :caption: transforms/rev/rev.c — my_main

.. literalinclude:: ../../../examples/src/transforms/rev/rev.asm
   :language: gas
   :lines: 75-112
   :caption: transforms/rev/rev.asm

Unlike a pure streaming filter, ``rev`` must hold a whole line before it
can print it back to front: it reads bytes into a line buffer until a
newline, then walks a pointer from the end of the buffer to the start,
printing each character::

   $ printf 'abcde\n' | spimulator -f .../rev.asm
   edcba

expand
~~~~~~

**expand** (``transforms/expand``) turns tabs into spaces, keeping
columns aligned:

.. literalinclude:: ../../../examples/src/transforms/expand/expand.c
   :language: c
   :lines: 37-72
   :caption: transforms/expand/expand.c — my_main

.. literalinclude:: ../../../examples/src/transforms/expand/expand.asm
   :language: gas
   :lines: 67-100
   :caption: transforms/expand/expand.asm

The trick is a *column counter*: a normal byte advances the column by
one, a newline resets it to zero, and a tab emits spaces until the column
reaches the next multiple of the tab width — so the alignment depends on
position, not a fixed number of spaces.

uniq
~~~~

**uniq** (``transforms/uniq``) drops adjacent duplicate lines:

.. literalinclude:: ../../../examples/src/transforms/uniq/uniq.c
   :language: c
   :lines: 47-76
   :caption: transforms/uniq/uniq.c — my_main

.. literalinclude:: ../../../examples/src/transforms/uniq/uniq.asm
   :language: gas
   :lines: 32-70
   :caption: transforms/uniq/uniq.asm

It keeps the *previous* line in a buffer; each new line is compared to it
byte by byte, and printed only if it differs. (Like the real ``uniq``,
it removes only *adjacent* duplicates.) ::

   $ printf 'a\na\nb\n' | spimulator -f .../uniq.asm
   a
   b

nl
~~

**nl** (``transforms/nl``) numbers the lines:

.. literalinclude:: ../../../examples/src/transforms/nl/nl.c
   :language: c
   :lines: 37-71
   :caption: transforms/nl/nl.c — my_main

.. literalinclude:: ../../../examples/src/transforms/nl/nl.asm
   :language: gas
   :lines: 28-68
   :caption: transforms/nl/nl.asm

A ``line_started`` flag remembers whether the current line's number has
been printed yet; at the first byte of each line it prints the
(right-padded) counter, then the text, and bumps the counter at the
newline.

base64
~~~~~~

**base64** (``transforms/base64``) is the most bit-twiddly filter:

.. literalinclude:: ../../../examples/src/transforms/base64/base64.c
   :language: c
   :lines: 39-88
   :caption: transforms/base64/base64.c — my_main

.. literalinclude:: ../../../examples/src/transforms/base64/base64.asm
   :language: gas
   :lines: 28-72
   :caption: transforms/base64/base64.asm

It reads three input bytes at a time (24 bits) and repacks them as four
6-bit groups, each used to index the base64 alphabet. The repacking is
pure shifting and masking — ``srl`` to slide a group down, ``andi`` (or
the ``& 0x3f`` mask) to keep six bits — exactly the kind of work the
:ref:`countingchapter` chapter's bit operations are for::

   $ printf 'Man' | spimulator -f .../base64.asm
   TWFu

tac
~~~

**tac** (``transforms/tac``) prints the lines in reverse order — which,
unlike the streaming filters, means it must hold the *entire* input
first:

.. literalinclude:: ../../../examples/src/transforms/tac/tac.c
   :language: c
   :lines: 30-81
   :caption: transforms/tac/tac.c — my_main

.. literalinclude:: ../../../examples/src/transforms/tac/tac.asm
   :language: gas
   :lines: 41-85
   :caption: transforms/tac/tac.asm

Because the input can be any size, ``tac`` grows its buffer with
``sbrk`` as it reads (the dynamic-memory idea of
:ref:`intermediatememory`), then scans the buffer backwards, printing
each line as it finds the newline before it::

   $ printf '1\n2\n3\n' | spimulator -f .../tac.asm
   3
   2
   1

wc, in depth
~~~~~~~~~~~~

``transforms/wc`` counts the bytes and lines of its input. It is worth
reading closely, because its three movements — choosing where to read
from, the read loop, and reporting — recur in nearly every filter.

First, the C the MIPS is a port of — every example in the curriculum is
paired with a C version, so you can read the two side by side:

.. literalinclude:: ../../../examples/src/transforms/wc/wc.c
   :language: c
   :lines: 36-70
   :caption: transforms/wc/wc.c — my_main

And the hand-written MIPS that implements it:

.. literalinclude:: ../../../examples/src/transforms/wc/wc.asm
   :language: gas
   :lines: 86-163
   :caption: transforms/wc/wc.asm — main

*Choosing the input (lines 86-112).* ``main`` first stashes the
runtime's return address in ``$s0`` (it makes many calls and wants a
clean ``jr $ra`` at the end) and defaults the file descriptor ``$s1`` to
0, standard input. It then dispatches on ``argc`` (in ``$a0``): exactly
one argument means "read stdin"; more than two is a usage error; exactly
two means a filename — or ``-`` — was given. For two arguments it loads
``argv[1]`` and peeks at its first byte: a lone ``-`` means stdin, while
anything else is opened with ``open`` (syscall 13, ``O_RDONLY``), the
returned descriptor landing in ``$s1``. A negative return means the open
failed, and it branches to the error path.

*The read loop (lines 118-131).* This is the heart of the program.
``read`` (syscall 14) is asked for **one byte** into the one-byte buffer
``oneByte``; it returns the count actually read in ``$v0``, and ``blez``
(branch if less-than-or-equal zero) leaves the loop at end of file.
Otherwise the byte counter ``$s2`` is bumped, the byte is loaded with
``lb``, and if it equals ``'\n'`` the line counter ``$s3`` is bumped as
well. Two things are worth pausing on. First, the *same* ``read`` works
whether ``$s1`` is standard input or an opened file — to the program a
file descriptor is just a number, which is the whole Unix "everything is
a file" idea. Second, the running state (``fd``, ``byte_count``,
``line_count``) lives in the **callee-saved** ``$s`` registers precisely
because every ``syscall`` clobbers the ``$a`` and ``$v`` registers; a
temporary ``$t`` register would not survive the loop.

*Reporting (lines 133-163).* At end of file it closes the descriptor
(syscall 16), but only if it isn't standard input, then prints the two
tallies with ``print_int`` (syscall 1) and their labels with
``print_string`` (syscall 4), and returns 0. The ``open_failed`` and
``usage`` paths each write a message and exit with status 1 (``exit2``) —
the robust-program pattern of :ref:`developingrobustprograms`. Run it::

   $ printf 'one two\nthree\n' | spimulator -f .../wc.asm
   14 bytes, 2 lines

(14 bytes: ``one two`` is 7, a newline, ``three`` is 5, a newline.)

**od** (``transforms/od``) dumps its input as a byte offset followed by
the byte values, like the Unix ``od`` — handy for seeing exactly what
bytes a file holds::

   $ printf 'AB' | spimulator -f .../od.asm
   0000000   A   B
   0000002

**tail** (``transforms/tail``) keeps only the last N lines, using a ring
buffer so it never has to hold the whole input in memory::

   $ printf '1\n2\n3\n4\n5\n6\n' | spimulator -f .../tail.asm -n 3
   4
   5
   6

(``transforms/`` also has ``cut`` for column extraction.)

Part 4 — Files
--------------

The file system calls of :ref:`filesch` put to work.

cat
~~~

**cat** (``fileio/cat``) copies a file (or stdin) to standard output:

.. literalinclude:: ../../../examples/src/fileio/cat/cat.c
   :language: c
   :lines: 38-61
   :caption: fileio/cat/cat.c — my_main

.. literalinclude:: ../../../examples/src/fileio/cat/cat.asm
   :language: gas
   :lines: 89-105
   :caption: fileio/cat/cat.asm — the copy loop

Where ``wc`` read one byte at a time, ``cat`` reads in **blocks**: each
``read`` (syscall 14) asks for up to 4096 bytes into ``buf`` and returns
the number actually read in ``$v0``; that same count is then handed to
``write`` (syscall 15) so only the bytes that were read are written. The
loop ends when ``read`` returns 0 (end of file). Block I/O like this is
how a real tool gets its speed::

   $ spimulator -f .../cat.asm somefile.txt        # prints the file

cp
~~

**cp** (``fileio/cp``) copies one named file to another — the same copy
loop, but with *two* descriptors:

.. literalinclude:: ../../../examples/src/fileio/cp/cp.c
   :language: c
   :lines: 25-55
   :caption: fileio/cp/cp.c — my_main

.. literalinclude:: ../../../examples/src/fileio/cp/cp.asm
   :language: gas
   :lines: 40-58
   :caption: fileio/cp/cp.asm — opening source and destination

The source is opened ``O_RDONLY`` (flag 0) and its descriptor saved in
``$s1``; the destination is opened ``O_WRONLY | O_CREAT | O_TRUNC``
(flag ``577`` = ``1 | 0x40 | 0x200``) with mode ``420`` (octal 0644),
its descriptor in ``$s2``. Then a ``read``-from-``$s1`` /
``write``-to-``$s2`` loop, identical in shape to ``cat``'s, copies the
bytes. Both fds live in callee-saved registers because the loop is full
of syscalls.

cksum
~~~~~

**cksum** (``fileio/cksum``) computes a CRC32 checksum:

.. literalinclude:: ../../../examples/src/fileio/cksum/cksum.asm
   :language: gas
   :lines: 155-168
   :caption: fileio/cksum/cksum.asm — the CRC update

This is the most arithmetic-heavy file demo. It precomputes a 256-entry
polynomial table (``crctab`` in ``.data``) and, for each input byte,
folds it into the running CRC: take the top byte of the CRC
(``srl ..., 24``), XOR in the data byte, use that as an index into the
word table (``lbu`` the index, scale by 4), and XOR the table entry back
in. It is a good study in keeping a running value in a callee-saved
register across a byte loop, and in indexing a ``.word`` table::

   $ printf 'hello\n' | spimulator -f .../cksum.asm
   3015617425 6

touch
~~~~~

**touch** (``fileio/touch``) creates an empty file — which is just open
followed immediately by close:

.. literalinclude:: ../../../examples/src/fileio/touch/touch.c
   :language: c
   :lines: 38-52
   :caption: fileio/touch/touch.c — my_main

.. literalinclude:: ../../../examples/src/fileio/touch/touch.asm
   :language: gas
   :lines: 34-50
   :caption: fileio/touch/touch.asm — main

``open`` with ``O_WRONLY | O_CREAT`` (flag ``65``) brings the file into
existence if it isn't there; nothing is written, and ``close`` (syscall
16) finishes. A negative descriptor means the create failed (e.g. a bad
path), handled by the error branch.

comm
~~~~

**comm** (``fileio/comm``) opens *two* files at once and compares their
sorted lines, printing three columns — lines only in the first (no
indent), only in the second (one tab), and common to both (two tabs):

.. literalinclude:: ../../../examples/src/fileio/comm/comm.c
   :language: c
   :lines: 55-117
   :caption: fileio/comm/comm.c — my_main

It keeps two open descriptors and a one-line buffer for each, reading a
fresh line from whichever side "fell behind" in the comparison — the
classic merge of two sorted streams. The column a line lands in is just
how many tabs are printed before it::

   $ spimulator -f .../comm.asm file1 file2
   apple                 # only in file1
           banana        # common to both (two tabs)
           cherry
       date              # only in file2 (one tab)

(``fileio/`` also has ``nologin``, a minimal open/close demo.)

Part 5 — Command-line arguments
-------------------------------

These take ``argv``; the numeric ones parse it with an ``atoi`` routine.

echo
~~~~

**echo** (``arguments/echo``) prints its arguments, space-separated:

.. literalinclude:: ../../../examples/src/arguments/echo/echo.c
   :language: c
   :lines: 44-51
   :caption: arguments/echo/echo.c — my_main

.. literalinclude:: ../../../examples/src/arguments/echo/echo.asm
   :language: gas
   :lines: 83-105
   :caption: arguments/echo/echo.asm — the argv loop

This is the clearest look at how ``argv`` is walked. ``argv`` is an
array of pointers; to reach ``argv[i]`` the loop computes ``i * 4``
(``sll $t0, $s3, 2`` — pointers are 4 bytes) and adds it to the array
base, then ``lw`` loads the pointer and ``print_string`` prints it. The
loop counts ``i`` from 1 (skipping ``argv[0]``, the program name) up to
``argc``::

   $ spimulator -f .../echo.asm hello world
   hello world

factorial
~~~~~~~~~

**factorial** (``arguments/factorial``) computes N! from a command-line N:

.. literalinclude:: ../../../examples/src/arguments/factorial/factorial.c
   :language: c
   :lines: 55-64
   :caption: arguments/factorial/factorial.c — my_main

.. literalinclude:: ../../../examples/src/arguments/factorial/factorial.asm
   :language: gas
   :lines: 92-120
   :caption: arguments/factorial/factorial.asm — main

``main`` parses ``argv[1]`` with an ``atoi`` routine (its result held in
a callee-saved register across the call), then multiplies down with
``mult``/``mflo``. Past 12! the product overflows 32 bits and the output
goes wrong — a worthwhile thing to observe rather than hide::

   $ spimulator -f .../factorial.asm 5
   120

gcd
~~~

**gcd** (``arguments/gcd``) is Euclid's algorithm on two arguments:

.. literalinclude:: ../../../examples/src/arguments/gcd/gcd.c
   :language: c
   :lines: 55-65
   :caption: arguments/gcd/gcd.c — my_main

.. literalinclude:: ../../../examples/src/arguments/gcd/gcd.asm
   :language: gas
   :lines: 115-122
   :caption: arguments/gcd/gcd.asm — the Euclid loop

The loop is wonderfully tight: while ``B`` is non-zero, replace the pair
``(A, B)`` with ``(B, A mod B)``. The ``mod`` is once again ``div``
followed by ``mfhi`` (the remainder), and the two ``move`` instructions
do the swap. Note that ``A`` and ``B`` live in ``$s1``/``$s2`` — they
must survive the ``jal atoi`` that parsed the second argument::

   $ spimulator -f .../gcd.asm 48 36
   12

factor
~~~~~~

**factor** (``arguments/factor``) prints the prime factorization:

.. literalinclude:: ../../../examples/src/arguments/factor/factor.c
   :language: c
   :lines: 23-55
   :caption: arguments/factor/factor.c — my_main

.. literalinclude:: ../../../examples/src/arguments/factor/factor.asm
   :language: gas
   :lines: 30-68
   :caption: arguments/factor/factor.asm — main

Trial division: for each candidate divisor ``d`` from 2 while
``d * d <= n``, divide out every factor of ``d`` (printing it each time)
before moving on. The ``d * d`` bound uses ``mult``/``mflo``; the
divisibility test and the division both come from ``div`` (remainder via
``mfhi``, quotient via ``mflo``)::

   $ spimulator -f .../factor.asm 60
   60: 2 2 3 5

seq
~~~

**seq** (``arguments/seq``) prints a range of integers:

.. literalinclude:: ../../../examples/src/arguments/seq/seq.c
   :language: c
   :lines: 39-56
   :caption: arguments/seq/seq.c — my_main

.. literalinclude:: ../../../examples/src/arguments/seq/seq.asm
   :language: gas
   :lines: 48-85
   :caption: arguments/seq/seq.asm — main

It parses two arguments (``atoi`` twice, the first held across the second
call) and counts from the first to the second, printing each. It is
signed-aware: if the start is below the end it counts up, otherwise down,
and chooses ``print_int`` versus ``print_uint`` accordingly::

   $ spimulator -f .../seq.asm 1 5
   1 2 3 4 5

tee
~~~

**tee** (``arguments/tee``) copies standard input to standard output
*and* to every file named on the command line:

.. literalinclude:: ../../../examples/src/arguments/tee/tee.c
   :language: c
   :lines: 55-83
   :caption: arguments/tee/tee.c — my_main

.. literalinclude:: ../../../examples/src/arguments/tee/tee.asm
   :language: gas
   :lines: 118-160
   :caption: arguments/tee/tee.asm — main

This shows a *variable* number of file descriptors. First it walks
``argv`` opening each named file (``O_WRONLY|O_CREAT|O_TRUNC``), saving
the descriptors in an array. Then, for each block read from stdin, it
writes to standard output and loops over the descriptor array writing the
same block to each file — a fan-out write::

   $ printf 'shared\n' | spimulator -f .../tee.asm out1 out2
   shared
   # ... and out1 and out2 now both contain "shared"

Part 6 — Recursion
------------------

**fibonacci** (``recursion/fibonacci``) computes the *N*-th Fibonacci
number both iteratively and recursively; its ``fib_rec`` is genuinely
recursive::

   $ spimulator -f .../fibonacci.asm 10
   iter: 55
   rec:  55

.. literalinclude:: ../../../examples/src/recursion/fibonacci/fibonacci.asm
   :language: gas
   :lines: 267-303
   :caption: recursion/fibonacci/fibonacci.asm — fib_rec (the recursive function)

Watch one call descend into the next under ``-explain=1`` (run it as
``... fibonacci.asm 10``). Each entry opens a fresh frame and saves
``$ra`` and ``$a0`` before recursing — then the same prologue appears
again, one level deeper, the frames nesting on the stack::

   →   addi $sp, $sp, -12        # open this call's frame
   →   sw $ra, 0($sp)            # save this call's return address
   →   sw $a0, 4($sp)            # save n before the first call
   →   addi $a0, $a0, -1         # argument for the recursive call: n-1
   →   jal 0x004000f4 [fib_rec]  # recurse
   →   addi $sp, $sp, -12        # ... the next call opens ITS frame
   ...

That repetition of the prologue, one level deeper each time, *is*
recursion made visible.

Two recursive calls per frame: ``hanoi``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**hanoi** (``recursion/hanoi``) prints the moves for the Towers of
Hanoi. The C is the textbook three-line solution::

   $ spimulator -f .../hanoi.asm 3
   Move disk from A to C
   Move disk from A to B
   Move disk from C to B
   Move disk from A to C
   ... (7 moves total)

.. literalinclude:: ../../../examples/src/recursion/hanoi/hanoi.c
   :language: c
   :lines: 66-76
   :caption: recursion/hanoi/hanoi.c — the recursive function

.. literalinclude:: ../../../examples/src/recursion/hanoi/hanoi.asm
   :language: gas
   :lines: 180-234
   :caption: recursion/hanoi/hanoi.asm — hanoi(n, src, dst, tmp)

This is the cleanest demonstration in the tree of *why* you save
registers across a call. The function takes four arguments in
``$a0``–``$a3`` (``n``, ``src``, ``dst``, ``tmp``) and calls itself
*twice*. The frame saves all four argument registers plus ``$ra``
(20 bytes). For the first recursive call, ``hanoi(n-1, src, tmp,
dst)``, only ``$a2`` and ``$a3`` need to swap, so the code shuffles them
through ``$t0`` and decrements ``$a0`` in place. But after that call
returns, ``$a0``–``$a3`` are *gone* — the recursion and the intervening
``print`` syscalls clobbered them — so the second call,
``hanoi(n-1, tmp, dst, src)``, reloads every argument from the frame
(``lw $a1, 16($sp)`` and friends) before ``jal``\ ing again. The frame
is the only reason ``src``, ``dst``, and ``tmp`` still exist after the
first descent. Drop the saves and the moves print garbage.

Backtracking: ``queens``
~~~~~~~~~~~~~~~~~~~~~~~~~~

**queens** (``recursion/queens``) solves the N-queens problem by
recursive backtracking, printing each solution as the column chosen in
each row::

   $ spimulator -f .../queens.asm 6
   1 3 5 0 2 4   2 5 1 4 0 3   3 0 4 1 5 2   4 2 0 5 3 1

.. literalinclude:: ../../../examples/src/recursion/queens/queens.c
   :language: c
   :lines: 68-79
   :caption: recursion/queens/queens.c — solve(row)

.. literalinclude:: ../../../examples/src/recursion/queens/queens.asm
   :language: gas
   :lines: 110-160
   :caption: recursion/queens/queens.asm — solve(row), the backtracking core

Backtracking is recursion with a loop inside each frame, and the MIPS
shows exactly that. ``solve`` saves ``$ra`` and its ``row`` argument,
plus a third slot at ``8($sp)`` for the loop variable ``col`` — note that
``col`` lives *in the frame*, not in a register, precisely because the
recursive ``jal solve`` in the middle of the loop would clobber any
``$t`` register holding it. Each iteration reloads ``col`` from the
frame, calls ``safe``, and on success writes ``columns[row] = col``
(the ``sll $t1, $t0, 2`` is the familiar ×4 for a word index) before
recursing on ``row + 1``. When ``row == N_global`` the recursion bottoms
out into ``print_solution`` and returns, and the loop in the parent frame
simply advances ``col`` and keeps searching — that resumption of the
*caller's* loop after the *callee* returns is the backtrack.

A bug on purpose: ``get-char-from-user``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The two ``recursion/get-char-from-user`` demos are the most instructive
in the whole tree, because the first one is *wrong on purpose*. It
allocates a stack frame of the wrong size, so the slot it stores into
lands at an address that is not a multiple of 4 — and a word store to an
unaligned address is illegal on MIPS. Run it under ``-explain=1`` and
you watch the exact instruction blow up::

   Stepped at PC = 0x00400034:
       memory[0x00400034] = 0xafc80000   →   sw $t0, 0($fp)
       source line 126:         sw $t0, 0($fp)   # store $t0 into the ch slot
   Exception occurred at PC=0x00400034
     Unaligned address in store: 0x7ffff3f7
     Exception 5  [Address error in store]

``$fp`` holds ``0x7ffff3f7`` — not a multiple of 4 — so the ``sw``
faults (exactly the rule from :ref:`records`). The second demo,
``get-char-from-user-2``, fixes the frame size so every slot is
word-aligned, and it just works::

   $ printf 'x\n' | spimulator -f .../get-char-from-user-2.asm
   ch was 120, value x

This is the best single argument for keeping stack frames a multiple of
4 — and a perfect example of using ``-explain`` to pinpoint a bug.

Calling conventions by hand: ``subrountines``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The two ``extras/subrountines`` demos compute the same result (printing
``5`` then ``26``) two ways: ``subrountines-1`` spells out the
stack-based calling convention longhand, while ``subrountines-2`` writes
it the idiomatic way. Reading them side by side shows what the
conventions of :ref:`functionschapter` actually buy you::

   $ spimulator -f .../subrountines-1.asm      # -2 prints the same
   5
   26

Part 7 — Libraries
------------------

A small library is just a file of ``.globl`` routines that other
programs call — the idea of :ref:`linking`. The demos here load the
library and the program together, each with its own ``-f``, and the
linker resolves the cross-file ``jal`` targets at load time.

Calling across files: ``ctype-demo``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**libctype** (``lib/libctype``) provides the character-classification
functions (``isalpha``, ``isdigit``, ...); **ctype-demo** exercises them
over every printable byte::

   $ spimulator -f examples/src/lib/libctype/libctype.asm \
                -f examples/src/lib/libctype-demo/ctype-demo.asm
   c=32 ' '  alpha=0 alnum=0 digit=0 upper=0 lower=0 space=1  up=' ' lo=' '
   c=33 '!'  alpha=0 alnum=0 digit=0 upper=0 lower=0 space=0  up='!' lo='!'
   ...

.. literalinclude:: ../../../examples/src/lib/libctype-demo/ctype-demo.c
   :language: c
   :lines: 19-30
   :caption: lib/libctype-demo/ctype-demo.c — the printing loop (excerpt)

.. literalinclude:: ../../../examples/src/lib/libctype-demo/ctype-demo.asm
   :language: gas
   :lines: 34-61
   :caption: lib/libctype-demo/ctype-demo.asm — main's loop head

The point of this demo is what survives the storm of calls. The loop
variable ``c`` and the loop bound live in ``$s1`` and ``$s2``, and the
runtime's return address is parked in ``$s0`` — all **callee-saved**
registers, chosen deliberately because the body ``jal``\ s perhaps a
dozen times per iteration (``_ps``, ``_pi``, then ``isalpha``,
``isalnum``, ``isdigit``, ... each defined in the *other* file). Had
``c`` lived in a ``$t`` register it would be destroyed by the first
``jal isalpha``. The ``jal isalpha`` itself is the linking lesson:
nothing in this file defines ``isalpha``; the symbol is resolved against
``libctype.asm`` only because both files were passed on the command line,
exactly as :ref:`linking` describes.

A function passed as data: ``bsearch-demo``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``lib/libstdlib`` is a second library (``atoi``, ``abs``, ``bsearch``,
``_Exit``). Because its ``atoi`` calls into ``libctype``, its demos load
all three files. **bsearch-demo** calls ``bsearch`` with a *comparison
routine* — a function passed as data, a pointer to code that the library
calls back::

   $ spimulator -f .../libctype.asm -f .../libstdlib.asm -f .../bsearch-demo.asm
   bsearch(5) = idx 0
   bsearch(47) = idx 4
   bsearch(100) = idx 9
   bsearch(23) = idx 2

.. literalinclude:: ../../../examples/src/lib/libstdlib-demo/bsearch-demo.asm
   :language: gas
   :lines: 37-41
   :caption: lib/libstdlib-demo/bsearch-demo.asm — the comparator

.. literalinclude:: ../../../examples/src/lib/libstdlib/libstdlib.asm
   :language: gas
   :lines: 274-296
   :caption: lib/libstdlib/libstdlib.asm — bsearch's loop, with the indirect call

The comparator ``int_cmp`` is an ordinary function: load the two ints it
is handed pointers to, subtract, return the sign in ``$v0``. What makes
it interesting is how ``bsearch`` *invokes* it. ``main`` passes the
**address** of ``int_cmp`` as the fifth argument, and ``bsearch`` stashes
it in ``$s4``; the call site is ``jalr $s4`` — "jump and link to the
address in a register" — rather than ``jal somelabel``. That single
instruction is the whole idea of a callback: the library was compiled
with no knowledge of ``int_cmp``, yet calls it on every probe of the
binary search, dispatching on the sign of the returned ``$v0`` to halve
the range. A function pointer is just an address you ``jalr`` to.

Cleanup handlers in reverse: ``atexit-demo``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**atexit-demo** registers cleanup handlers with ``atexit`` and then lets
the program exit; the handlers run in **reverse** order of
registration::

   $ spimulator -f .../libctype.asm -f .../libstdlib.asm -f .../atexit-demo.asm
   main about to exit
   handler 3 ran
   handler 2 ran
   handler 1 ran

.. literalinclude:: ../../../examples/src/lib/libstdlib-demo/atexit-demo.c
   :language: c
   :lines: 18-29
   :caption: lib/libstdlib-demo/atexit-demo.c — handlers and _start

.. literalinclude:: ../../../examples/src/lib/libstdlib/libstdlib.asm
   :language: gas
   :lines: 408-420
   :caption: lib/libstdlib/libstdlib.asm — exit's reverse-walk loop

``atexit`` simply appends each handler's address to a ``handlers[]``
array in the library's data segment and bumps ``handler_count``. The
reversal happens in ``exit``: it loads ``handler_count``, sets ``i =
count - 1``, and walks *downward*, ``jalr``\ ing ``handlers[i]`` each
time — the same function-pointer call as ``bsearch``, but driven by a
table instead of a single argument. Loop state (``i``, the saved status,
the table base) lives in ``$s0``–``$s2`` so it survives each handler
call; when ``i`` goes negative the loop falls through to ``_Exit``, which
hands the status to syscall 17. Registering three handlers and walking
the array backward is exactly why "last registered, first run."

Going further
-------------

This tour covers most of the curriculum; a handful remain under
``examples/src/`` (sequenced in ``examples/READING-ORDER.md``):
``transforms/cut``, ``algorithms/pascals-triangle``, ``intro/clear``,
``extras/print-out-ascii`` and ``extras/testStringsForEquality``, the
other ``libstdlib`` demos (``abs``, ``atoi``, ``exit``), and a few
near-duplicates of programs shown here. Open any of them under
``-explain`` and read along — it is the fastest way to build fluency in
MIPS.
