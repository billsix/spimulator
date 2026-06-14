..
   Copyright 2002 Jonathan Bartlett
   Copyright 2026 William Emerison Six (MIPS/spimulator port)

   Permission is granted to copy, distribute and/or modify this
   document under the terms of the GNU Free Documentation License,
   Version 1.1 or any later version published by the Free Software
   Foundation; with no Invariant Sections, with no Front-Cover Texts,
   and with no Back-Cover Texts.  A copy of the license is included in fdl.xml

   Port note (2026): the i386 instruction reference is replaced with a
   MIPS reference covering the instructions this book uses, as accepted
   by spimulator's assembler.  Section anchors are preserved.

.. _instructionsappendix:

Common MIPS Instructions
========================

Reading the Tables
------------------

This appendix lists the MIPS instructions used in this book, grouped by
purpose. For each one, the *operands* column shows the registers and
values it takes, written the way you write them in source:

-  ``$d`` — a destination register.
-  ``$s``, ``$t`` — source registers.
-  ``imm`` — an immediate (a constant written in the instruction).
-  ``offset($base)`` — a memory address, formed by adding a constant
   ``offset`` to the address in register ``$base``. This is MIPS's one
   memory-addressing form.
-  ``label`` — the name of a place in your program (a branch/jump
   target, or a data location).

Two things distinguish MIPS from the i386 this book was first written
for. First, **MIPS is a load/store architecture**: arithmetic and logic
instructions operate only on registers, and memory is touched only by
the load (``lw``, ``lb``, …) and store (``sw``, ``sb``, …) instructions.
Second, **MIPS has no flags register.** Comparisons do not set hidden
condition codes; instead, ``slt`` writes a 0/1 result into a register,
and the branch instructions compare registers directly. So there is no
"flags" column here — there are no flags.

Many convenient forms (``move``, ``li``, ``la``, ``blt``, ``b``, …) are
*pseudo-instructions*: the assembler expands each into one or more real
machine instructions. They are marked **(pseudo)** below. Run
spimulator with ``-show-expansion`` to see what any of them become.

.. _dtins:

Data Transfer Instructions
--------------------------

These move data between registers and memory, or load constants and
addresses into registers. They do no arithmetic.

.. list-table::
   :header-rows: 1
   :widths: 18 26 40

   * - Instruction
     - Operands
     - Description
   * - ``lw``
     - ``$d, offset($base)``
     - Load the 32-bit word at ``offset+$base`` into ``$d``. The address
       must be word-aligned.
   * - ``sw``
     - ``$s, offset($base)``
     - Store the word in ``$s`` to ``offset+$base`` (word-aligned).
   * - ``lb`` / ``lbu``
     - ``$d, offset($base)``
     - Load one byte, sign-extended (``lb``) or zero-extended (``lbu``),
       into ``$d``.
   * - ``sb``
     - ``$s, offset($base)``
     - Store the low byte of ``$s`` to memory.
   * - ``lh`` / ``lhu`` / ``sh``
     - ``$d, offset($base)``
     - Load/store a 16-bit halfword (sign- or zero-extended on load).
   * - ``lui``
     - ``$d, imm``
     - Load the 16-bit ``imm`` into the *upper* half of ``$d``; the lower
       half is zeroed. The building block for 32-bit constants.
   * - ``li`` (pseudo)
     - ``$d, imm``
     - Load a full 32-bit immediate into ``$d`` (expands to ``ori``, or
       ``lui``+``ori`` for large values).
   * - ``la`` (pseudo)
     - ``$d, label``
     - Load the *address* of ``label`` into ``$d`` (expands to
       ``lui``+``ori``).
   * - ``move`` (pseudo)
     - ``$d, $s``
     - Copy ``$s`` into ``$d`` (expands to ``addu $d, $s, $zero``).
   * - ``mfhi`` / ``mflo``
     - ``$d``
     - Copy the ``HI`` / ``LO`` result register (set by ``mult``/``div``)
       into ``$d``.

.. _intins:

Integer Instructions
--------------------

Basic arithmetic. The ``u`` forms do not trap on overflow; the plain
forms raise an exception on signed overflow.

.. list-table::
   :header-rows: 1
   :widths: 18 26 40

   * - Instruction
     - Operands
     - Description
   * - ``add`` / ``addu``
     - ``$d, $s, $t``
     - ``$d = $s + $t``.
   * - ``addi`` / ``addiu``
     - ``$d, $s, imm``
     - ``$d = $s + imm``.
   * - ``sub`` / ``subu``
     - ``$d, $s, $t``
     - ``$d = $s - $t``.
   * - ``mult`` / ``multu``
     - ``$s, $t``
     - Multiply ``$s`` by ``$t``; the 64-bit product goes to ``HI:LO``.
       Read the low word with ``mflo``.
   * - ``div`` / ``divu``
     - ``$s, $t``
     - Divide ``$s`` by ``$t``; quotient to ``LO`` (``mflo``), remainder
       to ``HI`` (``mfhi``).
   * - ``slt`` / ``sltu``
     - ``$d, $s, $t``
     - Set ``$d`` to 1 if ``$s < $t`` (signed / unsigned), else 0. The
       MIPS way to turn a comparison into a value.
   * - ``slti`` / ``sltiu``
     - ``$d, $s, imm``
     - As ``slt``, comparing against an immediate.
   * - ``neg`` (pseudo)
     - ``$d, $s``
     - ``$d = -$s`` (expands to ``sub $d, $zero, $s``).

.. _logicins:

Logic Instructions
------------------

Bitwise operations and shifts.

.. list-table::
   :header-rows: 1
   :widths: 18 26 40

   * - Instruction
     - Operands
     - Description
   * - ``and`` / ``or`` / ``xor`` / ``nor``
     - ``$d, $s, $t``
     - Bitwise AND / OR / XOR / NOR of ``$s`` and ``$t`` into ``$d``.
   * - ``andi`` / ``ori`` / ``xori``
     - ``$d, $s, imm``
     - Bitwise operation against a 16-bit immediate.
   * - ``not`` (pseudo)
     - ``$d, $s``
     - Bitwise complement (expands to ``nor $d, $s, $zero``).
   * - ``sll`` / ``srl``
     - ``$d, $t, shamt``
     - Shift ``$t`` left / right (logical) by ``shamt`` bits; vacated
       bits filled with 0.
   * - ``sra``
     - ``$d, $t, shamt``
     - Shift right *arithmetic*: vacated high bits filled with the sign
       bit (preserves sign).
   * - ``sllv`` / ``srlv`` / ``srav``
     - ``$d, $t, $s``
     - As above, but the shift amount comes from register ``$s``.

.. _flowins:

Flow Control Instructions
-------------------------

These change which instruction runs next. The conditional branches
compare registers directly — there is no flags register to consult.

.. list-table::
   :header-rows: 1
   :widths: 18 26 40

   * - Instruction
     - Operands
     - Description
   * - ``beq`` / ``bne``
     - ``$s, $t, label``
     - Branch to ``label`` if ``$s == $t`` / ``$s != $t``.
   * - ``blt`` / ``ble`` / ``bgt`` / ``bge`` (pseudo)
     - ``$s, $t, label``
     - Branch on signed ``<`` / ``<=`` / ``>`` / ``>=`` (expand to
       ``slt`` + ``beq``/``bne``).
   * - ``bltz`` / ``blez`` / ``bgtz`` / ``bgez``
     - ``$s, label``
     - Branch if ``$s`` is ``< 0`` / ``<= 0`` / ``> 0`` / ``>= 0``.
   * - ``beqz`` / ``bnez`` (pseudo)
     - ``$s, label``
     - Branch if ``$s`` is zero / non-zero.
   * - ``j``
     - ``label``
     - Jump unconditionally to ``label``.
   * - ``jal``
     - ``label``
     - Jump and link: jump to ``label`` and save the return address (the
       following instruction) in ``$ra``. Used to call a function.
   * - ``jr``
     - ``$s``
     - Jump to the address in ``$s``. ``jr $ra`` returns from a function.
   * - ``jalr``
     - ``$s``
     - Jump to the address in ``$s``, saving the return address in
       ``$ra`` (an indirect call).
   * - ``b`` (pseudo)
     - ``label``
     - Unconditional branch (expands to ``beq $zero, $zero, label``).
   * - ``syscall``
     - —
     - Request an operating-system service: the call number is in
       ``$v0`` and arguments in ``$a0``–``$a3`` (see :ref:`syscallap`).

.. _dirins:

Assembler Directives
--------------------

These are instructions to the *assembler*, not the CPU. They declare
data, mark sections, and control layout. (Note: unlike the GNU
assembler, spimulator has no ``.equ``/``.eqv`` for naming constants, and
no ``.bss``; use ``.space`` in ``.data`` to reserve zeroed storage.)

.. list-table::
   :header-rows: 1
   :widths: 22 36

   * - Directive
     - Description
   * - ``.text``
     - Begin the instructions section.
   * - ``.data``
     - Begin the data section.
   * - ``.globl name``
     - Make ``name`` visible to other source files (needed for ``main``,
       and for routines shared across files).
   * - ``.word v, …``
     - Emit one or more 32-bit words.
   * - ``.half v, …`` / ``.byte v, …``
     - Emit 16-bit halfwords / single bytes.
   * - ``.asciiz "…"``
     - Emit the string followed by a terminating 0 byte.
   * - ``.ascii "…"``
     - Emit the string with no terminator.
   * - ``.space n``
     - Reserve ``n`` bytes, initialized to 0 (used for buffers).
   * - ``.align n``
     - Align the next item to a 2\ :sup:`n`-byte boundary (``.align 2``
       for word alignment).

Assembler Syntax and Pseudo-Instructions
----------------------------------------

The i386 edition of this book ended with a comparison of AT&T and Intel
assembler *syntaxes*. MIPS does not have that split: there is one widely
used MIPS assembly syntax, and spimulator accepts it.

What MIPS *does* have, prominently, is *pseudo-instructions* — convenient
spellings the assembler expands into real machine instructions. ``move``,
``li``, ``la``, ``b``, ``blt``, ``not``, ``neg`` (and others above) are
all pseudo-instructions. This is why the source line you write and the
instruction the CPU runs sometimes differ: ``la $a0, msg`` is one line of
source but becomes a ``lui``/``ori`` pair. Run spimulator with
``-show-expansion`` to see exactly what each pseudo-instruction becomes,
and ``-print-ast`` to see how your whole file parsed.

Where to Go for More Information
--------------------------------

The authoritative references for the MIPS instruction set are:

-  *Computer Organization and Design* by Patterson and Hennessy — built
   around MIPS, with a full instruction reference (and an appendix on
   the SPIM simulator) in its back matter.
-  The MIPS32 Architecture reference manuals — the complete, formal
   instruction-set definition.
-  The documentation for spim and MARS, which describe the exact subset
   of instructions, pseudo-instructions, and directives the simulators
   accept.
